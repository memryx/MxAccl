// Copyright (c) 2025 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memx/memx.h>

#include "spdlog/spdlog.h"
#ifdef _WIN32
    #include <spdlog/sinks/win_eventlog_sink.h>
#endif

#include "dfp_executor.h"

using mxasio::ip::tcp;

using namespace MX::Manager;
using namespace MX::RPC;

// DFP Executor
//---------------------------------------------------------
//---------------------------------------------------------
//---------------------------------------------------------
DFPExecutor::DFPExecutor(uint8_t device_id_, std::vector<device_info_t>* devinfos_, const BlockyQueue<ExecutorTask*> *my_exec_queue_, unsigned int hw_monitor_interval_ms)
    : device_id(device_id_), my_exec_queue(my_exec_queue_), devinfos(devinfos_), hw_monitor_interval(std::chrono::milliseconds(hw_monitor_interval_ms))
{
    num_chips = devinfos->at(device_id).chip_count;
    can_get_power = devinfos->at(device_id).can_get_power_data;

    // initialize my module to default freq/volt options
    read_power_mode();
    current_freq_option = MX::Types::MxFrequencyOption::FREQ_USE_CONF;
    next_freq_option = MX::Types::MxFrequencyOption::FREQ_USE_CONF;
    set_power_mode(MX::Types::MxFrequencyOption::FREQ_USE_CONF);

    hw_monitor_running = false;
    hw_monitor_thread = nullptr;

    ct_autoclock_enabled = false;
    ct_power_limit_mw = 15000; // basically uncapped
    ct_freq_option_ptr = nullptr;
    ct_original_freq_option = MX::Types::MxFrequencyOption::FREQ_USE_CONF;

    // initialize the thread pairs map
    thread_pairs.clear();

    // clear dumpsters
    dumpster = nullptr;
    dumpster_size = 0;

    n_models = 0;

    spdlog::info("DFPExecutor {}: created DFPExecutor for device ID {} with num chips {} and can_get_power {}",
                 device_id, device_id, num_chips, can_get_power);
}

DFPExecutor::~DFPExecutor()
{

    // stop all the thread pairs
    for(auto it = thread_pairs.begin(); it != thread_pairs.end(); it++) {
        ModelThreadPair* pair = it->second;
        delete pair; // sets SF_TERMINATE and kills threads
    }

    stop_hw_monitor();

    // clear the map
    thread_pairs.clear();

    {
        std::lock_guard<std::mutex> lock(m_dumpster_lock);
        if(dumpster != nullptr) {
            delete [] dumpster;
            dumpster = nullptr;
        }
    }
}


//---------------------------------------------------------
//---------------------------------------------------------

void DFPExecutor::start_hw_monitor()
{
    std::unique_lock<std::mutex> lock(m_hw_monitor_lock);
    if(hw_monitor_running == false) {
        hw_monitor_running = true;
        lock.unlock();
        hw_monitor_thread = new std::thread(&DFPExecutor::hw_monitor_loop, this);
        spdlog::debug("DFPExecutor {}: started hardware monitor thread", device_id);
    }
    else {
        //spdlog::error("DFPExecutor {}: hardware monitor thread already running", device_id);
        lock.unlock();
    }
}

void DFPExecutor::stop_hw_monitor()
{
    std::unique_lock<std::mutex> lock(m_hw_monitor_lock);
    if(hw_monitor_running == true) {
        hw_monitor_running = false;
        if(hw_monitor_thread != nullptr) {
            lock.unlock();
            if(hw_monitor_thread->joinable()) {
                hw_monitor_thread->join();
            }
            delete hw_monitor_thread;
            hw_monitor_thread = nullptr;
            spdlog::debug("DFPExecutor {}: stopping hardware monitor thread", device_id);
        }
        else {
            lock.unlock();
        }
    }
    else {
        //spdlog::info("DFPExecutor {}: hardware monitor thread not running", device_id);
        lock.unlock();
    }
}

