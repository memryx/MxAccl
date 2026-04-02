// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_MODEL
#define MX_MODEL

#pragma once
#include <memx/accl/client.h>
#include <memx/accl/dfp.h>
#include <memx/accl/prepost.h>
#include <memx/accl/utils/blocky_queue.h>
#include <memx/accl/utils/errors.h>
#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/locked_var.h>
#include <memx/accl/utils/mxTypes.h>
#include <memx/accl/utils/rental_store.h>
#include <memx/accl/utils/threadsafe_map.h>
#include <memx/memx.h>
#include <stdint.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <atomic>

using namespace std;
using namespace MX::Utils;
using namespace MX::Types;

namespace MX
{
namespace Runtime
{

struct StreamTask {
    int id;
    callback_t in_cb;
    callback_t out_cb;
    int order;
    StreamTask(int stream_id, callback_t input_cb, callback_t output_cb) : id(stream_id), in_cb(input_cb), out_cb(output_cb)
    {
    }
};

struct inflightPacket {
    int stream_id = -1;
    int ctx_infer = -1;  // which context for this inflight inference
};

struct IomapItem {

    int stream_id = -1;  // Set when checked out from RentalStore
    int seq_num = -1;    // make sure output are processed in order for each stream

    // Input-side feature maps
    vector<FeatureMap*> ifmaps;              // Non-const fmaps (used by get_formatted_data)
    vector<const FeatureMap*> c_ifmaps;      // Const aliases of ifmaps (used by callbacks)
    vector<FeatureMap*> t_ifmaps;            // Transposed fmaps for pre
    vector<FeatureMap*> pre_ifmaps;          // Non-const fmaps for pre (used in pre_inference)
    vector<const FeatureMap*> pre_c_ifmaps;  // Const aliases of pre_ifmaps (used by callbacks)
    PrePost* pre_model = nullptr;            // Pre-model plugin

    // Output-side feature maps
    vector<FeatureMap*> ofmaps;               // Non-const fmaps (used by get_formatted_data)
    vector<const FeatureMap*> c_ofmaps;       // Const aliases of ofmaps (used by callbacks)
    vector<FeatureMap*> t_ofmaps;             // Transposed fmaps for post
    vector<FeatureMap*> post_ofmaps;          // Non-const fmaps for post (used in post_inference)
    vector<const FeatureMap*> post_c_ofmaps;  // Const aliases of post_ofmaps (used by callbacks)
    PrePost* post_model = nullptr;            // Post-model plugin

    ~IomapItem()
    {
        // delete input side feature maps
        for (auto &fm : ifmaps) {
            delete fm;
        }
        for (auto &fm : t_ifmaps) {
            delete fm;
        }
        for (auto &fm : pre_ifmaps) {
            delete fm;
        }
        delete pre_model;

        // delete output side feature maps
        for (auto &fm : ofmaps) {
            delete fm;
        }
        for (auto &fm : t_ofmaps) {
            delete fm;
        }
        for (auto &fm : post_ofmaps) {
            delete fm;
        }
        delete post_model;
    }
};

class MxModel
{
  public:
    int model_id_;         // unique id of the model on MXA
    Dfp::DfpObject* dfp_ = nullptr;  // Dfp object
    std::array<bool, 2> use_model_shape_;
    std::vector<int> open_contexts_;
    int ctx_infer_idx = 0;
    std::unordered_map<int, int> stream_id_map_;
    int parallel_fmap_convert_threads_ = 1;
    
    // sequence related: keep track of sequence number for each stream for ordering guarantees
    std::unordered_map<int, std::mutex> seq_mtxs_;
    std::unordered_map<int, std::condition_variable> seq_cvs_;
    std::unordered_map<int, atomic_int> seq_num_map_;
    std::unordered_map<int, atomic_int> next_seq_;

    // freelist-queue related
    vector<std::thread*> in_session_threads_;
    vector<std::thread*> out_session_threads_;
    std::thread* in_loop_thread_ = nullptr;
    std::thread* out_loop_thread_ = nullptr;
    std::thread* worker_monitor_thread_ = nullptr;

