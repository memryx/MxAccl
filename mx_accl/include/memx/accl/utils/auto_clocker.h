// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef AUTO_CLOCKER_H
#define AUTO_CLOCKER_H

#pragma once
#include <memx/accl/dfp.h>
#include <memx/accl/messages.h>
#include <memx/accl/utils/locked_var.h>
#include <memx/accl/utils/mxTypes.h>
#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/blocky_queue.h>

#include <vector>
#include <mutex>
#include <chrono>
#include <thread>
#include <cstdint>
#include <utility> // std::pair

using namespace MX::Types;
using namespace MX::Utils;

// this way it can be used for both local (mx_accl)
// and shared (mxa_manager) uses

class AutoClocker
{
  public:
    AutoClocker();
    ~AutoClocker();

    MxFrequencyOption run(int device_id_, Dfp::DfpObject* dfp_, unsigned int power_limit_mw, int driver_ctx_to_use,
                          unsigned int sample_interval_ms = 50, unsigned int num_samples = 6, bool check_fps_saturation = false);


  private:
    Dfp::DfpObject* dfp;
    int device_id;
    int num_models;
    int driver_ctx_id;
    unsigned int max_allowed_power_mw;

    // only allow 1 run() at a time
    std::mutex m_busy_lock;

    // min time between power samples
    std::chrono::milliseconds power_sample_interval;
    unsigned int num_samples_to_collect;


    // threads that loop over the relevant model's ports
    // NOTE: when using DFPs with multiple models, we will check max power
    //       for each model running alone, then again with all running
    //       at the same time. The maximum power is chosen across
    //       all these runs.
    void input_thread_fn(int model_id);
    void output_thread_fn(int model_id);
    void output_thread_fn_check_saturation(int model_id);

    std::vector<std::thread*> input_threads;
    std::vector<std::thread*> output_threads;

    // for each model thread pair
    std::vector<LockedVar<bool>>  model_threadpair_run; // toggled often while upclocking
    std::vector<LockedVar<bool>>  model_threadpair_kill; // set true only when totally finished
    std::vector<LockedVar<int>>   model_num_inflights; // number of inflight frames for this model
    std::vector<LockedVar<float>> model_avg_f2f_time_ms; // avg frame-to-frame latency for this model

    // feature maps for each model
    // [model_id][relative_port_id][<featuremap*, hw_port_id>]
    std::vector< std::vector<std::pair<FeatureMap*, int>>> ifmaps;
    std::vector< std::vector<std::pair<FeatureMap*, int>>> ofmaps;

    // allocate all the fmaps
    bool alloc_fmaps();

    // clear all fmaps
    void clear_fmaps();

    // close and clean up everything (like dtor)
    void clean_all();

    // set chip freq
    void set_chip_frequency(MxFrequencyOption freq);
    int device_chip_cnt;

    // power monitoring loop happens in main run_upclock() function
    // will have a max_power per model combo run and use max

};



#endif // AUTO_CLOCKER_H