void DFPExecutor::hw_monitor_loop()
{

    // initial timestamp
    std::chrono::steady_clock::time_point base_time = std::chrono::steady_clock::now();

    std::vector<uint64_t> tvalues(num_chips, 0);

    {
        std::unique_lock<std::shared_mutex> lock(m_temp_power);
        temp_window.resize(num_chips);
        _avg_temps.resize(num_chips);

        // clear power window
        power_window.clear();

        // clear pressure window
        pressure_window.clear();

        // clear each temp_window
        for(uint8_t i = 0; i < num_chips; i++) {
            temp_window[i].clear();
        }
    }

    uint64_t pvalue = 0;
    uint64_t uvalue = 0;



    for(;;) {

        // sleep for base_time + interval
        std::this_thread::sleep_until(base_time + hw_monitor_interval);

        {
            std::unique_lock<std::mutex> hlock(m_hw_monitor_lock);
            if(hw_monitor_running == false) {
                hlock.unlock();
                spdlog::debug("DFPExecutor {}: hardware monitor thread stopped", device_id);
                return; // die
            }
            else {

                // get temp of each chip
                for(uint8_t i = 0; i < num_chips; i++) {
                    memx_status status = memx_get_feature(device_id, i, OPCODE_GET_TEMPERATURE, &(tvalues[i]));
                    if(UNLIKELY(memx_status_error(status))) {
                        spdlog::error("DFPExecutor {}: memx_get_chip_temp() failed with error {} for chip {}", device_id, (uint32_t)status, i);
                        tvalues[i] = 0; // set to 0 on error
                        hlock.unlock();
                        return; // die
                    }
                }

                // get power value
                if(can_get_power) {
                    memx_status status = memx_get_feature(device_id, 0, OPCODE_GET_POWER, &pvalue);
                    if(UNLIKELY(memx_status_error(status))) {
                        spdlog::error("DFPExecutor {}: memx_get_power() failed with error {}", device_id, (uint32_t)status);
                        pvalue = 0; // set to 0 on error
                        hlock.unlock();
                        return; // die
                    }

                    // check if we are exceeding power limit and update stuff if so
                    if(ct_autoclock_enabled && ct_power_limit_mw.load() > 0){

                        spdlog::debug("DFPExecutor {}: current power = {} mW, power limit = {} mW",
                                      device_id, pvalue, ct_power_limit_mw.load());

                        if(pvalue > ct_power_limit_mw.load() && ct_freq_option_ptr != nullptr){

                            // we are exceeding power limit -- bump this DFP's freq down by 25 MHz for next time it runs
                            MX::Types::MxFrequencyOption cur_opt = ct_original_freq_option;

                            if(*(ct_freq_option_ptr) != cur_opt){
                                // we've already decreased but it has not taken effect yet
                                // so just skip
                                spdlog::debug("DFPExecutor {}: frequency already adjusted to {}, waiting for it to take effect",
                                              device_id, MX::Types::mxFrequencyOptionToString(*(ct_freq_option_ptr)));
                            }
                            else if(((unsigned int)cur_opt) > 200){ // 0 and 1 are invalid, and 200 is the lower limit
                                *(ct_freq_option_ptr) = (MX::Types::MxFrequencyOption) ((unsigned int)cur_opt - 25);
                                spdlog::warn("DFPExecutor {}: power limit of {} mW exceeded (current power: {} mW); reduced frequency to {}",
                                         device_id, ct_power_limit_mw.load(), pvalue, MX::Types::mxFrequencyOptionToString(*(ct_freq_option_ptr)));

                                // interrupt the current task
                                interrupt_task();
                            }


                            //------------------------------------------------------------//
                            // TODO: also have a way to go back if the situation improves //
                            //------------------------------------------------------------//
                        }
                    }
                }

                // get pressure (utilization) value
                memx_status status = memx_get_feature(device_id, 0, OPCODE_GET_MPU_UTILIZATION, &uvalue);
                if(UNLIKELY(memx_status_error(status))) {
                    spdlog::error("DFPExecutor {}: memx_get_mpu_utilization() failed with error {}", device_id, (uint32_t)status);
                    uvalue = 0; // set to 0 on error
                    hlock.unlock();
                    return; // die
                }

                // don't need hardware access anymore
                hlock.unlock();

                // acquire exclusive lock and update values
                {
                    std::unique_lock<std::shared_mutex> lock(m_temp_power);

                    if(can_get_power) {
                        power_window.push_back(static_cast<float>(pvalue));
                        // div by 2 so the window for pressure is half as small
                        // (need a fast response time)
                        if(power_window.size() > (hw_monitor_avg_samples / 2)) {
                            power_window.pop_front(); // remove oldest sample
                        }
                    }

                    for(uint8_t i = 0; i < num_chips; i++) {
                        temp_window[i].push_back(static_cast<float>(tvalues[i]) - 273.0); // convert Kelvin to Celsius
                        if(temp_window[i].size() > hw_monitor_avg_samples) {
                            temp_window[i].pop_front(); // remove oldest sample
                        }
                    }

                    pressure_window.push_back(static_cast<float>(uvalue));
                    if(pressure_window.size() > hw_monitor_avg_samples) {
                        pressure_window.pop_front(); // remove oldest sample
                    }

                    // update the averages
                    _avg_power = 0.0f;
                    if(can_get_power) {
                        for(unsigned int j = 0; j < power_window.size(); j++) {
                            _avg_power += power_window[j];
                        }
                        if(!power_window.empty()) {
                            _avg_power /= static_cast<float>(power_window.size());
                        }
                    }

                    _avg_pressure = 0.0f;
                    for(unsigned int j = 0; j < pressure_window.size(); j++) {
                        _avg_pressure += pressure_window[j];
                    }
                    if(!pressure_window.empty()) {
                        _avg_pressure /= static_cast<float>(pressure_window.size());
                    }

                    for(uint8_t i = 0; i < num_chips; i++) {
                        _avg_temps[i] = 0.0f;
                        for(unsigned int j = 0 ; j < temp_window[i].size(); j++) {
                            _avg_temps[i] += temp_window[i][j];
                        }
                        if(!temp_window[i].empty()) {
                            _avg_temps[i] /= static_cast<float>(temp_window[i].size());
                        }
                    }
                } // m_temp_power lock scope

            } // hw_monitor_running check scope

            // update the base time for the next iteration
            base_time = std::chrono::steady_clock::now();

        } // m_hw_monitor_lock scope
    }

    spdlog::debug("DFPExecutor {}: hardware monitor thread stopped", device_id);

}

//---------------------------------------------------------

// returns avg_temps
const std::vector<float> &DFPExecutor::avg_all_temps()
{
    std::shared_lock<std::shared_mutex> lock(m_temp_power);
    return _avg_temps;
}

// returns a vector (each elem is a chip) at the back (most recent) of the temp_window deque
const std::vector<float> DFPExecutor::inst_all_temps()
{
    std::shared_lock<std::shared_mutex> lock(m_temp_power);
    std::vector<float> inst_temps;
    for(const auto &temp_deque : temp_window) {
        if(!temp_deque.empty()) {
            inst_temps.push_back(temp_deque.back());
        }
        else {
            inst_temps.push_back(0.0f); // if empty, return 0.0
        }
    }
    return inst_temps;
}

// returns the maximum value of the avg_temps vector
float DFPExecutor::avg_max_temp()
{
    std::shared_lock<std::shared_mutex> lock(m_temp_power);
    float max_temp = -1000.0f;
    for(const auto &temp : _avg_temps) {
        if(temp > max_temp) {
            max_temp = temp;
        }
    }
    return max_temp;
}

// returns the maximum value of the back of the temp_window deque (most recent)
float DFPExecutor::inst_max_temp()
{
    std::shared_lock<std::shared_mutex> lock(m_temp_power);
    float max_temp = -1000.0f;
    for(const auto &temp_deque : temp_window) {
        if(!temp_deque.empty()) {
            if(temp_deque.back() > max_temp) {
                max_temp = temp_deque.back();
            }
        }
    }
    return max_temp;
}

// returns the average power value
float DFPExecutor::avg_power()
{
    std::shared_lock<std::shared_mutex> lock(m_temp_power);
    return _avg_power;
}

// returns the average pressure (pipeline utilization)
float DFPExecutor::avg_pressure()
{
    std::shared_lock<std::shared_mutex> lock(m_temp_power);
    return _avg_pressure;
}

// returns the instantaneous power value (most recent in deque)
float DFPExecutor::inst_power()
{
    std::shared_lock<std::shared_mutex> lock(m_temp_power);
    if(!power_window.empty()) {
        return power_window.back();
    }
    else {
        return 0.0f; // if empty, return 0.0
    }
}


uint8_t DFPExecutor::get_num_chips()
{
    return num_chips;
}

//---------------------------------------------------------

void DFPExecutor::read_power_mode()
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

void DFPExecutor::set_next_power_mode(MX::Types::MxFrequencyOption fop)
{
    std::lock_guard<std::mutex> lock(set_freq_mutex);
    next_freq_option = fop;
}

