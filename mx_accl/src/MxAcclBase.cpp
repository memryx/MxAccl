// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <sstream>

#include "spdlog/spdlog.h"

#include <memx/accl/MxAcclBase.h>
#include <memx/accl/client.h>
#include <memx/accl/utils/cpu_opts.h>

using namespace MX::Runtime;
using namespace MX::Types;
using namespace MX::Utils;
using namespace MX::RPC;
using namespace std;

static constexpr const char* CLASS_NAME = "MxAcclBase";

MxModel* MxAcclBase::get_model(int model_id) const
{
    MxModel* model = dfp_runner->get_model(model_id);
    if (!model) {
        std::string msg = fmt::format("model_id {} is not found", model_id);
        spdlog::error(msg);
        throw std::runtime_error(msg);
    }
    return model;
}

MxModel* MxAcclBase::get_model_or_throw(int model_id, const std::string &class_name, const std::string &func_name) const
{
    MxModel* model = dfp_runner->get_model(model_id);
    if (!model) {
        std::string msg = fmt::format("[{}] Error in {}: model_id {} is not found", class_name, func_name, model_id);
        spdlog::error(msg);
        throw std::runtime_error(msg);
    }
    return model;
}

// basic constructor -- for calling connect_dfp later
MxAcclBase::MxAcclBase(std::string server_addr, unsigned int server_port_base, bool ignore_server)
{
    ignore_server_ = ignore_server;
    server_addr_ = server_addr;
    server_port_base_ = server_port_base;

    // set my thread affinity to big cores, requiring at least 4
    set_self_affinity_to_big_cores(4);

    device_manager = new MX::Runtime::DeviceManager();
}

// AIO constructor using file path
MxAcclBase::MxAcclBase(const std::filesystem::path &dfp_path, std::vector<int> device_ids_to_use,
                       std::array<bool, 2> use_model_shape, bool local_mode,
                       SchedulerOptions sched_options, ClientOptions client_options,
                       std::string server_addr, unsigned int server_port_base, bool ignore_server)
    : MxAcclBase(server_addr, server_port_base, ignore_server)
{
    // connect the dfp
    int dfp_id = connect_dfp(dfp_path, device_ids_to_use, use_model_shape, local_mode, sched_options, client_options);
    if(dfp_id < 0) {
        std::string err_msg = "[MxAcclBase] Error in MxAcclBase constructor: connect_dfp failed.";
        spdlog::error(err_msg);
        throw std::runtime_error(err_msg);
    }

    if(local_mode) {
        // init pressure history
        pressure_thread_running.store(true, std::memory_order_relaxed);
        pressure_history.resize(device_manager->all_devices_count);
        pressure_avgs.resize(device_manager->all_devices_count, 0.0f);
        pressure_thread = new std::thread(&MxAcclBase::pressure_thread_func, this, device_ids_to_use);
    }
    else {
        pressure_thread = nullptr;
        pressure_thread_running.store(false, std::memory_order_relaxed);
    }
}


// AIO constructor using bytes
MxAcclBase::MxAcclBase(uint8_t* dfp_bytes, size_t dfp_byte_size, std::vector<int> device_ids_to_use,
                       std::array<bool, 2> use_model_shape, bool local_mode,
                       SchedulerOptions sched_options, ClientOptions client_options,
                       std::string server_addr, unsigned int server_port_base, bool ignore_server)
    : MxAcclBase(server_addr, server_port_base, ignore_server)
{   
    // connect the dfp
    int dfp_id = connect_dfp(dfp_bytes, dfp_byte_size, device_ids_to_use, use_model_shape, local_mode, sched_options, client_options);
    if(dfp_id < 0) {
        std::string err_msg = "[MxAcclBase] Error in MxAcclBase constructor: connect_dfp failed.";
        spdlog::error(err_msg);
        throw std::runtime_error(err_msg);
    }

    if(local_mode) {
        // init pressure history
        pressure_thread_running.store(true, std::memory_order_relaxed);
        pressure_history.resize(device_manager->all_devices_count);
        pressure_avgs.resize(device_manager->all_devices_count, 0.0f);
        pressure_thread = new std::thread(&MxAcclBase::pressure_thread_func, this, device_ids_to_use);
    }
    else {
        pressure_thread = nullptr;
        pressure_thread_running.store(false, std::memory_order_relaxed);
    }
}

