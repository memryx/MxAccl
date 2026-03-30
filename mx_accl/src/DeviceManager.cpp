// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <unordered_map>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <set>

#include "spdlog/spdlog.h"

#include <memx/accl/DeviceManager.h>
#include <memx/accl/client.h>

using namespace MX::Runtime;
using namespace MX::Types;
using namespace MX::Utils;
using namespace std;


DeviceManager::DeviceManager()
{
    all_devices_count = -1; // -1 means not discovered yet
    read_power_mode();
    local_device_in_use.clear();
}

DeviceManager::~DeviceManager()
{
}

std::vector<int> DeviceManager::convert_device_ids(const std::vector<int>& device_ids) 
{
    if (device_ids.empty()) {
        throw std::runtime_error("Argument device_ids_to_use is not allowed to be empty.");
    }

    if (all_devices_count == -1) {
        throw std::runtime_error("Device count has not been discovered yet. Please call discover_devices_direct() first.");
    }

    if (all_devices_count == 0) {
        throw std::runtime_error("No memryx devices found. Plesae check if the driver is properly installed and device is properly connected.");
    }

    std::vector<int> converted_ids(all_devices_count);
    if (device_ids.size() == 1 && device_ids[0] == -1) {
        // convert -1 to a vector of all device ids
        for (int i = 0; i < all_devices_count; i++) {
            converted_ids[i] = i;
        }

    } else {

        // check that all device ids are valid (between 0 and all_devices_count-1)
        for (int device_id : device_ids) {
            if (device_id < 0 || device_id >= all_devices_count) {
                throw std::runtime_error("User provides invalid device id: " + std::to_string(device_id));
            }
        }

        // Compare sizes to see if duplicates existed
        std::set<int> unique_set(device_ids.begin(), device_ids.end());
        if (unique_set.size() < device_ids.size()) {
            spdlog::warn("Duplicate device IDs detected. Removing duplicates.");
            converted_ids.assign(unique_set.begin(), unique_set.end());
        }
        else {
            converted_ids = device_ids;
        }
    }

    return converted_ids;
}

bool DeviceManager::discover_devices_direct()
{
    std::lock_guard<std::mutex> lock(m_discover);

    if(all_devices_count > -1) {
        spdlog::debug("[DeviceManager] Devices already discovered, skipping discovery.");
        return true;
    }

    memx_status status = memx_operation_get_device_count(&all_devices_count);
    if (memx_status_error(status)) {
        spdlog::warn("[DeviceManager] Local memx_operation_get_device_count() failed");
        all_devices_count = 0;
        return false;
    }

    if (all_devices_count == 0) {
        spdlog::warn("[DeviceManager] No Local devices found");
        return false;
    }

    device_infos.resize(all_devices_count);
    local_device_in_use.resize(all_devices_count, false);

    // count chips on all devices
    for(int d = 0; d < all_devices_count ; d++) {
        uint64_t hwinfo64 = 0;

        status = memx_get_feature(d, 0, OPCODE_GET_HW_INFO, &hwinfo64);
        if (memx_status_error(status)) {
            spdlog::warn("[DeviceManager] Local memx_get_feature() failed for device {}.", d);
            hwinfo64 = 0;
        }

        device_infos[d].chip_count = (hwinfo64 & ((uint64_t)0xFFL << 16)) >> 16;
        device_infos[d].chips_per_group = (hwinfo64 & ((uint64_t)0xFFL << 32)) >> 32;
        device_infos[d].num_groups = (hwinfo64 & ((uint64_t)0xFFL << 48)) >> 48;

        if((hwinfo64 & 0xFF) == 0x2){
            device_infos[d].is_usb = false;
        } else {
            device_infos[d].is_usb = true;
        }

        // figure out the correct MEMX_MPU_GROUP_CONFIG_* value from the number of chips and groups
        if(device_infos[d].chip_count == 8 && device_infos[d].num_groups == 1) {
            device_infos[d].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_EIGHT_MPUS;
        }
        else if(device_infos[d].chip_count == 4 && device_infos[d].num_groups == 1) {
            device_infos[d].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_FOUR_MPUS;
        }
        else if(device_infos[d].chip_count == 2 && device_infos[d].num_groups == 2) {
            device_infos[d].current_config = MEMX_MPU_GROUP_CONFIG_TWO_GROUP_TWO_MPUS;
        }
        else if (device_infos[d].chip_count == 2 && device_infos[d].num_groups == 1) {
            device_infos[d].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_TWO_MPUS;
        }
        else {
            // invalid configuration
            spdlog::error("[DeviceManager] Invalid chip count {} or group count {} for device {}",
                          device_infos[d].chip_count, device_infos[d].num_groups, d);
            device_infos[d].current_config = 0;
        }

        // check frequency and voltage
        uint64_t freq = 600;
        int device_chip_count = device_infos[d].chip_count;
        device_infos[d].freqs.resize(device_chip_count, 0);
        for(int i = 0; i < device_chip_count; i++) {
            status = memx_get_feature(d, i, OPCODE_GET_FREQUENCY, &freq);
            if (memx_status_error(status)) {
                spdlog::warn("[DeviceManager] Local memx_get_feature() failed for device {}.", d);
                freq = 600;
            }
            device_infos[d].freqs[i] = (uint16_t) freq & 0xFFFF;
        }
        uint64_t volt = 700;
        status = memx_get_feature(d, 0, OPCODE_GET_VOLTAGE, &volt);
        if (memx_status_error(status)) {
            spdlog::warn("[DeviceManager] Local memx_get_feature() failed for device {}.", d);
            volt = 700;
        }
        device_infos[d].volt = (uint16_t) volt & 0xFFFF;

        // check if device supports power data
        uint64_t power = 0;
        memx_status status = memx_get_feature(d, 0, OPCODE_GET_POWER, &power);
        if(memx_status_no_error(status) && power > 0 && power < 17000) {
            device_infos[d].can_get_power_data = true;
        }
        else {
            device_infos[d].can_get_power_data = false;
        }

    }

    return true;
}