    LockedVar<int> num_in_session_done_ = 0;
    LockedVar<int> num_out_session_done_ = 0;
    LockedVar<int> num_stream_done_ = 0;

    LockedVar<bool> in_session_done_ = false;
    LockedVar<bool> out_session_done_ = false;
    LockedVar<bool> in_loop_done_ = false;
    LockedVar<bool> out_loop_done_ = false;

    std::unordered_map<int, StreamTask*> stream_task_map_;
    BQExtFlagX<StreamTask*>* input_tasks_ = nullptr;
    // RentalStore<IomapItem>* ifmap_freelist_;
    // RentalStore<IomapItem>* ofmap_freelist_;
    BlockyQueue<IomapItem*>* ifmap_freelist_ = nullptr;;
    BlockyQueue<IomapItem*>* ofmap_freelist_ = nullptr;;

    BQExtFlagX<IomapItem*>* ifmap_queue_ = nullptr;;
    BQExtFlagX<IomapItem*>* ofmap_queue_ = nullptr;;
    BQExtFlagX<inflightPacket>* inflights_ = nullptr;;

    vector<uint8_t> in_ports_;   // input port information
    vector<uint8_t> out_ports_;  // output port information
    int in_num_workers_ = 0;
    int out_num_workers_ = 0;

    void input_session();
    void output_session();
    void input_loop();
    void output_loop();

    IomapItem* _create_in_item();
    IomapItem* _create_out_item();

    void _delete_io_resources();

    // model information
    MX::Types::MxModelInfo minfo;
    MX::Types::MxModelInfo pre_minfo;
    MX::Types::MxModelInfo post_minfo;

    // Pre/Post-processing model plugins
    PrePost* pp_pre = nullptr;
    PrePost* pp_post = nullptr;

    bool local_mode_;
    Client* client_ = nullptr;

    // Pre-processing model items
    std::filesystem::path post_model_path_;
    std::vector<size_t> post_out_size_;

    // Post-processing model items
    std::filesystem::path pre_model_path_;
    std::vector<size_t> pre_out_size_;

    // stream timer for worker monitor thread
    ThreadSafeMap<int, std::chrono::steady_clock::time_point> stream_timer_;

    // manual thread related
    LockedVar<bool> model_manual_run = false;
    std::mutex manual_run_mutex;
    ThreadSafePtrMap<int, BlockyQueue<IomapItem*>> manual_result_buffer_;

    void manual_input_loop();
    void manual_output_loop();

    // utility functions
    void _post_inference(IomapItem* item);
    void _pre_inference(IomapItem* item);
    void _init_model_info();
    void _init_pipeline_vars(bool is_manual);
    void _determine_num_workers();
    void _worker_monitor();
    void _drain();

  public:
    MxModel(int model_id,
            Dfp::DfpObject* dfp_object,
            std::array<bool, 2> use_model_shape,
            bool local_mode,
            std::vector<int> open_contexts,
            Client* client);

    void model_start();
    void model_stop();
    void model_wait();
    bool all_tasks_done() const;

    LockedVar<bool> model_run = false;  // flag to specify if model is running

    ~MxModel();
    void connect_stream(callback_t in_cb, callback_t out_cb, int stream_id);
    int get_num_streams() const;
    MX::Types::MxModelInfo get_model_info() const;
    MX::Types::MxModelInfo get_pre_model_info() const;
    MX::Types::MxModelInfo get_post_model_info() const;

    void model_set_post(std::filesystem::path post_model_path, const std::vector<size_t> &post_out_size_list);
    void model_set_pre(std::filesystem::path pre_model_path);
    void get_num_workers(int &input_workers, int &output_workers) const;
    void set_num_workers(int input_workers, int output_workers);
    void set_parallel_fmap_convert(int num_threads);

    // manual thread
    void _model_manual_start();
    bool model_manual_send(std::vector<float*> user_ifmaps, int stream_id, int32_t timeout);
    bool model_manual_receive(std::vector<float*> &user_ofmaps, int stream_id, int32_t timeout);
    bool manual_run(std::vector<float*> user_ifmaps, std::vector<float*> &user_ofmaps, int stream_id, int32_t timeout);
};
}  // namespace Runtime
}  // namespace MX

#endif