bool DFPExecutor::set_power_mode(MX::Types::MxFrequencyOption fop)
{
    memx_status status;

    if(fop == MX::Types::MxFrequencyOption::FREQ_USE_CONF) {
        if(num_chips == 2) {
            status = memx_set_feature(device_id, 0, OPCODE_SET_FREQUENCY, c2_freq);
            status = memx_set_feature(device_id, 1, OPCODE_SET_FREQUENCY, c2_freq);
            status = memx_set_feature(device_id, 0, OPCODE_SET_VOLTAGE,   c2_volt);
            devinfos->at(device_id).volt = c2_volt;
            devinfos->at(device_id).freqs[0] = c2_freq;
            devinfos->at(device_id).freqs[1] = c2_freq;
        }
        else if(num_chips >= 4) {
            // all num_chips >= 4 use the c4 values
            for(int i = 0; i < num_chips; i++) {
                status = memx_set_feature(device_id, i, OPCODE_SET_FREQUENCY, c4_freq);
                devinfos->at(device_id).freqs[i] = c4_freq;
            }
            status = memx_set_feature(device_id, 0, OPCODE_SET_VOLTAGE, c4_volt);
            devinfos->at(device_id).volt = c4_volt;
        }
        else {
            spdlog::error("DFPExecutor {}: num_chips {} is invalid for set_power_mode()", device_id, num_chips);
            return false;
        }
    }
    else {
        MX::Types::MxVoltageOption volt = MX::Types::getVoltageFromFrequency(fop);

        // set all chips to the same frequency that's passed in fop
        for(int i = 0; i < num_chips; i++) {
            status = memx_set_feature(device_id, i, OPCODE_SET_FREQUENCY, fop);
            devinfos->at(device_id).freqs[i] = fop;
        }
        status = memx_set_feature(device_id, 0, OPCODE_SET_VOLTAGE, volt);
        devinfos->at(device_id).volt = volt;
    }

    return memx_status_no_error(status);
}



//---------------------------------------------------------
//---------------------------------------------------------


bool DFPExecutor::close_device()
{

    stop_hw_monitor();

    std::unique_lock<std::mutex> dcslock(m_driver_ctx_set);
    for(uint8_t driver_ctx_id : driver_ctx_set) {
        memx_status status = memx_set_abort_read(driver_ctx_id);
        if(memx_status_error(status)) {
            spdlog::error("DFPExecutor {}: memx_set_abort_read() failed with error {} for driver ctx {}", device_id, (uint32_t)status, driver_ctx_id);
            return false;
        }

        // close the device for each driver context
        status = memx_close(driver_ctx_id);
        if(memx_status_error(status)) {
            spdlog::critical("DFPExecutor {}: memx_close() failed with error {} for driver ctx {}", device_id, (uint32_t)status, driver_ctx_id);
            return false;
        }
        spdlog::debug("DFPExecutor {}: closed driver context {}", device_id, driver_ctx_id);
    }

    driver_ctx_set.clear();
    dcslock.unlock();

    _avg_temps.clear();
    _avg_power = 0.0f;
    _avg_pressure = 0.0f;
    power_window.clear();
    pressure_window.clear();
    for(uint8_t i = 0; i < temp_window.size(); i++) {
        temp_window[i].clear();
    }
    temp_window.clear();

    spdlog::debug("DFPExecutor {}: closed all driver contexts and cleared temp/power data", device_id);
    return true;
}


bool DFPExecutor::close_ctx(uint8_t driver_ctx_id)
{

    // check if this ctx is open
    std::unique_lock<std::mutex> dcslock(m_driver_ctx_set);
    if(driver_ctx_set.count(driver_ctx_id) == 0) {
        spdlog::warn("DFPExecutor {}: close_ctx() called for driver context {} but it is not open", device_id, driver_ctx_id);
        return true; // nothing to close
    }

    memx_status status = memx_close(driver_ctx_id);
    if(memx_status_error(status)) {
        spdlog::critical("DFPExecutor {}: memx_close() failed with error {} for driver ctx {}", device_id, (uint32_t)status, driver_ctx_id);
        return false;
    }

    driver_ctx_set.erase(driver_ctx_id);
    spdlog::debug("DFPExecutor {}: closed driver context {}", device_id, driver_ctx_id);
    return true;
}


bool DFPExecutor::run_autoclock(ExecutorTask* task)
{
    if(task->autoclock_done) {
        return true;
    }

    // this driver_ctx_id can NOT be from my actual, eventual
    // driver_ctx_id -- use a new, temporary one from the global
    // DriverIDTracker object

    uint8_t driver_ctx_id = task->dfp_ctx->device2context_table[device_id];
    std::unique_lock<std::mutex> dcslock(m_driver_ctx_set);

    // cannot already be open!
    if(driver_ctx_set.count(driver_ctx_id) > 0) {
        spdlog::error("DFPExecutor {}: run_autoclock() called but driver context {} is already open", device_id, driver_ctx_id);
        return false;
    }

    // stop hw_monitor if running
    stop_hw_monitor();

    // upclocker object
    AutoClocker upclocker;
    MxFrequencyOption best = upclocker.run(device_id, task->dfp_ctx->dfp_obj, task->power_limit_mw,
                                           driver_ctx_id, task->autoclock_sample_interval_ms, task->autoclock_num_samples);

    // got a valid max freq
    if(best != MX::Types::MxFrequencyOption::MX_FREQUENCY_OPTION_INVALID) {
        spdlog::info("DFPExecutor {}: AutoClocker selected best frequency option {}", device_id,
                     MX::Types::mxFrequencyOptionToString(best));
        task->autoclock_done = true;
        task->freq_option = best;
        ct_autoclock_enabled = true;
        ct_power_limit_mw = task->power_limit_mw;
        ct_original_freq_option = task->freq_option;
        ct_freq_option_ptr = &(task->freq_option);
        dcslock.unlock();
        return true;
    }
    else {
        spdlog::error("DFPExecutor {}: AutoClocker failed to select a best frequency option", device_id);
        task->autoclock_done = true;
        dcslock.unlock();
        return false;
    }

}


bool DFPExecutor::open_device(DFPContext* d)
{
    uint8_t driver_ctx_id = d->device2context_table[device_id];
    std::unique_lock<std::mutex> dcslock(m_driver_ctx_set);
    // skip if already open
    if(driver_ctx_set.count(driver_ctx_id) > 0) {
        spdlog::debug("DFPExecutor {}: open_device() called but driver context {} is already open", device_id, driver_ctx_id);
        return true;
    }

    if(devinfos->at(device_id).is_usb == false){
        // set the dma_method for all chips on this device before calling open
        for(int chip_idx = 0; chip_idx < devinfos->at(device_id).chip_count; chip_idx++) {
            memx_status status = memx_set_feature(device_id, chip_idx, OPCDOE_SET_DEVICE_DMA_TRIGGER_TYPE, MEMX_CHIP_INPUT_DMA_TRIGGER_TYPE_HOST);
            if(memx_status_error(status)) {
                spdlog::error("DFPExecutor {}: memx_set_feature() failed with error {} for chip {} to set DMA trigger type to DMA_TRIGGER_TYPE_HOST",
                              device_id, (uint32_t)status, chip_idx);
                return false;
            }
        }
    }

    memx_status status = memx_open(driver_ctx_id, device_id, MEMX_DEVICE_CASCADE_PLUS);
    if(memx_status_error(status)) {
        spdlog::critical("DFPExecutor {}: memx_open() failed with error {} for driver ctx {}", device_id, (uint32_t)status, driver_ctx_id);
        return false;
    }

    start_hw_monitor(); // just returns if already running

    driver_ctx_set.insert(driver_ctx_id);
    spdlog::debug("DFPExecutor {}: opened driver context {}", device_id, driver_ctx_id);
    return true;
}

