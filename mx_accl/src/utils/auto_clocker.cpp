// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <vector>
#include <chrono>
#include <thread>
#include <cstdint>
#include <utility>
#include <iostream>
#include <memx/memx.h>
#include <memx/accl/dfp.h>
#include <memx/accl/utils/mxTypes.h>
#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/locked_var.h>
#include <memx/accl/utils/auto_clocker.h>

#include "spdlog/spdlog.h"

using namespace MX::Types;
using namespace MX::Utils;


// basic ctor
AutoClocker::AutoClocker()
{
    dfp = nullptr;
    device_id = -1;
    num_models = 0;
    power_sample_interval = std::chrono::milliseconds(50);
    num_samples_to_collect = 5;
    driver_ctx_id = 0;
}

// dtor also joins threads and clears any allocated FeatureMap* from vectors
AutoClocker::~AutoClocker()
{
    std::lock_guard<std::mutex> lock(m_busy_lock);
    clean_all();
}


// for causing cleanup without destroying the AutoClocker object
void AutoClocker::clean_all()
{
    // kill all lockedvar items
    for(int i = 0; i < num_models; i++) {
        model_threadpair_run[i] = false;
    }
    for(int i = 0; i < num_models; i++) {
        model_threadpair_kill[i] = true;
    }

    // join input threads
    for(auto &t : input_threads) {
        if(t != nullptr && t->joinable()) {
            t->join();
            delete t;
            t = nullptr;
        }
    }
    input_threads.clear();

    // join output threads
    for(auto &t : output_threads) {
        if(t != nullptr && t->joinable()) {
            t->join();
            delete t;
            t = nullptr;
        }
    }
    output_threads.clear();

    // clear fmaps
    clear_fmaps();

    // clear lockedvars
    model_threadpair_kill.clear();
    model_threadpair_run.clear();
    model_num_inflights.clear();

    // reset dfp and device_id
    dfp = nullptr;
    device_id = -1;
    num_models = 0;
    driver_ctx_id = 0;
}

// wipes allocated FeatureMap* from ifmaps and ofmaps
void AutoClocker::clear_fmaps()
{
    // for each item in ifmaps and ofmaps, delete the FeatureMap* part of the pair
    for(auto &vec : ifmaps) {
        for(auto &fm : vec) {
            delete fm.first;
            fm.first = nullptr;
        }
        vec.clear();
    }
    ifmaps.clear();

    for(auto &vec : ofmaps) {
        for(auto &fm : vec) {
            delete fm.first;
            fm.first = nullptr;
        }
        vec.clear();
    }
    ofmaps.clear();
}


// allocate all the fmaps based on port info from dfp
bool AutoClocker::alloc_fmaps()
{
    if(dfp == nullptr) {
        spdlog::error("[AutoClocker {}] alloc_fmaps() called but dfp is nullptr", device_id);
        return false;
    }

    num_models = dfp->get_dfp_meta()->num_models;

    // for each model
    for(int m = 0; m < num_models; m++) {

        std::vector< std::pair<FeatureMap*, int>> model_ifmaps;
        std::vector< std::pair<FeatureMap*, int>> model_ofmaps;

        // input ports
        const auto &inports = dfp->get_dfp_meta()->model_inports[m];
        for(const auto &port_idx : inports) {
            const auto* p = dfp->input_port(port_idx);
            FeatureMap* fm = new FeatureMap(p->total_size,
                                            (MX_data_format)p->format,
                                            p->dim_h,
                                            p->dim_w,
                                            p->dim_z,
                                            p->dim_c);
            fm->set_random_data(); // fill with random data
            model_ifmaps.push_back(std::make_pair(fm, port_idx));
            spdlog::debug("[AutoClocker {}] alloc_fmaps() model {} input port {}: allocated FeatureMap of size {}", device_id, m, port_idx, p->total_size);
        }
        ifmaps.push_back(model_ifmaps);

        // output ports
        const auto &outports = dfp->get_dfp_meta()->model_outports[m];
        for(const auto &port_idx : outports) {
            const auto* p = dfp->output_port(port_idx);
            FeatureMap* fm = new FeatureMap(p->total_size,
                                            (MX_data_format)p->format,
                                            p->dim_h,
                                            p->dim_w,
                                            p->dim_z,
                                            p->dim_c);
            model_ofmaps.push_back(std::make_pair(fm, port_idx));
            spdlog::debug("[AutoClocker {}] alloc_fmaps() model {} output port {}: allocated FeatureMap of size {}", device_id, m, port_idx, p->total_size);
        }
        ofmaps.push_back(model_ofmaps);
    }

    return true;
}