MxAcclBase::~MxAcclBase()
{
    // stop pressure thread if local mode
    if(pressure_thread != nullptr) {
        pressure_thread_running.store(false, std::memory_order_relaxed);
        pressure_thread->join();
        delete pressure_thread;
        pressure_thread = nullptr;
    }
    // close dfp runner
    if (dfp_runner != nullptr) {
        if(dfp_runner->is_local()) {
            dfp_runner->close_local();
        }
        else {
            dfp_runner->close_shared();
        }
        delete dfp_runner;
        dfp_runner = nullptr;
    }
    device_to_dfp_id_map.clear();
    delete device_manager;
}

bool MxAcclBase::is_ready()
{
    // check if we have any valid DFPRunners, or if ignore_server_ is set to true then we check if the device manager has > 0 devices
    if(ignore_server_) {
        return device_manager->all_devices_count > 0;
    }
    else {
        return dfp_runner != nullptr;
    }
}

//==============================================================================================
// LOCAL MODE PRESSURE MONITOR
//==============================================================================================

void MxAcclBase::pressure_thread_func(std::vector<int> device_ids)
{
    // this thread periodically polls each device in device_ids
    // and updates an average pressure value for each device
    // the get_pressure function just returns the average
    while(pressure_thread_running.load(std::memory_order_relaxed)) {

        for(auto device_id : device_ids) {
            if(device_id < 0 || device_id >= device_manager->all_devices_count) {
                continue;
            }
            float p = device_manager->get_pressure(device_id);
            if(p < 0.0f || p > 100.0f) {
                continue;
            }
            auto &history = pressure_history[device_id];
            history.push_back(p);
            if(history.size() > pressure_history_len) {
                history.pop_front();
            }

            // average the history into the pressure_avgs vect
            float sum = 0.0f;
            #pragma omp simd reduction(+:sum)
            for(unsigned int i = 0; i < history.size(); i++) {
                sum += history[i];
            }
            pressure_avgs[device_id] = sum / static_cast<float>(history.size());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pressure_poll_interval_ms));
    }
}


//==============================================================================================
// CONNECT DFP FUNCTIONS
//==============================================================================================

// multi-device from bytes
int MxAcclBase::connect_dfp(uint8_t* dfp_bytes, size_t dfp_byte_size,
                            std::vector<int> device_ids_to_use,
                            std::array<bool, 2> use_model_shape,
                            bool local_mode,
                            SchedulerOptions sched_options, ClientOptions client_options)
{

    int dfp_id = -1;
    if(dfp_bytes == nullptr) {
        spdlog::error("[MxAcclBase] Error in connect_dfp: dfp_bytes is null");
        return -1;
    }

    // cannot have local_mode=false and ignore_server_=true at the same time
    if(ignore_server_ && !local_mode) {
        spdlog::error("[MxAcclBase] Error in connect_dfp: cannot have ignore_server with local_mode=false");
        return -1;
    }

    // generate a new dfp_id
    dfp_id = dfp_id_tracker.get_new();

    // parse the dfp_bytes into a Dfp::DfpObject
    Dfp::DfpObject* dfp = new Dfp::DfpObject(dfp_bytes, dfp_byte_size);

    // create the runner
    dfp_runner = new DFPRunner(dfp_id, dfp, server_addr_, server_port_base_, local_mode,
                               device_ids_to_use, use_model_shape, sched_options,
                               client_options, device_manager, ignore_server_);
    
    // if local mode, set dfp_runner->init_local() and record lock successes/fails
    if(local_mode) {
        if(dfp_runner->init_local() == false) {
            spdlog::error("[MxAcclBase] Error in connect_dfp: dfp_runner->init_local() failed");
            delete dfp_runner;
            dfp_runner = nullptr;
            dfp_id_tracker.retire(dfp_id);
            return -1;
        }
        else {
            for(auto device_id : dfp_runner->get_converted_device_ids_to_use()) {
                device_manager->local_device_in_use[device_id] = true;
                device_to_dfp_id_map[device_id] = dfp_id;
            }
        }
    }
    else {
        if(dfp_runner->init_shared() == false) {
            spdlog::error("[MxAcclBase] Error in connect_dfp: dfp_runner->init_shared() failed");
            delete dfp_runner;
            dfp_runner = nullptr;
            dfp_id_tracker.retire(dfp_id);
            return -1;
        }
        else {
            for(auto device_id : dfp_runner->get_converted_device_ids_to_use()) {
                device_to_dfp_id_map[device_id] = dfp_id;
            }
        }
    }

    // return the dfp_id
    return dfp_id;
}