bool DFPExecutor::download_and_start_dfp(DFPContext* d)
{
    if(UNLIKELY(d == nullptr)) {
        spdlog::critical("DFPExecutor {}: download_and_start_dfp() called with nullptr DFPContext", device_id);
        return false;
    }

    uint8_t driver_ctx_id = d->device2context_table[device_id];
    std::unique_lock<std::mutex> dcslock(m_driver_ctx_set);
    // check that this ctx id is open
    bool isnt_open = (driver_ctx_set.count(driver_ctx_id) == 0);
    dcslock.unlock();
    if(isnt_open) {
        // not yet, so open it now
        if(open_device(d) == false) {
            spdlog::error("DFPExecutor {}: download_and_start_dfp() failed to open device for driver context {}", device_id, driver_ctx_id);
            return false;
        }
    }

    // set the power mode if next_freq_option differs from current_freq_option
    {
        std::lock_guard<std::mutex> lock(set_freq_mutex);
        if(next_freq_option != current_freq_option) {
            if(set_power_mode(next_freq_option)) {
                spdlog::info("DFPExecutor {}: changed power mode from {} to {}", device_id,
                             MX::Types::mxFrequencyOptionToString(current_freq_option),
                             MX::Types::mxFrequencyOptionToString(next_freq_option));
                current_freq_option = next_freq_option;
            }
            else {
                spdlog::error("DFPExecutor {}: failed to set power mode to {}", device_id,
                              MX::Types::mxFrequencyOptionToString(next_freq_option));
            }
        }
    }

    // print all the members of the DfpContext from d->dfp_obj->get_cache()
    Dfp::pDfpContext cache = d->dfp_obj->get_cache();
    if(UNLIKELY(cache == nullptr)) {
        spdlog::critical("DFPExecutor {}: download_and_start_dfp() called with nullptr DFPContext cache", device_id);
        return false;
    }

    // do the above printing, but with spdlog::debug
    spdlog::debug("DFPExecutor {}: DFPContext cache:", device_id);
    spdlog::debug("  identifier_data: {}", cache->identifier_data);
    spdlog::debug("  dfp_attr: {}", cache->dfp_attr);
    spdlog::debug("  input_mode_flag: {}", cache->input_mode_flag);
    spdlog::debug("  input_port_number: {}", cache->input_port_number);
    spdlog::debug("  output_port_number: {}", cache->output_port_number);
    spdlog::debug("  weight_size: {}", cache->weight_size);
    spdlog::debug("  config_size: {}", cache->config_size);
    spdlog::debug("  pInputConfigList: {}", static_cast<void*>(cache->pInputConfigList));
    spdlog::debug("  pOuputConfigList: {}", static_cast<void*>(cache->pOuputConfigList));
    spdlog::debug("  pWeightBaseAdr: {}", static_cast<void*>(cache->pWeightBaseAdr));
    spdlog::debug("  pRgCfgBaseAdr: {}", static_cast<void*>(cache->pRgCfgBaseAdr));
    spdlog::debug("  DFPExecutor {}: iport_size[0]: {}", device_id, d->info->port_info[0]->iport_sizes[0]);
    spdlog::debug("  DFPExecutor {}: oport_size[0]: {}", device_id, d->info->port_info[0]->oport_sizes[0]);

    // config the dumpster
    {
        std::lock_guard<std::mutex> lock(m_dumpster_lock);
        if(dumpster == nullptr || dumpster_size < d->info->biggest_ofmap_bytes) {
            // delete old dumpster if it exists
            if(dumpster != nullptr) {
                delete [] dumpster;
            }
            // allocate new dumpster
            dumpster_size = d->info->biggest_ofmap_bytes;
            dumpster = new uint8_t[dumpster_size];
        }
    }

    // look at the dfp num_chips and set the mpu group config accordingly *if* the chip_count < num_chips
    // make sure the num_chips is supported (1,2,3,4,8,12,16)

    // cannot support dfp num_chips > device chip_count
    if(UNLIKELY(d->info->num_chips > devinfos->at(device_id).chip_count)) {
        spdlog::error("DFPExecutor {}: DFP num_chips {} is greater than device chip_count {} for driver ctx {}",
                         device_id, d->info->num_chips, devinfos->at(device_id).chip_count, driver_ctx_id);

        // close the device_ctx_id if open
        close_ctx(driver_ctx_id);

        return false;
    }
    else {

        // else <=, so set the mpu group config accordingly
        if(UNLIKELY(d->info->num_chips == 1)) {
            memx_status s = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_ONE_MPU);
            if(UNLIKELY(memx_status_error(s))) {
                spdlog::critical("DFPExecutor {}: memx_config_mpu_group() failed with error {} for device_id {} / driver ctx {}",
                                 device_id, (uint32_t)s, device_id, driver_ctx_id);
                close_ctx(driver_ctx_id);
                return false;
            }
        }
        else if(d->info->num_chips == 2) {
            memx_status s = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_TWO_MPUS);
            if(UNLIKELY(memx_status_error(s))) {
                spdlog::critical("DFPExecutor {}: memx_config_mpu_group() failed with error {} for device_id {} / driver ctx {}",
                                 device_id, (uint32_t)s, device_id, driver_ctx_id);
                close_ctx(driver_ctx_id);
                return false;
            }
        }
        else if(UNLIKELY(d->info->num_chips == 3)) {
            memx_status s = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_THREE_MPUS);
            if(UNLIKELY(memx_status_error(s))) {
                spdlog::critical("DFPExecutor {}: memx_config_mpu_group() failed with error {} for device_id {} / driver ctx {}",
                                 device_id, (uint32_t)s, device_id, driver_ctx_id);
                close_ctx(driver_ctx_id);
                return false;
            }
        }
        else if(LIKELY(d->info->num_chips == 4)) {
            memx_status s = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_FOUR_MPUS);
            if(UNLIKELY(memx_status_error(s))) {
                spdlog::critical("DFPExecutor {}: memx_config_mpu_group() failed with error {} for device_id {} / driver ctx {}",
                                 device_id, (uint32_t)s, device_id, driver_ctx_id);
                close_ctx(driver_ctx_id);
                return false;
            }
        }
        else if(d->info->num_chips == 8) {
            memx_status s = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_EIGHT_MPUS);
            if(UNLIKELY(memx_status_error(s))) {
                spdlog::critical("DFPExecutor {}: memx_config_mpu_group() failed with error {} for device_id {} / driver ctx {}",
                                 device_id, (uint32_t)s, device_id, driver_ctx_id);
                close_ctx(driver_ctx_id);
                return false;
            }
        }
        else if(d->info->num_chips == 12) {
            memx_status s = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_TWELVE_MPUS);
            if(UNLIKELY(memx_status_error(s))) {
                spdlog::critical("DFPExecutor {}: memx_config_mpu_group() failed with error {} for device_id {} / driver ctx {}",
                                 device_id, (uint32_t)s, device_id, driver_ctx_id);
                close_ctx(driver_ctx_id);
                return false;
            }
        }
        else if(d->info->num_chips == 16) {
            memx_status s = memx_config_mpu_group(device_id, MEMX_MPU_GROUP_CONFIG_ONE_GROUP_SIXTEEN_MPUS);
            if(UNLIKELY(memx_status_error(s))) {
                spdlog::critical("DFPExecutor {}: memx_config_mpu_group() failed with error {} for device_id {} / driver ctx {}",
                                 device_id, (uint32_t)s, device_id, driver_ctx_id);
                close_ctx(driver_ctx_id);
                return false;
            }
        }
        else {
            spdlog::error("DFPExecutor {}: unsupported num_chips {} for memx_config_mpu_group() for driver ctx {}",
                          device_id, d->info->num_chips, driver_ctx_id);
            close_ctx(driver_ctx_id);
            return false;
        }
    } // dfp num_chips <= device chip_count

    // download the DFP to the device
    memx_status status = memx_download_model_from_cahce(driver_ctx_id, d->dfp_obj->get_cache(), 0,
                         MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL);
    if(UNLIKELY(memx_status_error(status))) {
        spdlog::critical("DFPExecutor {}: memx_download_model_from_cache() failed with error {} for driver ctx {}",
                         device_id, (uint32_t)status, driver_ctx_id);
        // close the device_ctx_id if open
        close_ctx(driver_ctx_id);
        return false;
    }

    status = memx_set_stream_enable(driver_ctx_id, 0);
    if(UNLIKELY(memx_status_error(status))) {
        spdlog::critical("DFPExecutor {}: memx_set_stream_enable() failed with error {} for driver ctx {}",
                         device_id, (uint32_t)status, driver_ctx_id);
        // close the device_ctx_id if open
        close_ctx(driver_ctx_id);
        return false;
    }

    spdlog::debug("DFPExecutor {}: started DFP with hash {}", device_id, MX::sha512::to_base64(d->hash).c_str());
    //d->print_clients();
    return true;
}