//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------


void AutoClocker::input_thread_fn(int model_id)
{
    while(model_threadpair_kill[model_id] == false) {

        while(model_threadpair_run[model_id] == true) {

            // keep sending all the fmaps for this model
            for(const auto &fm_pair : ifmaps[model_id]) {
                FeatureMap* fm = fm_pair.first;
                int hw_port_id = fm_pair.second;

                memx_status status = memx_stream_ifmap(driver_ctx_id,
                                                       hw_port_id,
                                                       fm->get_formatted_data(),
                                                       1000);
                if(memx_status_error(status)) {
                    spdlog::error("[AutoClocker {}] input_thread_fn model {}: memx_stream_ifmap() failed with error {}", device_id, model_id, (uint32_t) status);
                }
            }

            model_num_inflights[model_id] += 1;
        }

        std::this_thread::yield();
    }

    spdlog::debug("[AutoClocker {}] input_thread_fn model {}: terminating input thread", device_id, model_id);
}


void AutoClocker::output_thread_fn_check_saturation(int model_id)
{
    uint64_t frame_count = 0;
    std::chrono::high_resolution_clock::time_point last_frame_time;
    model_avg_f2f_time_ms[model_id] = -1.0f; // invalid
    while(model_threadpair_kill[model_id] == false) {

        frame_count = 0;

        while(model_threadpair_run[model_id] == true || model_num_inflights[model_id] > 0) {

            if(model_num_inflights[model_id] > 0) {

                // keep reading all the fmaps for this model
                for(const auto &fm_pair : ofmaps[model_id]) {
                    FeatureMap* fm = fm_pair.first;
                    int hw_port_id = fm_pair.second;

                    memx_status status = memx_stream_ofmap(driver_ctx_id,
                                                           hw_port_id,
                                                           fm->get_formatted_data(),
                                                           1000);
                    if(memx_status_error(status)) {
                        spdlog::error("[AutoClocker {}] output_thread_fn_check_saturation model {}: memx_stream_ofmap() failed with error {}", device_id, model_id, (uint32_t) status);
                    }
                }

                model_num_inflights[model_id] -= 1;
                frame_count += 1;
                if(frame_count == 1) {
                    // first frame, set last_frame_time and init avg to 0
                    last_frame_time = std::chrono::high_resolution_clock::now();
                    model_avg_f2f_time_ms[model_id] = 0.0f;
                }
                else {
                    // a later frame, compute time since last frame and add to avg
                    auto now = std::chrono::high_resolution_clock::now();
                    float frame_time_ms = (float)(std::chrono::duration_cast<std::chrono::microseconds>(now - last_frame_time).count()) / 1000.0f;

                    last_frame_time = now;
                    // exponential moving average with alpha = 0.1
                    model_avg_f2f_time_ms[model_id] = (0.9f * model_avg_f2f_time_ms[model_id].load()) + (0.1f * frame_time_ms);
                }
            }
            else {
                // no inflights, so just yield
                // yes, this can lead to high CPU from busy-waiting but we don't care
                std::this_thread::yield();
            }
        }

        std::this_thread::yield();
    }

    spdlog::debug("[AutoClocker {}] output_thread_fn_check_saturation model {}: terminating output thread", device_id, model_id);
}

void AutoClocker::output_thread_fn(int model_id)
{
    while(model_threadpair_kill[model_id] == false) {

        while(model_threadpair_run[model_id] == true || model_num_inflights[model_id] > 0) {

            if(model_num_inflights[model_id] > 0) {

                // keep reading all the fmaps for this model
                for(const auto &fm_pair : ofmaps[model_id]) {
                    FeatureMap* fm = fm_pair.first;
                    int hw_port_id = fm_pair.second;

                    memx_status status = memx_stream_ofmap(driver_ctx_id,
                                                           hw_port_id,
                                                           fm->get_formatted_data(),
                                                           1000);
                    if(memx_status_error(status)) {
                        spdlog::error("[AutoClocker {}] output_thread_fn model {}: memx_stream_ofmap() failed with error {}", device_id, model_id, (uint32_t) status);
                    }
                }

                model_num_inflights[model_id] -= 1;
            }
            else {
                // no inflights, so just yield
                // yes, this can lead to high CPU from busy-waiting but we don't care
                std::this_thread::yield();
            }
        }

        std::this_thread::yield();
    }

    spdlog::debug("[AutoClocker {}] output_thread_fn model {}: terminating output thread", device_id, model_id);
}



//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