// multi-device from file
int MxAcclBase::connect_dfp(const std::filesystem::path dfp_path, std::vector<int> device_ids_to_use,
                            std::array<bool, 2> use_model_shape,
                            bool local_mode,
                            SchedulerOptions sched_options, ClientOptions client_options)
{

    int dfp_id = -1;
    if(dfp_path.empty()) {
        spdlog::error("[MxAcclBase] Error in connect_dfp: dfp_path is empty");
        return -1;
    }

    // cannot have local_mode=false and ignore_server_=true at the same time
    if(ignore_server_ && !local_mode) {
        spdlog::error("[MxAcclBase] Error in connect_dfp: cannot have ignore_server with local_mode=false");
        return -1;
    }

    // generate a new dfp_id
    dfp_id = dfp_id_tracker.get_new();

    // parse the dfp_bytes into a Dfp::DfpObject
    Dfp::DfpObject* dfp = new Dfp::DfpObject(dfp_path.string());

    // create the runner
    dfp_runner = new DFPRunner(dfp_id, dfp, server_addr_, server_port_base_, local_mode,
                               device_ids_to_use, use_model_shape, sched_options,
                               client_options, device_manager, ignore_server_);

    // if local mode, set dfp_runner->init_local() and record lock successes/fails
    if(local_mode) {
        if(dfp_runner->init_local() == false) {
            spdlog::error("[MxAcclBase] Error in connect_dfp: dfp_runner->init_local() failed");
            delete dfp_runner;
            dfp_runner = nullptr;
            dfp_id_tracker.retire(dfp_id);
            return -1;
        }
        else {
            // add the runner to local devices in use list
            for(auto device_id : device_ids_to_use) {
                device_manager->local_device_in_use[device_id] = true;
                device_to_dfp_id_map[device_id] = dfp_id;
            }
        }
    }
    else {
        if(dfp_runner->init_shared() == false) {
            spdlog::error("[MxAcclBase] Error in connect_dfp: dfp_runner->init_shared() failed");
            delete dfp_runner;
            dfp_runner = nullptr;
            dfp_id_tracker.retire(dfp_id);
            return -1;
        }
        else {
            // add the runner to local devices in use list
            for(auto device_id : device_ids_to_use) {
                device_to_dfp_id_map[device_id] = dfp_id;
            }
        }
    }

    // return the dfp_id
    return dfp_id;

}


//==============================================================================================
// MISC "GET" FUNCTIONS
//==============================================================================================

int MxAcclBase::get_num_models()
{
    return dfp_runner->num_models;
}

int MxAcclBase::get_dfp_num_chips()
{
    return dfp_runner->dfp_->get_dfp_meta()->num_chips;
}

std::vector<int> MxAcclBase::get_converted_device_ids_to_use() const 
{
    return dfp_runner->get_converted_device_ids_to_use();
}

MxModelInfo MxAcclBase::get_model_info(int model_id) const
{
    return get_model_or_throw(model_id, CLASS_NAME, __func__)->get_model_info();
}

MxModelInfo MxAcclBase::get_pre_model_info(int model_id) const
{
    return get_model_or_throw(model_id, CLASS_NAME, __func__)->get_pre_model_info();
}

