// Copyright (c) 2025 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef DFP_EXECUTOR_H
#define DFP_EXECUTOR_H

#pragma once
#include <string>
#include <vector>
#include <deque>
#include <stack>
#include <thread>
#include <cstdint>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <mutex>
#include <shared_mutex>

#include "mxasio.hpp"

#include "contexts.h"

#include <memx/accl/messages.h>
#include <memx/accl/utils/auto_clocker.h>
#include <memx/accl/utils/locked_var.h>
#include <memx/accl/utils/mxTypes.h>
#include <memx/accl/utils/blocky_queue.h>


using mxasio::ip::tcp;

using namespace MX::Utils; // BlockyQueue

namespace MX
{
namespace Manager
{

// these tasks get queued up by the Scheduler thread
// into each Executor thread, which then runs them
struct ExecutorTask {
    ExecutorTask() : dfp_ctx(nullptr), allowed_device(-1), frame_limit(0), time_limit(0),
        freq_option(MX::Types::MxFrequencyOption::FREQ_USE_CONF), autoclock_enabled(true),
        autoclock_done(false), autoclock_check_fps_saturation(false), autoclock_sample_interval_ms(50),
        autoclock_num_samples(6), power_limit_mw(11500) {}

    // scheduling items
    DFPContext* dfp_ctx;
    int allowed_device;
    uint64_t frame_limit;
    uint32_t time_limit;

    // autoclock options
    MX::Types::MxFrequencyOption freq_option;
    bool autoclock_enabled;
    LockedVar<bool> autoclock_done;
    bool autoclock_check_fps_saturation;
    unsigned int autoclock_sample_interval_ms;
    unsigned int autoclock_num_samples;
    unsigned int power_limit_mw;
};


// each MXA is like its own "DFP Executor"
//
// How we do schedule which device does which DFP?
//
// - based on the current mix of DFPs, etc., (Scheduler stuff; higher layers)
// - if a given DFP is marked for multiple devices, have
//   *multiple DFPExecutor instances* run on the *same* DFPContext
//   This is safe because lower layers (ifmap/ofmap queus & freelists)
//   are made to support potentially multiple executors accessing them
//   at a time.
//
//
// DFPExecutor should have a pair of I/O threads per each ModelContext
// contained within the DFPContext
//
// Execution happens for the given N number of frames (applies to all subModels)
// or after a given total amount of time ('max latency')
//
//
// Swapping DFPs happens here too -- when the DFPContext is changed, we shouldn't have
// to modify *anything* in our running Model I/O threads, thanks to the wonderful
// layers of abstraction in ModelContext->ClientMeta->buffers/freelists/queue, etc...
// - NOTE: you do need to add/remove I/O thread pairs if the incoming DFP has a
//         different number of sub-Models, though!

class ModelThreadPair; // forward declare

class DFPExecutor
{

  public:
    DFPExecutor(uint8_t device_id_, std::vector<device_info_t>* devinfos_,
                const BlockyQueue<ExecutorTask*>* my_exec_queue_, unsigned int hw_monitor_interval_ms = 500);
    ~DFPExecutor();

    // so that ModelThreadPair can touch DFPExecutor's privates
    friend class ModelThreadPair;

    bool run_dfp(ExecutorTask* task);

    MX::Types::MxFrequencyOption run_autoclock(ExecutorTask* task, int device_id, unsigned int step_time_ms = 200);

    // expand/contract # threads depending on # submodels
    void add_iothread_pair(int submodel_id, std::mutex* m_dumpster_lock, uint8_t* &dumpster);
    void remove_iothread_pair(int submodel_id);

    // download and start stream to real hardware
    bool download_and_start_dfp(DFPContext* d);
    bool stop_dfp(DFPContext* d);

    // close all open contexts for this device
    bool close_device();

    // close only the given driver context ID
    bool close_ctx(uint8_t driver_ctx_id);

    // tracks open driver ctx IDs
    std::unordered_set<uint8_t> driver_ctx_set;

    // get max temps
    float avg_max_temp();
    float inst_max_temp();

    // get temps for each chip on this device (const ref to this->)
    const std::vector<float> &avg_all_temps();
    const std::vector<float>  inst_all_temps();

    // get powers (if possible)
    bool can_get_power;
    float avg_power();
    float inst_power();

    // get utilization (pressure)
    float avg_pressure();

    // get number of chips on this device
    uint8_t get_num_chips();

    // public request to set power mode
    void set_next_power_mode(MX::Types::MxFrequencyOption fop);

  private:
    const uint8_t device_id;
    const BlockyQueue<ExecutorTask*>* my_exec_queue;

    bool open_device(DFPContext* d);
    bool run_autoclock(ExecutorTask* task);
    std::vector<device_info_t>* devinfos;

    LockedVar<int> n_models; // used later!
    bool has_any_threadpair_hit_frame_limit() const;