void AutoClocker::set_chip_frequency(MxFrequencyOption freq)
{
    MxVoltageOption volt = getVoltageFromFrequency(freq);

    for(int i = 0; i < device_chip_cnt; i++) {
        memx_status status = memx_set_feature(device_id, i, OPCODE_SET_FREQUENCY, freq);
        if(memx_status_error(status)) {
            spdlog::error("[AutoClocker {}] set_chip_frequency(): memx_set_feature(OPCODE_SET_FREQUENCY) failed on chip {} with error {}", device_id, i, (uint32_t) status);
            return;
        }
    }
    memx_status status = memx_set_feature(device_id, 0, OPCODE_SET_VOLTAGE, volt);
    if(memx_status_error(status)) {
        spdlog::error("[AutoClocker {}] set_chip_frequency(): memx_set_feature(OPCODE_SET_VOLTAGE) failed with error {}", device_id, (uint32_t) status);
        return;
    }

}


//---------------------------------------------------------------------------------------

MxFrequencyOption AutoClocker::run(int device_id_, Dfp::DfpObject* dfp_, unsigned int power_limit_mw, int driver_ctx_to_use,
        unsigned int sample_interval_ms, unsigned int num_samples, bool check_fps_saturation)
{

    std::lock_guard<std::mutex> lock(m_busy_lock);

    // set parameters
    dfp = dfp_;
    device_id = device_id_;
    driver_ctx_id = driver_ctx_to_use;
    max_allowed_power_mw = power_limit_mw;
    power_sample_interval = std::chrono::milliseconds(sample_interval_ms);
    num_samples_to_collect = num_samples;

    // allocate fmaps
    spdlog::debug("[AutoClocker {}] run() allocating feature maps", device_id);
    if(alloc_fmaps() == false) {
        spdlog::error("[AutoClocker {}] run() alloc_fmaps() failed", device_id);
        return MX_FREQUENCY_OPTION_INVALID;
    }

    // create lockedvars
    model_threadpair_run.resize(num_models);
    model_threadpair_kill.resize(num_models);
    model_num_inflights.resize(num_models);
    model_avg_f2f_time_ms.resize(num_models);

    // create and start input and output threads
    for(int m = 0; m < num_models; m++) {
        spdlog::debug("[AutoClocker {}] run() creating threads for model {}", device_id, m);
        model_threadpair_run[m] = false;
        model_threadpair_kill[m] = false;
        model_num_inflights[m] = 0;

        // output thread
        if(check_fps_saturation) {
            std::thread* t_out = new std::thread(&AutoClocker::output_thread_fn_check_saturation, this, m);
            output_threads.push_back(t_out);
        }
        else {
            std::thread* t_out = new std::thread(&AutoClocker::output_thread_fn, this, m);
            output_threads.push_back(t_out);
        }

        // input thread
        std::thread* t_in = new std::thread(&AutoClocker::input_thread_fn, this, m);
        input_threads.push_back(t_in);

    }

    // create vector of uint64_t to hold max power so far, per model combo
    int num_runs = num_models > 1 ? num_models + 1 : 1;

    // for tracking if fps is still improving or has it saturated
    std::vector<float> run_fps(num_runs);
    std::vector<float> prev_run_fps(num_runs, 0.0f);
    std::vector<bool> last_3_runs_improved(3, true);

    memx_status status;

    // memx_open, download DFP, and set_stream_enable
    spdlog::debug("[AutoClocker {}] run() memx_open(), memx_download_dfp(), memx_set_stream_enable()", device_id);
    status = memx_open(driver_ctx_id, device_id, MEMX_DEVICE_CASCADE_PLUS);
    if(memx_status_error(status)) {
        spdlog::error("[AutoClocker {}] run() memx_open() failed with error {}", device_id, (uint32_t) status);
        clean_all();
        return MX_FREQUENCY_OPTION_INVALID;
    }
    spdlog::debug("[AutoClocker {}] run() memx_open() succeeded on driver_ctx_id {}", device_id, driver_ctx_id);

    device_chip_cnt = 0;
    uint64_t hwinfo64 = 0;
    status = memx_get_feature(device_id, 0, OPCODE_GET_HW_INFO, &hwinfo64);
    if(memx_status_error(status)) {
        spdlog::error("[AutoClocker {}] run() memx_get_feature(OPCODE_GET_HW_INFO) failed with error {}", device_id, (uint32_t) status);
        clean_all();
        memx_close(driver_ctx_id);
        return MX_FREQUENCY_OPTION_INVALID;
    }
    else {
        device_chip_cnt = (hwinfo64 & ((uint64_t)0xFFL << 16)) >> 16;
        spdlog::debug("[AutoClocker {}] run() device_chip_cnt = {}", device_id, device_chip_cnt);
    }

    status = memx_download_model_from_cahce(driver_ctx_id, dfp->get_cache(), 0, MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL);
    if(memx_status_error(status)) {
        spdlog::error("[AutoClocker {}] run() memx_download_dfp() failed with error {}", device_id, (uint32_t) status);
        clean_all();
        memx_close(driver_ctx_id);
        return MX_FREQUENCY_OPTION_INVALID;
    }
    spdlog::debug("[AutoClocker {}] run() memx_download_dfp() succeeded on driver_ctx_id {}", device_id, driver_ctx_id);


    MxFrequencyOption freq = FREQ_250MHz; // starting point


    while(freq <= FREQ_1000MHz) {
        spdlog::debug("[AutoClocker {}] run() starting power measurement run for freq option {}", device_id, mxFrequencyOptionToString(freq));

        set_chip_frequency(freq);

        status = memx_set_stream_enable(driver_ctx_id, 0);
        if(memx_status_error(status)) {
            spdlog::error("[AutoClocker {}] run() memx_set_stream_enable() failed with error {}", device_id, (uint32_t) status);
            clean_all();
            memx_close(driver_ctx_id);
            return MX_FREQUENCY_OPTION_INVALID;
        }
        spdlog::debug("[AutoClocker {}] run() memx_set_stream_enable() succeeded on driver_ctx_id {}", device_id, driver_ctx_id);

        // Start each model thread pair one at a time to get max power per model.
        // While running, use calls to memx_get_feature to get power samples
        // at a rate of power_sample_interval (minimum time between samples, including memx_get_feature time).
        // After num_samples_to_collect samples, stop the model and store the max power seen.
        uint64_t all_runs_max_power = 0;
        for(int run_idx = 0; run_idx < num_runs; run_idx++) {

            // if last run and multiple models, start all models
            if(run_idx == num_models && num_models > 1) {
                spdlog::debug("[AutoClocker {}] Running ALL models together for max power measurement", device_id);
                for(int m = 0; m < num_models; m++) {
                    model_threadpair_run[m] = true;
                }
            }
            else {
                spdlog::debug("[AutoClocker {}] Running model {} for max power measurement", device_id, run_idx);
                model_threadpair_run[run_idx] = true;
            }


            //-----------------------------------------------------------------------------------------
            // collect power samples
            uint64_t local_max_power = 0;
            for(unsigned int sample_idx = 0; sample_idx < num_samples_to_collect; sample_idx++) {

                auto start_time = std::chrono::high_resolution_clock::now();

                uint64_t pvalue = 0;
                memx_status status = memx_get_feature(device_id, 0, OPCODE_GET_POWER, &pvalue);
                if(memx_status_error(status)) {
                    spdlog::error("[AutoClocker {}] Sample {}: memx_get_feature(OPCODE_GET_POWER) failed with error {}", device_id, sample_idx, (uint32_t) status);
                    clean_all();
                    memx_close(driver_ctx_id);
                    return MX_FREQUENCY_OPTION_INVALID;
                }
                else {
                    if(pvalue > local_max_power) {
                        local_max_power = pvalue;
                    }
                    spdlog::debug("[AutoClocker {}] Sample {}: Power = {} mW", device_id, sample_idx, pvalue);
                }

                // wait until power_sample_interval has elapsed
                auto end_time = std::chrono::high_resolution_clock::now();
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                if(elapsed_ms < power_sample_interval) {
                    std::this_thread::sleep_for(power_sample_interval - elapsed_ms);
                }
            }
            //-----------------------------------------------------------------------------------------


            // stop the model(s)
            if(run_idx == num_models && num_models > 1) {
                for(int m = 0; m < num_models; m++) {
                    model_threadpair_run[m] = false;

                    // wait for inflights to drain to 0
                    if(model_num_inflights[m].wait_for_value(0) == false) {
                        spdlog::warn("[AutoClocker {}] run ALL models: failure waiting for model {} inflights to drain", device_id, m);
                    }
                    else {
                        spdlog::debug("[AutoClocker {}] run ALL models: model {} inflights drained to 0", device_id, m);
                    }
                }

                if(check_fps_saturation) {
                    // run_fps is the maximum of all models (minimum of model_avg_f2f_time_ms vector)
                    float min_f2f_time = -1.0f;
                    for(int m = 0; m < num_models; m++) {
                        float f2f_time = model_avg_f2f_time_ms[m];
                        if(f2f_time >= 0.0f) {
                            if(min_f2f_time < 0.0f || f2f_time < min_f2f_time) {
                                min_f2f_time = f2f_time;
                            }
                        }
                    }

                    run_fps[run_idx] = (min_f2f_time > 0.0f) ? (1000.0f / min_f2f_time) : 0.0f;
                }
            }
            else {
                model_threadpair_run[run_idx] = false;
                spdlog::debug("[AutoClocker {}] run model {}: stopping model", device_id, run_idx);

                // wait for inflights to drain to 0
                if(model_num_inflights[run_idx].wait_for_value(0) == false) {
                    spdlog::warn("[AutoClocker {}] run model {}: failure waiting for inflights to drain", device_id, run_idx);
                }
                else {
                    spdlog::debug("[AutoClocker {}] run model {}: inflights drained to 0", device_id, run_idx);
                }

                if(check_fps_saturation) {
                    run_fps[run_idx] = 1000.0f / model_avg_f2f_time_ms[run_idx];
                }
            }


            spdlog::debug("[AutoClocker {}] Run {} finished: Max Power = {} mW", device_id, run_idx, local_max_power);

            // update the all_runs_max_power if greater
            if(local_max_power > all_runs_max_power) {
                all_runs_max_power = local_max_power;
            }

        }

        // set_stream_disable for this driver ctx
        status = memx_set_stream_disable(driver_ctx_id, 1);
        if(memx_status_error(status)) {
            spdlog::error("[AutoClocker {}] run() memx_set_stream_disable() failed with error {}", device_id, (uint32_t) status);
            clean_all();
            memx_close(driver_ctx_id);
            return MX_FREQUENCY_OPTION_INVALID;
        }

        // print the max power
        spdlog::debug("[AutoClocker {}] All runs finished: max power at this frequency = {} mW", device_id, all_runs_max_power);

        // if the power is less than the limit, try next frequency (+25) [only if we've had some fps improvement]
        if((all_runs_max_power < max_allowed_power_mw) && (freq < FREQ_1000MHz)) {

            if(check_fps_saturation) {
                bool fps_improved = false;
                for(int r = 0; r < num_runs; r++) {
                    // if any run improved by at least 1.5%, consider fps improved
                    spdlog::debug("[AutoClocker {}] run {}: previous FPS = {}, current FPS = {}", device_id, r, prev_run_fps[r], run_fps[r]);
                    if(run_fps[r] > (prev_run_fps[r] * 1.015f)) {
                        fps_improved = true;
                        break;
                    }
                }

                last_3_runs_improved[0] = last_3_runs_improved[1];
                last_3_runs_improved[1] = last_3_runs_improved[2];
                last_3_runs_improved[2] = fps_improved;

                // if all last 3 runs did not improve, we have saturated
                if(last_3_runs_improved[0] == false && last_3_runs_improved[1] == false && last_3_runs_improved[2] == false) {

                    // bump back to previous freq since there wasn't improvement
                    freq = (MxFrequencyOption)( (int)freq - 25 );

                    spdlog::debug("[AutoClocker {}] FPS seems to have saturated. Choosing frequency option {}", device_id, mxFrequencyOptionToString(freq));
                    break;
                }
                else {
                    spdlog::debug("[AutoClocker {}] Max power {} mW is below limit {} mW and we saw some FPS improvement, trying next frequency", device_id, all_runs_max_power, max_allowed_power_mw);
                    freq = (MxFrequencyOption)( (int)freq + 25 );
                }
            }
            else {
                spdlog::debug("[AutoClocker {}] Max power {} mW is below limit {} mW, trying next frequency", device_id, all_runs_max_power, max_allowed_power_mw);
                freq = (MxFrequencyOption)( (int)freq + 25 );
            }
        }
        else {
            if(freq == FREQ_1000MHz) {
                spdlog::debug("[AutoClocker {}] Reached maximum frequency option {}, stopping clocking", device_id, mxFrequencyOptionToString(freq));
                break;
            }
            else {
                spdlog::debug("[AutoClocker {}] Max power {} mW exceeds limit {} mW, stopping clocking", device_id, all_runs_max_power, max_allowed_power_mw);
                freq = (MxFrequencyOption)( (int)freq - 25 );
                break;
            }
        }

        if(check_fps_saturation) {
            // update prev_run_fps
            for(int r = 0; r < num_runs; r++) {
                prev_run_fps[r] = run_fps[r];
            }
        }

    }


    // clean up
    int save_device_id = device_id;
    clean_all();
    memx_close(driver_ctx_id);

    spdlog::info("[AutoClocker {}] run() completed, determined max frequency option: {}", save_device_id, mxFrequencyOptionToString(freq));

    return freq;
}