MxModelInfo MxAcclBase::get_post_model_info(int model_id) const
{
    return get_model_or_throw(model_id, CLASS_NAME, __func__)->get_post_model_info();
}

//==============================================================================================
// MISC "SET" FUNCTIONS
//==============================================================================================

void MxAcclBase::connect_post_model(std::filesystem::path post_model_path, int model_id,
                                    const std::vector<size_t> &post_size_list)
{
    return get_model_or_throw(model_id, CLASS_NAME, __func__)->model_set_post(post_model_path, post_size_list);
}

void MxAcclBase::connect_pre_model(std::filesystem::path pre_model_path, int model_id)
{
    return get_model_or_throw(model_id, CLASS_NAME, __func__)->model_set_pre(pre_model_path);
}

void MxAcclBase::set_parallel_fmap_convert(int num_threads, int model_id)
{
    return get_model_or_throw(model_id, CLASS_NAME, __func__)->set_parallel_fmap_convert(num_threads);
}

//==============================================================================================
// POWER, TEMP, AND PRESSURE FUNCTIONS
//==============================================================================================

bool MxAcclBase::can_get_power_consumption(int device_id)
{
    if(device_manager == nullptr) {
        spdlog::error("[MxAcclBase] Error in can_get_power_consumption: device_manager is null");
        return false;
    }

    if(device_id < 0 || device_id >= device_manager->all_devices_count) {
        spdlog::error("[MxAcclBase] Error in can_get_power_consumption: device_id {} is out of range", device_id);
        return false;
    }

    // this function doesn't actually differ between local and shared mode
    return device_manager->device_infos[device_id].can_get_power_data;
}

float MxAcclBase::get_power(int device_id)
{
    auto it = device_to_dfp_id_map.find(device_id);
    if(UNLIKELY(it == device_to_dfp_id_map.end())) {
        spdlog::error("[MxAcclBase] Error in get_power: device_id {} not found in device_to_dfp_id_map", device_id);
        return -1;
    }

    bool is_local = dfp_runner->is_local();

    if(!is_local) {
        Client* client = dfp_runner->get_first_client();

        // call client->get_avg_max_temp(device_id)
        if(UNLIKELY(client == nullptr)) {
            spdlog::error("[MxAcclBase] Error in get_power: client is null");
            return -1;
        }

        return client->get_avg_power(device_id);
    }
    else {
        if(device_id < 0 || device_id >= device_manager->all_devices_count) {
            spdlog::error("[MxAcclBase] Error in get_power: device_id {} is out of range", device_id);
            return -1;
        }

        return device_manager->get_power(device_id);
    }
}

float MxAcclBase::get_max_temperature(int device_id)
{
    // get the DFPRunner for this device_id
    auto it = device_to_dfp_id_map.find(device_id);
    if(UNLIKELY(it == device_to_dfp_id_map.end())) {
        spdlog::error("[MxAcclBase] Error in get_max_temperature: device_id {} not found in device_to_dfp_id_map", device_id);
        return -1;
    }

    bool is_local = dfp_runner->is_local();

    if(!is_local) {
        Client* client = dfp_runner->get_first_client();

        // call client->get_avg_max_temp(device_id)
        if(UNLIKELY(client == nullptr)) {
            spdlog::error("[MxAcclBase] Error in get_max_temperature: client is null");
            return -1;
        }

        return client->get_inst_max_temp(device_id);
    }
    else {
        if(device_id < 0 || device_id >= device_manager->all_devices_count) {
            spdlog::error("[MxAcclBase] Error in get_max_temperature: device_id {} is out of range", device_id);
            return -1;
        }

        return device_manager->get_max_temperature(device_id);
    }
}