bool DFPExecutor::stop_dfp(DFPContext* d)
{

    uint8_t driver_ctx_id = d->device2context_table[device_id];
    std::unique_lock<std::mutex> dcslock(m_driver_ctx_set);
    if(UNLIKELY(driver_ctx_set.count(driver_ctx_id) == 0)) {
        // not open, so nothing to stop
        spdlog::warn("DFPExecutor {}: stop_dfp() called but driver context {} is not open", device_id, driver_ctx_id);
        return true;
    }

    // NOTE:
    // We have to set `wait` to one to ensure the function waits for `ifmap` and `ofmap` to complete.
    // Setting `wait` to zero returns immediately, which may cause a hang in the next round of download_dfp
    // if `ifmap` or `ofmap` are still processing from the previous operation.
    int wait = 1;
    memx_status status = memx_set_stream_disable(driver_ctx_id, wait);
    if(UNLIKELY(memx_status_error(status))) {
        spdlog::error("DFPExecutor {}: memx_set_stream_disable() failed with error {}", device_id, (uint32_t) status);
        return false;
    }
    spdlog::debug("DFPExecutor {}: stopped DFP", device_id);

    // clear upclocking stuff too
    ct_autoclock_enabled = false;
    ct_power_limit_mw = 15000;
    ct_freq_option_ptr = nullptr;
    ct_original_freq_option = MX::Types::MxFrequencyOption::FREQ_USE_CONF;

    return true;
}


void DFPExecutor::interrupt_task()
{
    // calls soft halt() on all modelthreadpairs
    for(int i = 0; i < n_models; i++) {
        ModelThreadPair* p = thread_pairs.at(i);
        if(p != nullptr) {
            p->halt();
        }
    }
}


bool DFPExecutor::run_dfp(ExecutorTask* task)
{

    DFPContext* d = task->dfp_ctx;

    // if this device's num_chips < the d->info->num_chips, error
    if(UNLIKELY(num_chips < d->info->num_chips)) {
        spdlog::error("DFPExecutor {}: run_dfp() device num_chips {} is less than DFPContext num_chips {}",
                      device_id, num_chips, d->info->num_chips);
        return false;
    }

    n_models = d->info->num_models;
    uint8_t driver_ctx_id = d->device2context_table[device_id];

    spdlog::debug("DFPExecutor {}: running DFP with hash {} for {} frames, {} ms timeout",
                  device_id, MX::sha512::to_base64(d->hash).c_str(), task->frame_limit, task->time_limit);

    // do auto upclock if not already done
    if(can_get_power && task->autoclock_enabled && (task->autoclock_done == false)) {
        if(run_autoclock(task) == false) {
            spdlog::error("DFPExecutor {}: run_autoclock() failed", device_id);
        }
    }

    // set next_power_mode to task->freq_option
    set_next_power_mode(task->freq_option);
    ct_original_freq_option = task->freq_option;
    ct_autoclock_enabled = task->autoclock_enabled;
    ct_power_limit_mw = task->power_limit_mw;
    ct_freq_option_ptr = &(task->freq_option);

    // download the DFP to the device
    if(download_and_start_dfp(d) == false) {
        spdlog::critical("DFPExecutor {}: failed to download and start DFP", device_id);
        return false;
    }

    for(int i = 0; i < n_models; i++) {
        // add a thread pair for each submodel
        // Note: skips IDs that already exist
        add_iothread_pair(i, &m_dumpster_lock, dumpster);
    }

    // now assign the DFPContext and model id to each thread pair
    for(int i = 0; i < n_models; i++) {

        // get the thread pair
        ModelThreadPair* pair = thread_pairs[i];

        // set the pair's driver_ctx_id
        pair->driver_ctx_id = driver_ctx_id;

        // assign the DFPContext and model id
        if(pair->assign_and_start(task, i) == false) {
            // failed to assign, throw error
            spdlog::error("DFPExecutor {}: failed to assign DFPContext to ModelThreadPair for model {}", device_id, i);
            return false;
        }
    }

    // now wait for all threads to finish
    for(int i = 0; i < n_models; i++) {

        ModelThreadPair* p = thread_pairs[i];

        // wait for input and output loop done
        if(p->num_loop_ready.wait_for_value(0) == false) {
            spdlog::warn("DFPExecutor {}: num_loop_ready.wait_for_value(0) was killed early ModelThreadPair {}", device_id, i);
        }
    }

    spdlog::debug("DFPExecutor {}: all threads finished", device_id);

    // stop the DFP
    if(stop_dfp(d) == false) {
        spdlog::error("DFPExecutor {}: failed to stop DFP", device_id);
        return false;
    }

    spdlog::debug("DFPExecutor {}: stopped DFP with hash {}. Returning true..", device_id, MX::sha512::to_base64(d->hash).c_str());

    n_models = 0;

    // success
    return true;
}