bool DeviceManager::discover_devices_remote(Client* client)
{
    std::lock_guard<std::mutex> lock(m_discover);

    if(all_devices_count > -1) {
        spdlog::debug("[DeviceManager] Devices already discovered, skipping discovery.");
        return true;
    }

    if(client == nullptr) {
        spdlog::error("[DeviceManager] Client is null, cannot discover devices remotely.");
        return false;
    }


    device_infos.clear();
    device_infos = client->get_device_infos();
    all_devices_count = device_infos.size();
    if (all_devices_count == 0) {
        spdlog::warn("[DeviceManager] No Remote devices found");
        return false;
    }
    local_device_in_use.resize(all_devices_count, false);

    return true;
}


void DeviceManager::print_all_devices()
{

    // print the device_info table for all devices
    std::cout << "Device ID | Chip Count | Num Groups | is MXM2 |  Freq | Volt" << std::endl;
    std::cout << "----------|------------|------------|---------|-------|-----" << std::endl;
    for(int d = 0; d < all_devices_count ; d++) {
        std::cout << std::setw(9) << int(d) << " | ";
        std::cout << std::setw(10) << device_infos[d].chip_count << " | ";
        std::cout << std::setw(10) << device_infos[d].num_groups << " | ";
        std::cout << std::setw(7)  << (device_infos[d].can_get_power_data ? "Yes" : "No") << " | ";
        std::cout << std::setw(5)  << device_infos[d].freqs[0] << " | ";
        std::cout << std::setw(4)  << device_infos[d].volt;
        std::cout << std::endl;
    }
    std::cout << std::endl;

}

void DeviceManager::print_devices_info()
{

    // print the device_info table for all devices
    std::cout << "Device ID | Chip Count |  Freq | Volt" << std::endl;
    std::cout << "----------|------------|-------|-----" << std::endl;
    for(int d = 0; d < all_devices_count ; d++) {
        std::cout << std::setw(9) << int(d) << " | ";
        std::cout << std::setw(10) << device_infos[d].chip_count << " | ";
        std::cout << std::setw(5)  << device_infos[d].freqs[0] << " | ";
        std::cout << std::setw(4)  << device_infos[d].volt;
        std::cout << std::endl;
    }
    std::cout << std::endl;

}


void DeviceManager::read_power_mode()
{

#ifdef _WIN32
    std::string config_path = "C:\\Program Files\\memryx\\power.conf";
#else
    std::string config_path = "/etc/memryx/power.conf";
#endif

    if(std::filesystem::exists(config_path)) {
        // read each line
        std::ifstream fd(config_path);
        for( std::string line; getline( fd, line ); ) {
            if(line[0] == '#') {
                continue;
            }
            std::string varname = line.substr(0, 6);
            if(varname == "FREQ4C") {
                std::string val = line.substr(7, 3);
                c4_freq = (uint16_t) std::stoi(val);
            }
            else if(varname == "VOLT4C") {
                std::string val = line.substr(7, 3);
                c4_volt = (uint16_t) std::stoi(val);
            }
            else if(varname == "FREQ2C") {
                std::string val = line.substr(7, 3);
                c2_freq = (uint16_t) std::stoi(val);
            }
            else if(varname == "VOLT2C") {
                std::string val = line.substr(7, 3);
                c2_volt = (uint16_t) std::stoi(val);
            }

        }
    }
    else {
        // set default values
        c4_freq = 600;
        c4_volt = 700;
        c2_freq = 600;
        c2_volt = 700;
    }

}