std::vector<float> MxAcclBase::get_chip_temperatures(int device_id)
{
    // get the DFPRunner for this device_id
    auto it = device_to_dfp_id_map.find(device_id);
    if(UNLIKELY(it == device_to_dfp_id_map.end())) {
        spdlog::error("[MxAcclBase] Error in get_chip_temperatures: device_id {} not found in device_to_dfp_id_map", device_id);
        return std::vector<float>();
    }

    bool is_local = dfp_runner->is_local();

    if(!is_local) {
        Client* client = dfp_runner->get_first_client();

        // call client->get_avg_max_temp(device_id)
        if(UNLIKELY(client == nullptr)) {
            spdlog::error("[MxAcclBase] Error in get_chip_temperatures: client is null");
            return std::vector<float>();
        }

        return client->get_avg_temp_per_chip(device_id);
    }
    else {
        if(device_id < 0 || device_id >= device_manager->all_devices_count) {
            spdlog::error("[MxAcclBase] Error in get_chip_temperatures: device_id {} is out of range", device_id);
            return std::vector<float>();
        }

        return device_manager->get_chip_temperatures(device_id);
    }
}

Pressure MxAcclBase::get_pressure(int device_id)
{
    // get the DFPRunner for this device_id
    auto it = device_to_dfp_id_map.find(device_id);
    if(UNLIKELY(it == device_to_dfp_id_map.end())) {
        spdlog::error("[MxAcclBase] Error in get_pressure: device_id {} not found in device_to_dfp_id_map", device_id);
        return Pressure(Pressure::Level::FULL);
    }

    bool is_local = dfp_runner->is_local();

    if(!is_local) {
        Client* client = dfp_runner->get_first_client();

        // call client->get_pressure(device_id)
        if(UNLIKELY(client == nullptr)) {
            spdlog::error("[MxAcclBase] Error in get_pressure: client is null");
        }

        float p = client->get_pressure(device_id);
        if(p < MEMX_PRESSURE_LOW_THRESH) {
            return Pressure(Pressure::Level::LOW);
        }
        else if(p < MEMX_PRESSURE_MEDIUM_THRESH) {
            return Pressure(Pressure::Level::MEDIUM);
        }
        else if(p < MEMX_PRESSURE_HIGH_THRESH) {
            return Pressure(Pressure::Level::HIGH);
        }
        else {
            return Pressure(Pressure::Level::FULL);
        }
    }
    else {
        if(device_id < 0 || ((unsigned int)device_id) >= pressure_avgs.size()) {
            spdlog::error("[MxAcclBase] Error in get_pressure: device_id {} is out of range", device_id);
            return -1;
        }

        float p = pressure_avgs[device_id];
        if(p < MEMX_PRESSURE_LOW_THRESH) {
            return Pressure(Pressure::Level::LOW);
        }
        else if(p < MEMX_PRESSURE_MEDIUM_THRESH) {
            return Pressure(Pressure::Level::MEDIUM);
        }
        else if(p < MEMX_PRESSURE_HIGH_THRESH) {
            return Pressure(Pressure::Level::HIGH);
        }
        else {
            return Pressure(Pressure::Level::FULL);
        }
    }
}


//==============================================================================================
// DEVICE CONTROL FUNCTIONS
//==============================================================================================

bool MxAcclBase::set_operating_frequency(int device_id, MxFrequencyOption freq_option)
{
    // get the DFPRunner for this device_id
    auto it = device_to_dfp_id_map.find(device_id);
    if(UNLIKELY(it == device_to_dfp_id_map.end())) {
        spdlog::error("[MxAcclBase] Error in set_operating_frequency: device_id {} not found in device_to_dfp_id_map", device_id);
        return false;
    }

    bool is_local = dfp_runner->is_local();

    if(!is_local) {
        Client* client = dfp_runner->get_first_client();

        // call client->get_avg_max_temp(device_id)
        if(UNLIKELY(client == nullptr)) {
            spdlog::error("[MxAcclBase] Error in set_operating_frequency: client is null");
            return false;
        }

        return client->set_power_mode(device_id, (uint16_t) freq_option);
    }
    else {
        if(device_id < 0 || device_id >= device_manager->all_devices_count) {
            spdlog::error("[MxAcclBase] Error in set_operating_frequency: device_id {} is out of range", device_id);
            return false;
        }

        // get the device chip count from device_manager
        int device_chip_count = device_manager->device_infos[device_id].chip_count;

        return device_manager->set_power_mode(device_id, device_chip_count, freq_option);
    }

}