    uint8_t num_chips; // number of chips on this device (set in open_device())

    std::map<int, ModelThreadPair*> thread_pairs;

    //--------------------------------------------------------------------------------
    std::mutex set_freq_mutex;
    MX::Types::MxFrequencyOption current_freq_option;
    MX::Types::MxFrequencyOption next_freq_option;

    // set the power mode for this device
    bool set_power_mode(MX::Types::MxFrequencyOption fop);

    // for forcibly interrupting the current DFP when power limits get exceeded
    void interrupt_task();

    //--------------------------------------------------------------------------------

    // start/stop thread that monitor hardware temps & powers
    void start_hw_monitor();
    void stop_hw_monitor();

    // hardware monitor thread
    std::thread* hw_monitor_thread;

    // parses power config file
    void read_power_mode();
    uint16_t c4_freq;
    uint16_t c4_volt;
    uint16_t c2_freq;
    uint16_t c2_volt;
    // ct = current task
    LockedVar<bool> ct_autoclock_enabled;
    LockedVar<unsigned int> ct_power_limit_mw;
    LockedVar<MX::Types::MxFrequencyOption*> ct_freq_option_ptr;
    LockedVar<MX::Types::MxFrequencyOption>  ct_original_freq_option;


    // hardware monitor thread's loop function
    void hw_monitor_loop();
    bool hw_monitor_running;
    std::mutex m_hw_monitor_lock;

    // the actual temp and power data retrieved
    // by the public functions above
    std::vector<float> _avg_temps; // average temps per chip
    float              _avg_power; // average power consumption
    float              _avg_pressure; // average pressure

    // the 'instantaneous' temps and power are just the newest values
    // in the deques

    //--------------------------------------------------------------------------------

    // interval to poll temp and power
    const std::chrono::milliseconds hw_monitor_interval;

    // number of samples to average over
    // (time the avg is over is thus hw_monitor_interval * hw_monitor_avg_samples)
    const unsigned int hw_monitor_avg_samples = 8;

    // the complete window of powers so far
    std::deque<float> power_window;

    // the complete window of pressures so far
    std::deque<float> pressure_window;

    // for temps, it's per interval * per chip
    std::vector<std::deque<float>> temp_window;

    mutable std::shared_mutex m_temp_power; // mutex for temp and power data

    //--------------------------------------------------------------------------------

    uint8_t* dumpster; // buffer for dumping overflow frames
    size_t  dumpster_size; // size of the dumpster buffer
    mutable std::mutex m_dumpster_lock; // mutex for changing dumpster size

    //--------------------------------------------------------------------------------

    // need mutex all memx_get_feature() calls because of the driver
    mutable std::shared_mutex m_memx_get_feature;
    mutable std::mutex m_driver_ctx_set;
};




class ModelThreadPair
{
  public:
    ModelThreadPair(const DFPExecutor* my_dfpexec_);
    ~ModelThreadPair();

    friend class DFPExecutor;

    // assigns given DFP
    // start running for N frames / M time
    bool assign_and_start(ExecutorTask* task, int model_id_);

    // halt while draining frame pipeline
    void halt();

    // forcibly halt; do not drain pipeline
    void force_halt();

    // current DFP ptr
    DFPContext* d;

    // publicly accessible "interrupt flags" that
    // the main DFPExecutor thread waits on

    // check out our synchronization mechanism here:
    // https://link.excalidraw.com/l/55syurcaaU1/A46Bz3AK1dp
    std::condition_variable s_loop_ready;
    LockedVar<bool> a_input_done;
    LockedVar<bool> enter_out_flag;
    LockedVar<bool> enter_in_flag;
    LockedVar<int> num_loop_ready;

    std::mutex m_flags;
    std::mutex m_ready;

    uint8_t driver_ctx_id;


    std::mutex* m_dumpster_lock;
    uint8_t** dumpster_ptr;

  private:
    // stored internally
    int model_id;
    uint64_t frame_limit;
    uint32_t time_limit;

    // disabled in SDK 2.1
    const bool stop_on_empty = false;


    // targets of threads
    void input_loop();
    void output_loop();

    const DFPExecutor* my_dfpexec;

    // 3'b000=run, 3'b010=halt, 3'b011=force-halt, 3'b101=terminate
    enum StopFlags : char {
        SF_RUN = 0,
        SF_HALT = 2,
        SF_FORCE_HALT = 3,
        SF_TERMINATE = 5
    };
    LockedVar<StopFlags> a_stop_in;
    LockedVar<StopFlags> a_stop_out;

    std::thread* ithread;
    std::thread* othread;

    // use the X version (the exclusive LockedVar instead of SharedLockedVar)
    // since we will use a_input_done as the external flag
    BQExtFlagX<ContextClient*>* inflights;

};



}
}

#endif // DFP_EXECUTOR_H