//---------------------------------------------------------

void DFPExecutor::add_iothread_pair(int submodel_id, std::mutex* m_dumpster_lock, uint8_t* &dumpster)
{

    if(thread_pairs.count(submodel_id) > 0) {
        // already exists, no need to add again
        return;
    }

    // create a new ModelThreadPair and assign it to the DFPContext
    ModelThreadPair* pair = new ModelThreadPair(this);
    pair->m_dumpster_lock = m_dumpster_lock;
    pair->dumpster_ptr = &dumpster;

    // add to the map
    thread_pairs[submodel_id] = pair;
}


void DFPExecutor::remove_iothread_pair(int submodel_id)
{

    if(thread_pairs.count(submodel_id) == 0) {
        // doesn't exist, nothing to remove
        return;
    }

    // get the pair
    ModelThreadPair* pair = thread_pairs[submodel_id];

    // delete the pair
    delete pair;

    // remove from the map
    thread_pairs.erase(submodel_id);

}


bool DFPExecutor::has_any_threadpair_hit_frame_limit() const
{
    for(int i = 0; i < n_models; i++) {
        ModelThreadPair* p = thread_pairs.at(i);
        if(p == nullptr) {
            spdlog::warn("DFPExecutor {}: has_any_threadpair_hit_frame_limit() found nullptr ModelThreadPair at index {}", device_id, i);
            return true;
        }
        else {
            if(p->a_input_done == true || p->a_stop_in != ModelThreadPair::SF_RUN) {
                return true;
            }
        }
    }


    return false;
}




// ModelThreadPair
//---------------------------------------------------------
//---------------------------------------------------------
//---------------------------------------------------------

ModelThreadPair::ModelThreadPair(const DFPExecutor* my_dfpexec_) : my_dfpexec(my_dfpexec_)
{
    d = nullptr;
    driver_ctx_id = 0xFF; // invalid

    m_dumpster_lock = nullptr;
    dumpster_ptr = nullptr;

    a_stop_out = SF_RUN;
    a_stop_in = SF_RUN;

    a_input_done = true;
    enter_out_flag = false;
    enter_in_flag = false;
    num_loop_ready = 0;

    inflights = new BQExtFlagX<ContextClient*>(UINT_MAX, &a_input_done, true);

    ithread = new std::thread(&ModelThreadPair::input_loop, this);
    othread = new std::thread(&ModelThreadPair::output_loop, this);
}


ModelThreadPair::~ModelThreadPair()
{

    a_stop_out = SF_TERMINATE;
    a_stop_in = SF_TERMINATE;

    // wait for them to finish
    if(ithread->joinable()) {
        ithread->join();
    }
    if(othread->joinable()) {
        othread->join();
    }

    delete ithread;
    delete othread;

    delete inflights;
}


bool ModelThreadPair::assign_and_start(ExecutorTask* task, int model_id_)
{

    if(a_input_done == false) {
        spdlog::critical("ModelThreadPair {}-{}: input done flag is not set, this should not happen!", driver_ctx_id, model_id_);
        return false;
    }

    // assign this dfp and sub model id
    model_id = model_id_;
    d = task->dfp_ctx;

    frame_limit = task->frame_limit;
    time_limit = task->time_limit;
    a_input_done = false; // reset input done flag

    a_stop_out = SF_RUN;
    a_stop_in = SF_RUN;

    spdlog::debug("ModelThreadPair {}-{}: assigned DFPContext with hash {}", driver_ctx_id, model_id, MX::sha512::to_base64(d->hash).c_str());

    // notify the input/output threads to start running
    {
        std::lock_guard<std::mutex> tlock(m_ready);

        if(num_loop_ready != 0) {
            spdlog::critical("ModelThreadPair {}-{}: num_loop_ready is not 0, this should not happen!", driver_ctx_id, model_id);
            return false;
        }

        num_loop_ready = 2; // both threads are ready
        s_loop_ready.notify_all(); // notify the threads
    }


    return true;
}


void ModelThreadPair::halt()
{
    // set the stop flag to 'soft' halt
    a_stop_out = SF_HALT; // soft halt
    a_stop_in = SF_HALT; // soft halt
    inflights->notify(); // wake up the inflight tracker
}


void ModelThreadPair::force_halt()
{
    // set the stop flag to 'hard' halt
    a_stop_out = SF_FORCE_HALT; // force halt
    a_stop_in = SF_FORCE_HALT; // force halt
    inflights->notify(); // wake up the inflight tracker
}