bool DeviceManager::set_power_mode(int device_id, int num_chips, MX::Types::MxFrequencyOption fop)
{

    memx_status status = MEMX_STATUS_OK;

    // check that the num_chips given is equal to the number of chips in the device,
    // or the valid special case of a 4-chip device working with num_chips==2
    if(device_id < 0 || device_id >= all_devices_count) {
        throw std::runtime_error("Invalid device ID passed to DeviceManager::set_power_mode");
        return false;
    }
    if(num_chips != device_infos[device_id].chip_count) {
        if(device_infos[device_id].chip_count == 4 && num_chips == 2) {
            // this is a special case, so we can ignore it
        }
        else {
            throw std::runtime_error("Invalid number of chips passed to DeviceManager::set_power_mode");
            return false;
        }
    }

    if(fop == MX::Types::MxFrequencyOption::FREQ_USE_CONF) {
        if(num_chips == 2) {
            status = memx_set_feature(device_id, 0, OPCODE_SET_FREQUENCY, c2_freq);
            status = memx_set_feature(device_id, 1, OPCODE_SET_FREQUENCY, c2_freq);
            status = memx_set_feature(device_id, 0, OPCODE_SET_VOLTAGE,   c2_volt);
        }
        else if(num_chips >= 4) {
            // all num_chips >= 4 use the c4 values
            for(int i = 0; i < num_chips; i++) {
                status = memx_set_feature(device_id, i, OPCODE_SET_FREQUENCY, c4_freq);
            }
            status = memx_set_feature(device_id, 0, OPCODE_SET_VOLTAGE, c4_volt);
        }
        else if (num_chips == 1) {
            status = memx_set_feature(device_id, 0, OPCODE_SET_FREQUENCY, c2_freq);
            status = memx_set_feature(device_id, 0, OPCODE_SET_VOLTAGE,   c2_volt);
        }
        else {
            throw std::runtime_error("Invalid number of chips passed to DeviceManager::set_power_mode");
            return false;
        }
    }
    else {
        MX::Types::MxVoltageOption volt = MX::Types::getVoltageFromFrequency(fop);

        // set all chips to the same frequency that's passed in fop
        for(int i = 0; i < num_chips; i++) {
            status = memx_set_feature(device_id, i, OPCODE_SET_FREQUENCY, fop);
        }
        status = memx_set_feature(device_id, 0, OPCODE_SET_VOLTAGE, volt);
    }

    return memx_status_no_error(status);
}


bool DeviceManager::configure_groups(int device_id, int device_chip_count, int pdfp_num_chips)
{

    memx_status status = MEMX_STATUS_OK;

    //Change the MPU config based on DFP if needed
    if(pdfp_num_chips == 8 && device_chip_count == 8) {
        status = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_EIGHT_MPUS);
    }
    else if(pdfp_num_chips == 4) {
        status = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_FOUR_MPUS);
    }
    else if(pdfp_num_chips == 2) {
        if (device_chip_count == 2) {
            status = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_TWO_MPUS);
        }
        else {
            status = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_TWO_GROUP_TWO_MPUS);
        }
    }
    else {
        // invalid combination
        spdlog::error("[DeviceMgr] Invalid DFP chip count {} for device {} with chip count {}", pdfp_num_chips, device_id,
                      device_chip_count);
        return false;
    }

    return memx_status_no_error(status);
}


float DeviceManager::get_power(int device_id)
{

    if(device_id < 0 || device_id >= all_devices_count) {
        std::cerr << "Invalid device ID given to DeviceManager::get_power: " << device_id << std::endl;
        return -1.0f;
    }

    if(device_infos[device_id].can_get_power_data) {
        uint64_t power = 0;
        memx_get_feature(device_id, 0, OPCODE_GET_POWER, &power);
        return (float) power;
    }
    else {
        std::cerr << "Connected device does not support power measurement\n";
        return -1.0f;
    }
}


float DeviceManager::get_pressure(int device_id)
{

    if(device_id < 0 || device_id >= all_devices_count) {
        std::cerr << "Invalid device ID given to DeviceManager::get_pressure: " << device_id << std::endl;
        return -1.0f;
    }

    uint64_t util = 0;
    memx_get_feature(device_id, 0, OPCODE_GET_MPU_UTILIZATION, &util);
    return (float) util;
}


float DeviceManager::get_max_temperature(int device_id)
{

    if(device_id < 0 || device_id >= all_devices_count) {
        std::cerr << "Invalid device ID given to DeviceManager::get_max_temperature: " << device_id << std::endl;
        return -1.0f;
    }

    float device_max_temp = -999.9f;
    for (uint8_t chip_num = 0; chip_num < device_infos[device_id].chip_count; chip_num++) {
        uint64_t temperature = 0;
        memx_get_feature(device_id, chip_num, OPCODE_GET_TEMPERATURE, &temperature);
        temperature -= 273;
        if(device_max_temp < (float) temperature) {
            device_max_temp = (float) temperature;
        }
    }

    return device_max_temp;
}


std::vector<float> DeviceManager::get_chip_temperatures(int device_id)
{

    if(device_id < 0 || device_id >= all_devices_count) {
        std::cerr << "Invalid device ID given to DeviceManager::get_chip_temperatures: " << device_id << std::endl;
        return {};
    }

    std::vector<float> chip_temperatures(device_infos[device_id].chip_count);
    for (uint8_t chip_num = 0; chip_num < device_infos[device_id].chip_count; chip_num++) {
        uint64_t temperature = 0;
        memx_get_feature(device_id, chip_num, OPCODE_GET_TEMPERATURE, &temperature);
        chip_temperatures[chip_num] = (float) (temperature - 273);
    }

    return chip_temperatures;
}