void ModelThreadPair::input_loop()
{

    for(;;) {

        // wait for both input and output threads to get ready
        std::unique_lock<std::mutex> dlock(m_ready);
        s_loop_ready.wait(dlock, [this] { return (num_loop_ready == 2) || (a_stop_in == SF_TERMINATE); });
        if(num_loop_ready < 2) {
            // if we got here, it means the program is being shutdown
            // by the main thread somehwere, so we should exit this loop
            dlock.unlock();
            spdlog::info("ModelThreadPair {}-{}: input loop exiting", driver_ctx_id, model_id);
            break;
        }

        // if we got here, both threads are ready to run
        dlock.unlock();

        // now we officialy enter the input loop
        enter_in_flag = true;

        // now continuously pull inputs from the ifmap queues
        uint64_t frame = 0;
        uint64_t total_frames = 0;

        ModelContext* mctx = d->get_mctx(model_id);

        IomapItem* item = nullptr;
        port_infos_t* p = d->info->port_info[model_id];

        memx_status status = MEMX_STATUS_OTHERS;

        // no timeouts! only frames!
        if(time_limit == 0 && frame_limit > 0) {

            spdlog::debug("ModelThreadPair {}-{}: input stream loop with frame limit {}", driver_ctx_id, model_id, frame_limit);

            while(frame < frame_limit && a_stop_in == SF_RUN) {

                if(mctx->ifmap_queue->pop_timeout_with_ctxpush(item, 1000, driver_ctx_id) == false) {

                    if (item == nullptr) {
                        // if item is nullptr, it means the queue is empty
                        // This can happen when client only connect_dfp but never send any frames to manager
                        spdlog::debug("ModelThreadPair {}-{}: input loop timed out because queue is empty", driver_ctx_id, model_id);
                        break;
                    }

                    if (mctx->is_client_existed(item->dest_client) == false) {
                        // client no longer exists
                        spdlog::debug("ModelThreadPair {}-{}: input loop timed out because client no longer exists", driver_ctx_id, model_id);
                        break;
                    }

                    // This break condition helps with multi-model DFPs where not all frames go through both
                    // models. Like Mediapipe Hands, Virtual Painter, and Face/Emotion. The issue
                    // was one of the ModelThreadPairs would exit while the other kept going.
                    if(my_dfpexec->has_any_threadpair_hit_frame_limit()) {
                        // if we hit the frame limit, break
                        spdlog::debug("ModelThreadPair {}-{}: input loop timed out WHILE another hit the frame limit", driver_ctx_id, model_id);
                        break;
                    }

                    spdlog::debug("ModelThreadPair {}-{}: input loop timed out, we'll try again. Either because another thread is still running or no pending DFPs in my_dfpexec's queue", driver_ctx_id, model_id);
                    continue;
                }

                for(uint32_t i = 0; i < mctx->num_ifmaps; i++) {
                    // syntax is [driver_ctx_id, port_id, data, timeout]
                    status = memx_stream_ifmap(driver_ctx_id, i + (p->istart_idx), (item->data)[i], 0);
                    if(UNLIKELY(memx_status_error(status))) {
                        break;
                    }
                }
                if(UNLIKELY(memx_status_error(status))) {
                    // error, so break
                    spdlog::critical("ModelThreadPair {}-{}: memx_stream_ifmap() failed with error {}", driver_ctx_id, model_id, (uint32_t) status);
                    item->dest_client->decrement_pending_frames();
                    break;
                }

                // push this frame's destination to the inflight tracker
                inflights->push(item->dest_client);

                // return this IomapItem to the ifmap freelist
                mctx->ifmap_freelist->push(item);

                // cool optimization: only increment frame count if my_dfpexec's queue has other DFPs
                // present that want to run
                if(my_dfpexec->my_exec_queue->size() > 0) {
                    frame++;
                }

                total_frames++;

                //spdlog::debug("ModelThreadPair {}: input loop processed frame {}", model_id, frame);
            } //end while

        }
        else {

            spdlog::debug("ModelThreadPair {}-{}: input stream loop with frame limit {} and time limit {}",
                          driver_ctx_id, model_id, frame_limit, time_limit);

            if(frame_limit == 0) {
                // if frame limit is 0, we will run indefinitely until stopped

                // set a reasonable timeout if time_limit is also 0
                if(time_limit == 0) {
                    time_limit = 1000; // 1 second
                }

                while(a_stop_in == SF_RUN) {

                    // pop with timeout (and force the timeout even if other thread pairs are still running)
                    if(mctx->ifmap_queue->pop_timeout_with_ctxpush(item, time_limit, driver_ctx_id) == false) {
                        spdlog::debug("ModelThreadPair {}-{}: input loop timed out", driver_ctx_id, model_id);
                        break;
                    }

                    for(uint32_t i = 0; i < mctx->num_ifmaps; i++) {
                        // syntax is [driver_ctx_id, port_id, data, timeout]
                        status = memx_stream_ifmap(driver_ctx_id, i + (p->istart_idx), (item->data)[i], time_limit);
                        if(UNLIKELY(memx_status_error(status))) {
                            break;
                        }
                    }
                    if(UNLIKELY(memx_status_error(status))) {
                        // error, so break
                        spdlog::critical("ModelThreadPair {}-{}: memx_stream_ifmap() failed with error {}", driver_ctx_id, model_id, (uint32_t) status);
                        item->dest_client->decrement_pending_frames();
                        break;
                    }

                    // push this frame's destination to the inflight tracker
                    inflights->push(item->dest_client);

                    // return this IomapItem to the ifmap freelist
                    mctx->ifmap_freelist->push(item);

                    total_frames++;

                } //end while
            }
            else {
                while(frame < frame_limit && a_stop_in == SF_RUN) {

                    // pop with timeout
                    if(mctx->ifmap_queue->pop_timeout_with_ctxpush(item, time_limit, driver_ctx_id) == false) {
                        spdlog::debug("ModelThreadPair {}-{}: input loop timed out", driver_ctx_id, model_id);
                        break;
                    }

                    for(uint32_t i = 0; i < mctx->num_ifmaps; i++) {
                        // syntax is [driver_ctx_id, port_id, data, timeout]
                        status = memx_stream_ifmap(driver_ctx_id, i + (p->istart_idx), (item->data)[i], time_limit);
                        if(UNLIKELY(memx_status_error(status))) {
                            break;
                        }
                    }
                    if(UNLIKELY(memx_status_error(status))) {
                        // error, so break
                        spdlog::critical("ModelThreadPair {}-{}: memx_stream_ifmap() failed with error {}", driver_ctx_id, model_id, (uint32_t) status);
                        item->dest_client->decrement_pending_frames();
                        break;
                    }

                    // push this frame's destination to the inflight tracker
                    inflights->push(item->dest_client);

                    // return this IomapItem to the ifmap freelist
                    mctx->ifmap_freelist->push(item);

                    // cool optimization: only increment frame count if my_dfpexec's queue has other DFPs
                    // present that want to run
                    if(my_dfpexec->my_exec_queue->size() > 0) {
                        frame++;
                    }

                    total_frames++;

                } //end while
            } //end if time_limit == 0
        } // end if frame_limit == 0

        a_input_done = true; // reset input done flag
        inflights->notify(); // wake up the inflight tracker

        spdlog::debug("ModelThreadPair {}-{}: input loop finished readout. Final frame count: {}", driver_ctx_id, model_id, total_frames);


        // make sure we already officially entered the output loop
        if(enter_out_flag.wait_for_value(true) == false) {
            spdlog::error("ModelThreadPair {}-{}: input loop failed to enter output loop due to killed enter_out_flag, terminating", driver_ctx_id, model_id);
            num_loop_ready--;
            inflights->notify();
            break;
        }
        enter_out_flag = false;

        spdlog::debug("ModelThreadPair {}-{}: input loop make sure we already officially entered the output loop", driver_ctx_id, model_id);

        // if we got here, we either timed out or hit the frame limit
        num_loop_ready--;

        // check for termination
        if(a_stop_in == SF_TERMINATE) { // terminate
            spdlog::info("ModelThreadPair {}-{}: input loop terminating", driver_ctx_id, model_id);
            inflights->notify(); // wake up the inflight tracker
            break;
        }

        spdlog::debug("ModelThreadPair {}-{}: input loop finished.", driver_ctx_id, model_id);
        inflights->notify(); // wake up the inflight tracker

    }
    // terminated!
}




void ModelThreadPair::output_loop()
{

    IomapItem* dst = nullptr;

    for(;;) {

        // wait for both input and output threads to get ready
        std::unique_lock<std::mutex> dlock(m_ready);
        s_loop_ready.wait(dlock, [this] { return (num_loop_ready == 2) || (a_stop_out == SF_TERMINATE); });
        if(num_loop_ready < 2) {
            // if we got here, it means the program is being shutdown
            // by the main thread somehwere, so we should exit this loop
            dlock.unlock();
            spdlog::info("ModelThreadPair {}-{}: output loop exiting", driver_ctx_id, model_id);
            break;
        }

        // if we got here, both threads are ready to run
        dlock.unlock();

        // now we officialy enter the input loop
        enter_out_flag = true;

        port_infos_t* p = d->info->port_info[model_id];

        uint64_t total_frames = 0;

        // now continuously get outputs from the chip and push to
        // the output queue pointers taken from the inflight tracker
        ContextClient* dest_client;

        ModelContext* mctx = d->get_mctx(model_id);

        spdlog::debug("ModelThreadPair {}-{}: output loop starting readout", driver_ctx_id, model_id);

        memx_status status = MEMX_STATUS_OTHERS;

        // ofmap thread doesn't do timeouts/frame limits, because
        // we don't want to lose any data that the chip has produced
        //          &0x1 = FORCE_HALT or TERMINATE
        while( (a_input_done == false || inflights->size() > 0)
                && (a_stop_out.load() & 0x1) == SF_RUN) {

            // pop the next destination from the inflight tracker
            if(inflights->pop(dest_client)) {
                //spdlog::debug("ModelThreadPair {} O: popped output queue pair from inflights", model_id);

                // get a free destination from the freelist
                if(dest_client->ofmap_freelists->at(driver_ctx_id)->pop(dst) == false) {
                    inflights->push(dest_client); // push it back to inflights

                    // no free destination, so break
                    spdlog::debug("ModelThreadPair {}-{} O: output loop exited before getting a free destination", driver_ctx_id, model_id);
                    break;
                }

                // stream ofmaps from the chip into dst
                for(uint32_t i = 0; i < mctx->num_ofmaps; i++) {

                    // syntax is [driver_ctx_id, port_id, data, timeout]
                    //spdlog::debug("ModelThreadPair {} O: streaming ofmap frame #{} on port {}", model_id, i, i + (p->ostart_idx));
                    status = memx_stream_ofmap(driver_ctx_id, i + (p->ostart_idx), (dst->data)[i], 0);
                    if(UNLIKELY(memx_status_error(status))) {
                        break;
                    }
                }
                if(UNLIKELY(memx_status_error(status))) {
                    // error, so break
                    spdlog::critical("ModelThreadPair {}-{} O: memx_stream_ofmap() failed with error {}", driver_ctx_id, model_id, (uint32_t) status);
                    break;
                }

                total_frames++;

                // push the dst to the output queue
                if(dest_client->ofmap_queues->at(driver_ctx_id)->push_timeout(dst, 500) == false) {
                    dest_client->decrement_pending_frames();

                    // timeout -- break this loop
                    spdlog::error("ModelThreadPair {}-{} O: output loop timed out on output queue push", driver_ctx_id, model_id);
                    break;
                }

                dest_client->decrement_pending_frames();
            }
            else {
                // no more inflight queues, so break
                spdlog::debug("ModelThreadPair {}-{} O: output loop exiting due to being halted by a_input_done", driver_ctx_id, model_id);
                break;
            }
        }

        // do drain_pop on the inflights queue to clear any remaining items
        int drain_count = 0;
        while(inflights->drain_pop(dest_client)) {
            spdlog::debug("ModelThreadPair {}-{} O: draining client id: {}", driver_ctx_id, model_id, dest_client->id);

            // get a free destination from the freelist
            if(dest_client->ofmap_freelists->at(driver_ctx_id)->pop(dst) == false) {
                // no free destination, so break
                spdlog::warn("ModelThreadPair {}-{} O: drain pop cannot get a free destination -- need to use fmap dumpster and drop data!", driver_ctx_id, model_id);
                std::unique_lock<std::mutex> dumplock(*m_dumpster_lock);
                for(uint32_t i = 0; i < mctx->num_ofmaps; i++) {
                    // use the dumpster to fill the data
                    status = memx_stream_ofmap(driver_ctx_id, i + (p->ostart_idx), *dumpster_ptr, 0);
                    if(UNLIKELY(memx_status_error(status))) {
                        break;
                    }
                }
                if(UNLIKELY(memx_status_error(status))) {
                    // error, so break
                    spdlog::critical("ModelThreadPair {}-{} O: memx_stream_ofmap() failed with error {}", driver_ctx_id, model_id, (uint32_t) status);
                    dumplock.unlock();
                    break;
                }
                drain_count++;
                dest_client->decrement_pending_frames();
                dumplock.unlock();
            }
            else {

                for(uint32_t i = 0; i < mctx->num_ofmaps; i++) {
                    //spdlog::debug("ModelThreadPair {} O: draining ofmap frame #{} on port {}", model_id, i, i + (p->ostart_idx));
                    status = memx_stream_ofmap(driver_ctx_id, i + (p->ostart_idx), (dst->data)[i], 0);
                    if(UNLIKELY(memx_status_error(status))) {
                        break;
                    }
                }
                if(UNLIKELY(memx_status_error(status))) {
                    // error, so break
                    spdlog::error("ModelThreadPair {}-{} O: memx_stream_ofmap() failed with error {}", driver_ctx_id, model_id, (uint32_t) status);
                    break;
                }
                drain_count++;

                // push the dst to the output queue
                if(dest_client->ofmap_queues->at(driver_ctx_id)->push_timeout(dst, 500) == false) {
                    spdlog::warn("ModelThreadPair {}-{} O: drain oqueue push timed out -- dropping data", driver_ctx_id, model_id);
                }

                dest_client->decrement_pending_frames();
            }
        }

        // ***********************************************************
        // NOTE:
        // After this line, you should not access dest_client anymore,
        // because the client may already be removed once client's program
        // disconnect and client->client_pending_frame_cnt down to zero!!
        // ***********************************************************

        spdlog::debug("ModelThreadPair {}-{} O: drained {} items from inflights queue", driver_ctx_id, model_id, drain_count);
        total_frames += drain_count;

        spdlog::debug("ModelThreadPair {}-{} O: output loop finished readout. Final frame count: {}", driver_ctx_id, model_id, total_frames);

        // make sure we already officially entered the input loop
        if(enter_in_flag.wait_for_value(true) == false) {
            spdlog::error("ModelThreadPair {}-{} O: output loop failed to enter input loop due to killed enter_in_flag, terminating", driver_ctx_id, model_id);
            num_loop_ready--;
            break;
        }
        enter_in_flag = false;

        spdlog::debug("ModelThreadPair {}-{}: output loop make sure we already officially entered the input loop", driver_ctx_id, model_id);

        num_loop_ready--;

        // first check for termination
        if(a_stop_out == SF_TERMINATE) { // terminate
            spdlog::info("ModelThreadPair {}-{}: output loop terminating", driver_ctx_id, model_id);
            break;
        }


        spdlog::debug("ModelThreadPair {}-{} O: output loop finished.", driver_ctx_id, model_id);

    }

    // terminated!

}
