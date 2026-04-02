// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memx/accl/MxModel.h>
#include <memx/accl/prepost.h>

#include "spdlog/spdlog.h"

#include <fstream>

using namespace MX::Runtime;
using namespace MX::Types;
using namespace MX::Utils;

MxModel::MxModel(int model_id,
                 Dfp::DfpObject* dfp,
                 std::array<bool, 2> use_model_shape,
                 bool local_mode,
                 std::vector<int> open_contexts,
                 Client* client)
{
    // One task per stream, tasks will be added in connect_stream()
    input_tasks_ = new BQExtFlagX<StreamTask*>(UINT_MAX, &in_session_done_, true);

    // init variables
    model_id_ = model_id;
    local_mode_ = local_mode;
    dfp_ = dfp;
    open_contexts_ = open_contexts;
    client_ = client;
    use_model_shape_ = use_model_shape;

    // init model info
    in_ports_ = dfp_->get_dfp_meta()->model_inports[model_id];
    out_ports_ = dfp_->get_dfp_meta()->model_outports[model_id];
    _init_model_info();
}

void MxModel::_init_model_info()
{

    int num_in_ports = in_ports_.size();
    int num_op_ports = out_ports_.size();

    minfo.model_index = model_id_;
    minfo.num_in_featuremaps = num_in_ports;
    minfo.num_out_featuremaps = num_op_ports;
    minfo.use_model_shape_in = use_model_shape_[0];
    minfo.use_model_shape_out = use_model_shape_[1];

    // init input model info
    for (int ip = 0; ip < num_in_ports; ++ip) {
        int port_idx = in_ports_[ip];
        Dfp::PortInfo* pinfo = dfp_->input_port(port_idx);

        int64_t h = pinfo->dim_h;
        int64_t w = pinfo->dim_w;
        int64_t z = pinfo->dim_z;
        int64_t c = pinfo->dim_c;

        MX::Types::ShapeVector featureMap_shape{h, w, z, c};

        minfo.input_layer_names.push_back(std::string(pinfo->layer_name));
        minfo.in_featuremap_shapes.push_back(featureMap_shape);
        minfo.in_featuremap_sizes.push_back(pinfo->total_size);

        // Adding raw shape info
        std::vector<int64_t> raw(pinfo->raw_shape.size());
        for (const auto &pair : pinfo->raw_shape) {
            raw[pair.first] = pair.second;
        }
        minfo.in_raw_shapes.push_back(raw);
    }

    // init output model info
    for (int op = 0; op < num_op_ports; ++op) {
        int port_idx = out_ports_[op];

        Dfp::PortInfo* pinfo = dfp_->output_port(port_idx);

        int64_t h = pinfo->dim_h;
        int64_t w = pinfo->dim_w;
        int64_t z = pinfo->dim_z;
        int64_t c = pinfo->dim_c;

        MX::Types::ShapeVector featureMap_shape{h, w, z, c};

        minfo.output_layer_names.push_back(std::string(pinfo->layer_name));
        minfo.out_featuremap_shapes.push_back(featureMap_shape);
        minfo.out_featuremap_sizes.push_back(pinfo->total_size);

        // Adding raw shape info
        std::vector<int64_t> raw(pinfo->raw_shape.size());
        for (const auto &pair : pinfo->raw_shape) {
            raw[pair.first] = pair.second;
        }
        minfo.out_raw_shapes.push_back(raw);
    }
}

/**
 * Create and append the input featuremaps to the vector of input featuremaps
 *
 * Each time this called, it allocates the input featuremaps and their
 * transposed versions and appends them to the vector. If the pre-processing
 * model is connected, it also allocates the input featuremaps for the
 * pre-processing vector.
 */
IomapItem* MxModel::_create_in_item()
{
    // user --> pre_in_fmap --> plugin --> in_featuremaps --> chip

    IomapItem* item = new IomapItem();

    // init non-const fmaps
    for (int i = 0; i < static_cast<int>(in_ports_.size()); ++i) {
        int port = in_ports_[i];
        Dfp::PortInfo* pinfo = dfp_->input_port(port);

        FeatureMap* t = new FeatureMap(pinfo->total_size,
                                       (MX_data_format)pinfo->format,
                                       pinfo->dim_h,
                                       pinfo->dim_w,
                                       pinfo->dim_z,
                                       pinfo->dim_c,
                                       parallel_fmap_convert_threads_,
                                       use_model_shape_[0],
                                       pinfo);

        item->ifmaps.push_back(t);
    }

    // init const fmaps
    for (FeatureMap* fmap : item->ifmaps) {
        item->c_ifmaps.push_back(static_cast<const FeatureMap*>(fmap));
    }

    // init transposed fmaps
    for (FeatureMap* fmap : item->ifmaps) {
        FeatureMap* t_fmap = new FeatureMap(*fmap);

        // setting to FM_PRE, when FeatureMap::set_data is called (in runinference plugin) apply_transforms & convert_data should not run
        // for pre_in_featuremaps
        t_fmap->fm_type = FM_PRE;

        item->t_ifmaps.push_back(t_fmap);
    }

    // init when pre model is connected
    if (!pre_model_path_.empty()) {

        // init pre model plugin
        // NOTE: we need to duplicate PrePost model, since it is not thread-safe
        item->pre_model = mx_create_prepost(pre_model_path_);
        if (item->pre_model == nullptr) {
            throw(std::runtime_error("Error creating pre-procesing model - please verify connect_pre_model() "));
        }

        // init pre model featuremaps
        for (int i = 0; i < (int)pp_pre->get_input_names().size(); ++i) {
            FeatureMap* t = new FeatureMap(pre_minfo.in_featuremap_sizes[i], MX_FMT_FP32);
            t->fm_type = FM_PRE;
            item->pre_ifmaps.push_back(t);
        }

        // init const pre model featuremaps
        for (FeatureMap* fmap : item->pre_ifmaps) {
            item->pre_c_ifmaps.push_back(static_cast<const FeatureMap*>(fmap));
        }
    }

    return item;
}

/**
 * Create and append the output featuremaps to the vector of output featuremaps
 *
 * Each time this called, it allocates the output featuremaps and their
 * transposed versions and appends them to the vector. If the post-processing
 * model is connected, it also allocates the output featuremaps for the
 * post-processing vector.
 */
IomapItem* MxModel::_create_out_item()
{
    IomapItem* item = new IomapItem();

    // init non-const fmaps
    for (int i = 0; i < static_cast<int>(out_ports_.size()); ++i) {
        int port = out_ports_[i];
        Dfp::PortInfo* pinfo = dfp_->output_port(port);

        FeatureMap* t = new FeatureMap(pinfo->total_size,
                                       (MX_data_format)pinfo->format,
                                       pinfo->dim_h,
                                       pinfo->dim_w,
                                       pinfo->dim_z,
                                       pinfo->dim_c,
                                       parallel_fmap_convert_threads_,
                                       use_model_shape_[1],
                                       pinfo);

        item->ofmaps.push_back(t);
    }

    // init const fmaps
    for (FeatureMap* fmap : item->ofmaps) {
        item->c_ofmaps.push_back(static_cast<const FeatureMap*>(fmap));
    }

    // init transposed fmaps
    for (FeatureMap* fmap : item->ofmaps) {
        FeatureMap* t_fmap = new FeatureMap(*fmap);

        // setting to FM_POST, when FeatureMap::get_data is called (in runinference plugin) apply_transforms & convert_data should not run
        // for post_in_featuremaps
        t_fmap->fm_type = FM_POST;

        item->t_ofmaps.push_back(t_fmap);
    }

    // init when post model is connected
    if (!post_model_path_.empty()) {

        // init post model plugin
        // NOTE: we need to duplicate PrePost model, since it is not thread-safe
        item->post_model = mx_create_prepost(post_model_path_);
        if (item->post_model == nullptr) {
            throw(std::runtime_error("Error creating post-procesing model - "
                                     "please verify connect_post_model() "));
        }

        // init post model featuremaps
        vector<FeatureMap*> temp_pov;
        for (int i = 0; i < (int)pp_post->get_output_names().size(); ++i) {
            FeatureMap* t = new FeatureMap(post_minfo.out_featuremap_sizes[i]);
            t->fm_type = FM_POST;
            item->post_ofmaps.push_back(t);
        }

        // init const post model featuremaps
        for (FeatureMap* fmap : item->post_ofmaps) {
            item->post_c_ofmaps.push_back(static_cast<const FeatureMap*>(fmap));
        }
    }

    return item;
}

/**
 * Sets the post-processing model for the MxModel instance.
 *
 * This function configures the post-processing model by setting the path
 * and output sizes. It initializes the post-info model using the specified
 * post-processing path and size list. It then updates the model's output
 * feature map information and checks for dynamic output sizes, throwing an
 * error if necessary sizes are not provided. The function also matches
 * the names of the model's output layers with the post-processing model.
 *
 * @param post_path Path to the post-processing model.
 * @param post_out_sizelist Vector containing the output sizes for the
 *                          post-processing model.
 */

void MxModel::model_set_post(std::filesystem::path post_path, const std::vector<size_t> &post_out_sizelist)
{
    post_model_path_ = post_path;
    post_out_size_ = post_out_sizelist;

    pp_post = mx_create_prepost(post_model_path_, post_out_size_);

    if (pp_post->dynamic_output) {
        if (post_out_size_.size() == 0) {
            throw std::runtime_error("The given post-processing model might have dynamic output size. Please provide the largest \
                                    possible size of output in the second argument of connect_post_model()");
        }
    }

    // must have use_model_shape_[1] == true when using a post-processing model
    if (!use_model_shape_[1]) {
        spdlog::error("Post-processing model requires use_model_shape.output to be true");
        throw std::runtime_error("Post-processing model requires use_model_shape.output to be true");
    }

    pp_post->match_names(minfo.output_layer_names, Process_Post);

    post_minfo.num_in_featuremaps = minfo.num_out_featuremaps;
    post_minfo.in_featuremap_shapes = minfo.out_featuremap_shapes;
    post_minfo.in_featuremap_sizes = minfo.out_featuremap_sizes;
    post_minfo.input_layer_names = minfo.output_layer_names;
    post_minfo.use_model_shape_in = use_model_shape_[0];
    post_minfo.use_model_shape_out = use_model_shape_[1];

    post_minfo.num_out_featuremaps = pp_post->get_output_sizes().size();

    for (int op = 0; op < post_minfo.num_out_featuremaps; ++op) {
        if (post_out_size_.size() == 0) {  // Post model sizes are not given by the user so we get the
            // sizes from post info
            std::vector<int64_t> output_shape = pp_post->get_output_shapes()[op];
            MX::Types::ShapeVector featureMap_shape(static_cast<int>(output_shape.size()));
            for (int i = 0; i < static_cast<int>(output_shape.size()); ++i) {
                featureMap_shape[i] = output_shape[i];
            }
            post_minfo.out_featuremap_sizes.push_back(pp_post->get_output_sizes()[op]);
            post_minfo.output_layer_names.push_back(pp_post->get_output_names()[op]);
            post_minfo.out_featuremap_shapes.push_back(featureMap_shape);
        }

        else {
            post_minfo.out_featuremap_sizes.push_back(post_out_size_[op]);
        }
    }

    // In the case of dfp output and post inputs not matching in terms of order
    // or number, we find the matching and for the direct outputs from the dfp
    // that are not inputs to the post-model, we keep their names so that they
    // can later be appended to the post-model outputs.

    // NOTE:
    // The DFP model may output many feature maps, but the post-model may only
    // take some of them as inputs.
    //
    // `real_featuremaps` is a vector of indices. It lists exactly which DFP
    // outputs the post-model should use — in the correct order.
    for (int i = 0; i < (int)pp_post->real_featuremaps.size(); ++i) {
        post_minfo.num_out_featuremaps++;
        if (pp_post->type == Plugin_Onnx) {
            minfo.out_featuremap_shapes[pp_post->real_featuremaps[i]].set_ch_first();
        }

        post_minfo.out_featuremap_shapes.push_back(minfo.out_featuremap_shapes[pp_post->real_featuremaps[i]]);
        post_minfo.out_featuremap_sizes.push_back(minfo.out_featuremap_sizes[pp_post->real_featuremaps[i]]);
        post_minfo.output_layer_names.push_back(minfo.output_layer_names[pp_post->real_featuremaps[i]]);

        // printf("\n<<< Post model real output \"%s\" total_size: %lu\n",
        //        minfo.output_layer_names[pp_post->real_featuremaps[i]].c_str(),
        //        minfo.out_featuremap_sizes[pp_post->real_featuremaps[i]]);
    }
}

/**
 * @brief Connects a pre-processing model to the current model.
 *
 * Given a pre-processing model path, this function connects the model to the
 * current model.
 *
 * @param pre_path Path to the pre-processing model.
 */
void MxModel::model_set_pre(std::filesystem::path pre_path)
{
    pre_model_path_ = pre_path;
    pp_pre = mx_create_prepost(pre_model_path_);
    pp_pre->match_names(minfo.input_layer_names, Process_Pre);

    pre_minfo.num_in_featuremaps = pp_pre->get_input_sizes().size();

    for (int ip = 0; ip < pre_minfo.num_in_featuremaps; ++ip) {
        std::vector<int64_t> input_shape = pp_pre->get_input_shapes()[ip];

        MX::Types::ShapeVector featureMap_shape(static_cast<int>(input_shape.size()));

        for (int i = 0; i < static_cast<int>(input_shape.size()); ++i) {
            featureMap_shape[i] = input_shape[i];
        }

        pre_minfo.in_featuremap_shapes.push_back(featureMap_shape);
        pre_minfo.in_featuremap_sizes.push_back(pp_pre->get_input_sizes()[ip]);
        pre_minfo.input_layer_names.push_back(pp_pre->get_input_names()[ip]);

        //// printf all the pre_minfo data
        // printf("\n>> Pre layer \"%s\" total_size: %lu\n",
        // pp_pre->get_input_names()[ip].c_str(),
        // pp_pre->get_input_sizes()[ip]);
    }
    pre_minfo.num_out_featuremaps = minfo.num_in_featuremaps;
    pre_minfo.out_featuremap_shapes = minfo.in_featuremap_shapes;
    pre_minfo.out_featuremap_sizes = minfo.in_featuremap_sizes;
    pre_minfo.output_layer_names = minfo.input_layer_names;

    // use_model_shape_[0] must be true when using a pre-processing model
    if (!use_model_shape_[0]) {
        spdlog::error("Pre-processing model requires use_model_shape.input to be true");
        throw std::runtime_error("Pre-processing model requires use_model_shape.input to be true");
    }

    // printf("\n>> Pre model info: num_in_featuremaps: %d, num_out_featuremaps:
    // %d\n",
    //        pre_minfo.num_in_featuremaps,
    //        pre_minfo.num_out_featuremaps);

    // In the case of dfp input and pre outputs not matching in terms of order
    // or number, we find the matching and for the direct inputs to the dfp that
    // are not outputs of the pre-model, we keep their names so that they can
    // later be appended to the MxModel inputs.
    for (int i = 0; i < (int)pp_pre->real_featuremaps.size(); ++i) {
        pre_minfo.num_in_featuremaps++;
        if (pp_pre->type == Plugin_Onnx) {
            minfo.in_featuremap_shapes[pp_pre->real_featuremaps[i]].set_ch_first();
        }

        pre_minfo.in_featuremap_shapes.push_back(minfo.in_featuremap_shapes[pp_pre->real_featuremaps[i]]);
        pre_minfo.in_featuremap_sizes.push_back(minfo.in_featuremap_sizes[pp_pre->real_featuremaps[i]]);
        pre_minfo.input_layer_names.push_back(minfo.input_layer_names[pp_pre->real_featuremaps[i]]);

        // printf("\n>>> Pre model real input \"%s\" total_size: %lu\n",
        //        minfo.input_layer_names[pp_pre->real_featuremaps[i]].c_str(),
        //        minfo.in_featuremap_sizes[pp_pre->real_featuremaps[i]]);
    }
}

void MxModel::set_num_workers(int input_workers, int output_workers)
{
    if (input_workers < 0 || output_workers < 0) {
        throw logic_error("number of workers must be 0 (auto) or a number >= 1");
    }
    in_num_workers_ = input_workers;
    out_num_workers_ = output_workers;
}

void MxModel::get_num_workers(int &input_workers, int &output_workers) const
{
    input_workers = in_num_workers_;
    output_workers = out_num_workers_;
}

void MxModel::set_parallel_fmap_convert(int num_threads)
{

    if (num_threads < 2) {
        parallel_fmap_convert_threads_ = 1;
    }
    else {
        parallel_fmap_convert_threads_ = num_threads;
    }
}

void MxModel::_init_pipeline_vars(bool is_manual)
{
    num_stream_done_ = 0;
    num_in_session_done_ = 0;
    num_out_session_done_ = 0;

    in_session_done_ = false;
    out_session_done_ = false;
    in_loop_done_ = false;
    out_loop_done_ = false;

    // delete old IO resources if they exist (e.g., when model_start() is called multiple times)
    _delete_io_resources();

    int num_streams = get_num_streams();
    
    // reset sequence number for each stream
    seq_num_map_.clear();
    for (int i = 0; i < num_streams; ++i) {
        seq_num_map_[i] = 0;
        next_seq_[i] = 0;
    }

    // ===== Initialize queues and freelists =====
    // NOTE:
    // _create_X_item() relies on parallel_fmap_convert_threads, and it might be reset after construction.
    // So freelist initialization must be done in model_start(), not in the constructor.

    // NOTE: For manual threads, num_streams can be 0, so we enforce a minimum queue size of 2
    int ifmap_queue_size = max(num_streams * 2, 2);
    int ofmap_queue_size = max(num_streams * 2, 2);

    ifmap_freelist_ = new BlockyQueue<IomapItem*>(ifmap_queue_size);
    ofmap_freelist_ = new BlockyQueue<IomapItem*>(ofmap_queue_size);

    // init IomapItem and push into freelist
    for (int i = 0; i < ifmap_queue_size; i++) {
        IomapItem* item = _create_in_item();
        ifmap_freelist_->push(item);
    }
    for (int i = 0; i < ofmap_queue_size; i++) {
        IomapItem* item = _create_out_item();
        ofmap_freelist_->push(item);
    }

    if (is_manual) {
        // Manual mode: do not monitor stream status, so use `model_manual_run` to determine when ifmapQ to stop waiting
        ifmap_queue_ = new BQExtFlagX<IomapItem*>(ifmap_queue_size, &model_manual_run, false);
    }
    else {
        // Auto thread, in_session monitors if streams are done
        ifmap_queue_ = new BQExtFlagX<IomapItem*>(ifmap_queue_size, &in_session_done_, true);
    }

    ofmap_queue_ = new BQExtFlagX<IomapItem*>(ofmap_queue_size, &out_loop_done_, true);
    inflights_ = new BQExtFlagX<inflightPacket>(UINT_MAX, &in_loop_done_, true);
}

void MxModel::_determine_num_workers()
{

    int num_streams = get_num_streams();
    int num_cpu_cores = std::thread::hardware_concurrency();
    int num_models = dfp_->get_dfp_meta()->num_models;

    // set default num_workers, num_workers will be adjusted on fly in worker monitor thread
    int default_num_workers = max(1, min(num_streams, num_cpu_cores / (2 * num_models)));
    if (in_num_workers_ == 0) 
        in_num_workers_ = default_num_workers;
    if (out_num_workers_ == 0) 
        out_num_workers_ = default_num_workers;
}

/**
 * This function starts the model by allocating the required memory, creating
 * and starting the model threads, and setting the corresponding flags to
 * true.
 *
 * @note This function should be called after the model has been set up and
 * before any inference calls.
 */
void MxModel::model_start()
{
    if (get_num_streams() == 0) {
        spdlog::warn("Model {}: no streams connected. No threads will be started.", model_id_);
        return;
    }

    // determine number of workers for input/output sessions
    _determine_num_workers();

    // init such as ifmap_queue, freelists
    _init_pipeline_vars(false /* is_manual */);

    // used in input_session, will push and pop frequently
    for (const auto& [stream_id, task] : stream_task_map_) {
        input_tasks_->push(task);
    }

    // model start running
    // NOTE: model_run flag must be set before worker monitor thread starts
    model_run.store(true);

    // start worker monitor thread
    // 
    // NOTE:
    // The monitor thread introduces slight delay during shutdown (especially annoying when running tests)
    // because it sleeps before exiting, so start it only when necessary.
    if (in_num_workers_ < get_num_streams()) {
        // Enter if, this is only case where we need to dynamically adjust workers.
        worker_monitor_thread_ = new std::thread(&MxModel::_worker_monitor, this);
    }

    // start sessions with specified number of workers
    for (int i = 0; i < in_num_workers_; ++i) {
        std::thread* th = new std::thread(&MxModel::input_session, this);
        in_session_threads_.push_back(th);
    }
    for (int i = 0; i < out_num_workers_; ++i) {
        std::thread* th = new std::thread(&MxModel::output_session, this);
        out_session_threads_.push_back(th);
    }

    // start loops (one thread per loop)
    // 
    // NOTE: Each loop must run on a single thread. 
    // If, for any reason in the future, multiple threads are used, 
    // FMAP synchronization must be handled explicitly to ensure
    // send/receive FMAP in order.
    in_loop_thread_ = new std::thread(&MxModel::input_loop, this);
    out_loop_thread_ = new std::thread(&MxModel::output_loop, this);
}

/*
 * Monitors worker threads and dynamically adjusts the worker count.
 * If all workers are idle, it spawns an additional worker (up to the number of streams)
 * to avoid deadlock.
 *
 * Note: A stream can become idle. For example, in a multi-stage pipeline
 * (e.g., car detection -> plate OCR), a stream may receive no plates
 * for several consecutive frames.
 * 
 * See this PR for detail:
 * https://github.com/memryx/MX_API/pull/281
 */
void MxModel::_worker_monitor()
{
    const auto idle_thres = std::chrono::seconds(3);
    int num_streams = get_num_streams();

    while (model_run.load() && !in_session_done_.load()) {
        int num_idle = 0;
        auto now = std::chrono::steady_clock::now();
            
        for (const auto& [id, start] : stream_timer_.get_all_pairs()) {
            if (now - start > idle_thres) { 
                num_idle++;
            }
        }

        // Maximum number of workers is capped by the number of streams
        // 
        // NOTE: We only spawn extra workers for the input session.
        // The output session should not be blocked. If it is blocked, it likely means
        // the output callback has entered a busy loop, which should not happen.
        if (num_idle == in_num_workers_ && in_num_workers_ < num_streams) {
            // spawn extra in session worker
            in_num_workers_++;
            spdlog::debug("[Model {}][Worker Monitor]: Detected {} idle workers, spawning an extra in session worker thread", model_id_, num_idle);
            std::thread* th = new std::thread(&MxModel::input_session, this);
            in_session_threads_.push_back(th);
        }
        
        // avoid busy looping
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// Helper function that runs the inference on the pre-processing model
void MxModel::_pre_inference(IomapItem* item)
{
    // pre plugin run inference
    vector<FeatureMap*> permuted_output;
    for (int i = 0; i < (int)pp_pre->dfp_pattern.size(); ++i) {
        permuted_output.push_back(item->t_ifmaps[pp_pre->dfp_pattern[i]]);
    }

    // pre plugin run inference
    // input: item->pre_ifmaps
    // output: permuted_output
    item->pre_model->runinference(item->pre_ifmaps, permuted_output);

    // t_ifmaps -> ifmaps
    for (int i = 0; i < minfo.num_in_featuremaps; ++i) {
        item->ifmaps[i]->set_data_force_foldedops((float*)item->t_ifmaps[i]->get_data_ptr(), true);
    }
}

// Helper function that runs the inference on the post-processing model
void MxModel::_post_inference(IomapItem* item)
{
    int stream = item->stream_id;

    // ofmaps -> t_ofmaps
    // Notice this path forces a transpose. That's the reason
    // `use_model_shape_[1]` must be true when given a post-model.
    for (int i = 0; i < minfo.num_out_featuremaps; ++i) {
        item->ofmaps[i]->get_data_force_foldedops((float*)item->t_ofmaps[i]->get_data_ptr(), true);
    }

    // In case of dynamic output we initiate the data with zeros as it is
    // possible that the actual output is smaller than allocated featuremap
    if (item->post_model->dynamic_output) {
        for (int m = 0; m < static_cast<int>(post_out_size_.size()); ++m) {
            memset(item->post_ofmaps[m]->get_data_ptr(), 0, post_out_size_[m] * sizeof(float));
        }
    }

    // post plugin run inference
    std::vector<FeatureMap*> permuted_output;
    for (int i = 0; i < (int)pp_post->dfp_pattern.size(); ++i) {
        permuted_output.push_back(item->t_ofmaps[pp_post->dfp_pattern[i]]);
    }

    // IO for post model
    // input: permuted_output
    // output: item->post_ofmaps
    item->post_model->runinference(permuted_output, item->post_ofmaps);

    // t_ofmaps -> post_ofmaps ()
    // FIXME: this section apparently has a bug. item->post_ofmaps should not keep push_back each time
    for (int i = 0; i < (int)pp_post->real_featuremaps.size(); ++i) {
        item->t_ofmaps[pp_post->real_featuremaps[i]]->fm_type = FM_POST;
        item->post_ofmaps.push_back(item->t_ofmaps[pp_post->real_featuremaps[i]]);
    }
}

// The actual task sent to input threadpools
void MxModel::input_session()
{
#ifdef GDB_DEBUG
    std::cout << "input_session tid = " << std::hex << "0x" << (uintptr_t)pthread_self() << std::dec << std::endl;
#endif

    StreamTask* task = nullptr;

    for (;;) {

        // get the next stream task
        if (input_tasks_->pop(task) == false) {
            spdlog::debug("[Model {}][input session]: thread done due to input_tasks is empty", model_id_);
            break;
        }

        int stream_id = task->id;

        // checkout a IomapItem from freelist
        IomapItem* item = ifmap_freelist_->pop();
        item->stream_id = stream_id;  // set stream_id

        spdlog::debug("[Model {}][input_session]: run input callback for stream {}", model_id_, stream_id);

        bool stream_continue;
        stream_timer_.update(stream_id, std::chrono::steady_clock::now()); // update stream timer
        if (!pre_model_path_.empty()) {

            // TODO: implement _pre_copy
            // _pre_copy()

            // call app's input callback
            stream_continue = task->in_cb(item->pre_c_ifmaps, stream_id);

            // infer with pre model
            _pre_inference(item);
        }
        else {
            // call app's input callback
            stream_continue = task->in_cb(item->c_ifmaps, stream_id);
        }

        if (model_run.load() && stream_continue) {            
            // CRITICAL NOTE: 
            // 
            // Push data to ifmapQ BEFORE requeuing the task.
            // 
            // This prevents a race condition where a second thread picks up the task, 
            // finishes the stream(stream_continue is FALSE) , and triggers a shutdown
            // while this thread is still pushing the current item -- this item will be lost and never processed
            ifmap_queue_->push(item);

            // Requeue the stream task for continuous input
            // NOTE:
            // task push should lies the bottom of the section of stream_continue == true
            input_tasks_->push(task); 
        }
        else {
            num_stream_done_++;

            // check if all streams are done
            if (num_stream_done_.load() == get_num_streams()) {
                spdlog::debug("[Model {}][input_session]: all streams are done", model_id_);

                // this flag has to be set true before notify
                in_session_done_.store(true);

                // notify
                input_tasks_->notify();
                ifmap_queue_->notify();

                break;
            }
        }
    }

    // done
}

void MxModel::input_loop()
{
#ifdef GDB_DEBUG
    std::cout << "input_loop tid = " << std::hex << "0x" << (uintptr_t)pthread_self() << std::dec << std::endl;
#endif

    IomapItem* item = nullptr;
    for (;;) {

        // get next input IomapItem
        if (ifmap_queue_->pop(item) == false) {
            spdlog::debug("[Model {}][input loop]: thread done due to ifmap_queue is empty", model_id_);
            break;
        }

        inflightPacket packet;
        packet.stream_id = item->stream_id;

        // send input data to mxa chip
        if (local_mode_) {

            int ctx_infer = open_contexts_.at(ctx_infer_idx);
            packet.ctx_infer = ctx_infer;

            // Sending inputs to MPU in local mode
            memx_status status;
            for (int i = 0; i < static_cast<int>(in_ports_.size()); ++i) {
                status = memx_stream_ifmap(ctx_infer, in_ports_[i], item->ifmaps[i]->get_formatted_data(), 0 /* timeout */);
                if (memx_status_error(status)) {
                    throw runtime_error("stream_ifmap failed, try resetting the MXA");
                }
            }

            // FIXME: this is some sussy code for load-balancing.....
            // update ctx_infer idx
            ctx_infer_idx = (ctx_infer_idx + 1) % open_contexts_.size();

        }
        else {

            // Sending inputs to mxa-manager
            for (int i = 0; i < static_cast<int>(in_ports_.size()); ++i) {
                if (client_->send(item->ifmaps[i]->get_formatted_data(), item->ifmaps[i]->get_formatted_size()) == false) {
                    throw runtime_error("Error in sending data to mxa-manager");
                }
            }
        }

        // push this frame's packet to the inflight tracker
        inflights_->push(packet);

        spdlog::debug("[Model {}][input_loop]: pushed stream {} item to inflights tracker", model_id_, item->stream_id);

        // return this IomapItem to the ifmap freelist
        ifmap_freelist_->push(item);
    }

    // done
    spdlog::debug("Model {}: input loop thread done", model_id_);
    in_loop_done_.store(true);
    inflights_->notify();
}

void MxModel::output_loop()
{
#ifdef GDB_DEBUG
    std::cout << "output_loop tid = " << std::hex << "0x" << (uintptr_t)pthread_self() << std::dec << std::endl;
#endif

    IomapItem* item = nullptr;

    for (;;) {

        inflightPacket packet;

        // get the next completed frame's packet from inflights tracker
        if (inflights_->pop(packet) == false) {
            spdlog::debug("Model {}: output loop thread done due to inflights "
                          "tracker is empty",
                          model_id_);
            break;
        }

        int stream_id = packet.stream_id;
        int ctx_infer = packet.ctx_infer;

        spdlog::debug("[Model {}][output_loop]: got data from inflights of stream {}", model_id_, stream_id);

        // checkout a IomapItem from ofmap freelist
        ofmap_freelist_->pop(item);
        item->stream_id = stream_id;  // reset stream_id

        // NOTE: only single thread for output_loop, so no need for lock here accessing seq_num_map_
        // set sequence number for this item, which will be used in post-processing and output callback to ensure the order of frames
        item->seq_num = seq_num_map_[stream_id];
        seq_num_map_[stream_id]++;

        if (local_mode_) {

            memx_status status;
            for (int i = 0; i < static_cast<int>(out_ports_.size()); ++i) {
                status = memx_stream_ofmap(ctx_infer, out_ports_[i], item->ofmaps[i]->get_formatted_data(), 0 /* timeout */);
                if (memx_status_error(status)) {
                    throw runtime_error("stream_ofmap failed, try resetting the MXA");
                }
            }
        }
        else {

            // receive output data from server
            for (int i = 0; i < static_cast<int>(out_ports_.size()); ++i) {
                if (client_->recv(item->ofmaps[i]->get_formatted_data(), item->ofmaps[i]->get_formatted_size()) == false) {
                    throw runtime_error("Error in receving data from mxa-manager");
                }
            }
        }

        // push this frame's IomapItem to ofmap queue
        ofmap_queue_->push(item);

        spdlog::debug("[Model {}][output_loop]: pushed stream {} item to ofmap queue", model_id_, stream_id);
    }

    // done
    spdlog::debug("Model {}: output loop thread done", model_id_);
    out_loop_done_.store(true);
    ofmap_queue_->notify();
}

// The actual task sent to output threadpools
void MxModel::output_session()
{
#ifdef GDB_DEBUG
    std::cout << "output_session tid = " << std::hex << "0x" << (uintptr_t)pthread_self() << std::dec << std::endl;
#endif

    for (;;) {

        IomapItem* item = nullptr;

        // get the next output IomapItem
        if (ofmap_queue_->pop(item) == false) {
            spdlog::debug("Model {}: output session thread done due to ofmap_queue is empty", model_id_);
            break;
        }

        // post-processing inference if post model is connected
        if (!post_model_path_.empty()) {
            _post_inference(item);
        }

        int stream_id = item->stream_id;
        int seq_num = item->seq_num;

        if (seq_num != next_seq_[stream_id].load()) {
            spdlog::debug("Model {}: stream {} out of order: expected seq_num {}, got seq_num {}", model_id_, stream_id, next_seq_[stream_id].load(), seq_num);
            // wait until it's turn
            std::unique_lock<std::mutex> lk(seq_mtxs_[stream_id]);
            seq_cvs_[stream_id].wait(lk, [&](){ return seq_num == next_seq_[stream_id].load(); });
        }

        // call app's output callback
        if (!post_model_path_.empty()) {
            stream_task_map_[stream_id]->out_cb(item->post_c_ofmaps, stream_id);
        }
        else {
            stream_task_map_[stream_id]->out_cb(item->c_ofmaps, stream_id);
        }

        // notify next frame of this stream
        next_seq_[stream_id]++;
        seq_cvs_[stream_id].notify_all();

        spdlog::debug("[Model {}][output_session]: run out callback for stream {}", model_id_, stream_id);

        // return this IomapItem to the ofmap freelist
        ofmap_freelist_->push(item);
    }


    // update the number of input sessions done
    num_out_session_done_++;

    // done
    if (num_out_session_done_.load() == (int)out_session_threads_.size()) {
        out_session_done_.store(true);
        spdlog::debug("Model {}: output session thread done", model_id_);
    }
}

int MxModel::get_num_streams() const
{
    return stream_task_map_.size();
}

void MxModel::model_wait()
{
    if (!model_run.load()) {
        std::string msg = fmt::format("Model {}: must call accl.start() before accl.wait()", model_id_);
        throw std::logic_error(msg);
    }

    for (auto &th : in_session_threads_) {
        if (th->joinable()) {
            th->join();
        }
    }
    for (auto &th : out_session_threads_) {
        if (th->joinable()) {
            th->join();
        }
    }
    in_loop_thread_->join();
    out_loop_thread_->join();

    // in case leftover data in mx chips
    _drain();

    // mark as not running
    model_run.store(false);
    
    if (worker_monitor_thread_ && worker_monitor_thread_->joinable())
        worker_monitor_thread_->join();

    spdlog::debug("[MxModel {}] All task finished. model_wait() ends.", model_id_);
}

void MxModel::_drain() {
    
    // NOTE: draining logic for shared mode is implemented in mxa-manager

    if (local_mode_ && model_run.load()) {

        IomapItem* item = nullptr;
                    
        // pop item either from ofmap freelist or queue
        if (ofmap_freelist_->pop_timeout(item, 10 /* timeout */) == false) {
            ofmap_queue_->pop(item);
        }
        
        // drain all driver contexts
        for (int ctx : open_contexts_) {

            int num_drained = 0;
            memx_status status = MEMX_STATUS_OK;
            while (true) {

                for (int i = 0; i < static_cast<int>(out_ports_.size()); ++i) {
                    status = (memx_status) ((int)status |  memx_stream_ofmap(ctx, out_ports_[i], item->ofmaps[i]->get_formatted_data(), 100 /* timeout */));
                }

                if (status != MEMX_STATUS_OK)
                    break;

                num_drained++;
            }
            spdlog::debug("Model {}: Context {} drained {} frames", model_id_, ctx, num_drained);
        }

        ofmap_freelist_->push(item);
    }
}
void MxModel::model_stop()
{
    // Do nothing. Keep this function just for backward compatibility.
}

MX::Types::MxModelInfo MxModel::get_model_info() const
{
    return this->minfo;
}

MX::Types::MxModelInfo MxModel::get_pre_model_info() const
{
    return this->pre_minfo;
}

MX::Types::MxModelInfo MxModel::get_post_model_info() const
{
    return this->post_minfo;
}

void MxModel::_delete_io_resources() {
    while (ifmap_freelist_ && !ifmap_freelist_->empty()) {
        IomapItem* item = nullptr;
        ifmap_freelist_->pop(item);
        delete item;
    }
    while (ofmap_freelist_ && !ofmap_freelist_->empty()) {
        IomapItem* item = nullptr;
        ofmap_freelist_->pop(item);
        delete item;
    }
    delete ifmap_freelist_;
    delete ofmap_freelist_;
    delete ifmap_queue_;
    delete ofmap_queue_;
    delete inflights_;

    ifmap_freelist_ = nullptr;
    ofmap_freelist_ = nullptr;
    ifmap_queue_ = nullptr;
    ofmap_queue_ = nullptr;
    inflights_ = nullptr;
}

MxModel::~MxModel()
{
    spdlog::debug("Model {}: MxModel destructor called", model_id_);

    // mark as not running
    model_run.store(false);
    model_manual_run.store(false);
    
    // Manual mode not monitor stream status, might get stuck in ifmap_queue->pop() in manual_input_loop
    if (ifmap_queue_)
        ifmap_queue_->notify();

    // delete threads
    for (auto &th : in_session_threads_) {
        if (th->joinable()) {
            th->join();
        }
        delete th;
    }
    for (auto &th : out_session_threads_) {
        if (th->joinable()) {
            th->join();
        }
        delete th;
    }

    if (in_loop_thread_ && in_loop_thread_->joinable()) {
        in_loop_thread_->join();
    }
    if (out_loop_thread_ && out_loop_thread_->joinable()) {
        out_loop_thread_->join();
    }
    delete in_loop_thread_;
    delete out_loop_thread_;

    if (worker_monitor_thread_ && worker_monitor_thread_->joinable()) {
        worker_monitor_thread_->join();
    }
    delete worker_monitor_thread_;
    
    // in case leftover data in mx chips
    _drain();

    _delete_io_resources();

    // delete stream tasks
    for (auto &pair : stream_task_map_) {
        delete pair.second;
    }
    delete input_tasks_;

    // delete result buffer items
    manual_result_buffer_.clear();
}

void MxModel::connect_stream(callback_t in_cb, callback_t out_cb, int stream_id)
{

    // Disallow connect streams after starting the Model
    if (model_run.load()) {
        throw logic_error("connect_stream called after starting MxAccl");
    }

    // Disallow nullptr callbacks
    if (in_cb == nullptr || out_cb == nullptr) {
        throw invalid_argument("input callback or output callback got a nullptr!");
    }

    // Disallow duplicate stream ids
    if (stream_task_map_.count(stream_id) > 0) {
        throw invalid_argument("duplicate stream id passed in connect_stream");
    }

    StreamTask* task = new StreamTask(stream_id, in_cb, out_cb);

    stream_task_map_[stream_id] = task;
}


bool MxModel::all_tasks_done() const
{
    return out_session_done_.load();
}

void MxModel::manual_input_loop()
{
#ifdef GDB_DEBUG
    std::cout << "manual_input_loop tid = " << std::hex << "0x" << (uintptr_t)pthread_self() << std::dec << std::endl;
#endif

    for (;;) {

        IomapItem* item = nullptr;

        // get next input IomapItem
        if (ifmap_queue_->pop(item) == false) {
            spdlog::debug("[Model {}][manual_input_loop]: input loop thread done due to ifmap_queue is empty", model_id_);
            break;
        }

        inflightPacket packet;
        packet.stream_id = item->stream_id;

        // send input data to mxa chip
        if (local_mode_) {

            int ctx_infer = open_contexts_.at(ctx_infer_idx);
            packet.ctx_infer = ctx_infer;

            // Sending inputs to MPU in local mode
            memx_status status;
            for (int i = 0; i < static_cast<int>(in_ports_.size()); ++i) {
                status = memx_stream_ifmap(ctx_infer, in_ports_[i], item->ifmaps[i]->get_formatted_data(), 0 /* timeout */);
                if (memx_status_error(status)) {
                    throw runtime_error("stream_ifmap failed, try resetting the MXA");
                }
            }

            // FIXME: this is some sussy code for load-balancing.....
            // update ctx_infer idx
            ctx_infer_idx = (ctx_infer_idx + 1) % open_contexts_.size();

        }
        else {
            for (int i = 0; i < static_cast<int>(in_ports_.size()); ++i) {
                if (client_->send(item->ifmaps[i]->get_formatted_data(), item->ifmaps[i]->get_formatted_size()) == false) {
                    throw runtime_error("Error in sending data to mxa-manager");
                }
            }
        }

        // push this frame's packet to the inflight tracker
        inflights_->push(packet);

        spdlog::debug("[Model {}][manual_input_loop]: pushed stream {} item to inflights tracker", model_id_, item->stream_id);

        // return this IomapItem to the ifmap freelist
        ifmap_freelist_->push(item);
    }

    // done
    spdlog::debug("Model {}: manual input loop thread done", model_id_);
    in_loop_done_.store(true);
    inflights_->notify();
}

void MxModel::manual_output_loop()
{
#ifdef GDB_DEBUG
    std::cout << "manual_output_loop tid = " << std::hex << "0x" << (uintptr_t)pthread_self() << std::dec << std::endl;
#endif

    for (;;) {

        IomapItem* item = nullptr;
        inflightPacket packet;

        // get the next completed frame's stream_id from inflights tracker
        if (inflights_->pop(packet) == false) {
            spdlog::debug("[Model {}][manual_output_loop]: output loop thread done due to inflights tracker is empty", model_id_);
            break;
        }

        int stream_id = packet.stream_id;
        int ctx_infer = packet.ctx_infer;

        spdlog::debug("[Model {}][manual_output_loop]: got data completed stream {} from inflights tracker", model_id_, stream_id);

        // checkout a IomapItem from ofmap freelist
        ofmap_freelist_->pop(item);
        item->stream_id = stream_id;  // reset stream_id

        if (local_mode_) {
            memx_status status;
            for (int i = 0; i < static_cast<int>(out_ports_.size()); ++i) {
                status = memx_stream_ofmap(ctx_infer, out_ports_[i], item->ofmaps[i]->get_formatted_data(), 0 /* timeout */);
                if (memx_status_error(status)) {
                    throw runtime_error("stream_ofmap failed, try resetting the MXA");
                }
            }
        }
        else {

            // receive output data from server
            for (int i = 0; i < static_cast<int>(out_ports_.size()); ++i) {
                if (client_->recv(item->ofmaps[i]->get_formatted_data(), item->ofmaps[i]->get_formatted_size()) == false) {
                    throw runtime_error("Error in receving data from mxa-manager");
                }
            }
        }

        // Push this frame's IomapItem into the result buffer for the given
        // stream_id. NOTE: manual_result_buffer_ is accessed only by the manual
        // thread.
        manual_result_buffer_[stream_id]->push(item);
        spdlog::debug("[Model {}-{}][manual_output_loop]: manual_result_buffer_ size: {}", model_id_, stream_id, manual_result_buffer_[stream_id]->size());
    }

    // done
    spdlog::debug("Model {}: manual output loop thread done", model_id_);
    out_loop_done_.store(true);
    ofmap_queue_->notify();
}

void MxModel::_model_manual_start()
{
    std::lock_guard<std::mutex> lock(manual_run_mutex);

    if (model_manual_run.load()) {
        return;
    }

    // init such as ifmap_queue, freelists
    _init_pipeline_vars(true /* is_manual */);

    // NOTE: model_manual_run flag must be set before starting manual loops
    model_manual_run.store(true);

    // start loops (one thread per loop)
    in_loop_thread_ = new std::thread(&MxModel::manual_input_loop, this);
    out_loop_thread_ = new std::thread(&MxModel::manual_output_loop, this);

    spdlog::debug("Model {}: manual model start running", model_id_);
}

bool MxModel::model_manual_send(std::vector<float*> user_ifmaps, int stream_id, int32_t timeout)
{
    // start manual model if not started yet
    _model_manual_start();

    // init result buffer for this stream
    manual_result_buffer_.init(stream_id);

    IomapItem* item = ifmap_freelist_->pop();
    item->stream_id = stream_id;  // set stream_id

    spdlog::debug("[Model {}][manual_send]: set data for stream {}", model_id_, stream_id);

    if (!pre_model_path_.empty()) {

        // TODO: implement _pre_copy
        // _pre_copy()

        // copy data from user to internal `pre` feature map
        for (int i = 0; i < (int)item->ifmaps.size(); i++) {
            item->pre_ifmaps[i]->set_data(user_ifmaps[i]);
        }

        // infer with pre model
        _pre_inference(item);
    }
    else {
        // copy data from user to internal feature map
        for (int i = 0; i < (int)item->ifmaps.size(); i++) {
            item->ifmaps[i]->set_data(user_ifmaps[i]);
        }
    }

    spdlog::debug("[Model {}][manual_send]: push item into ifmap_queue for stream {}", model_id_, stream_id);

    if (ifmap_queue_->push_timeout(item, timeout) == false) {
        // return the item to freelist if push to queue failed
        ifmap_freelist_->push(item);
        return false;
    }

    spdlog::debug("[Model {}][manual_send]: finished one data send for stream {}", model_id_, stream_id);

    return true;
}
bool MxModel::model_manual_receive(std::vector<float*> &user_ofmaps, int stream_id, int32_t timeout)
{
    // start manual model if not started yet
    _model_manual_start();

    // init result buffer for this stream
    manual_result_buffer_.init(stream_id);

    IomapItem* item = nullptr;

    if (timeout > 0) {
        // wait till input is sent to this stream or timeout
        if (manual_result_buffer_[stream_id]->pop_timeout(item, timeout) == false) {
            return false;
        }
    }
    else {
        // wait indefinitely till input is sent to this stream
        spdlog::debug("[Model {}-{}][recv]: manual_result_buffer_ size: {}", model_id_, stream_id, manual_result_buffer_[stream_id]->size());
        manual_result_buffer_[stream_id]->pop(item);
    }

    spdlog::debug("[Model {}][manual_receive]: get item from result buffer for stream {}", model_id_, stream_id);

    if (!post_model_path_.empty()) {
        // infer with post model
        _post_inference(item);

        // copy data from internal `post` feature map to user
        for (int i = 0; i < (int)item->ofmaps.size(); i++) {
            item->post_ofmaps[i]->get_data(user_ofmaps[i]);
        }
    }
    else {
        // copy data from internal feature map to user
        for (int i = 0; i < (int)item->ofmaps.size(); i++) {
            item->ofmaps[i]->get_data(user_ofmaps[i]);
        }
    }


    // return the item to freelist
    ofmap_freelist_->push(item);

    spdlog::debug("[Model {}][manual_receive]: finished one data receive for stream {}", model_id_, stream_id);

    return true;
}
bool MxModel::manual_run(std::vector<float*> user_ifmaps, std::vector<float*> &user_ofmaps, int stream_id, int32_t timeout)
{
    if (!this->model_manual_send(user_ifmaps, stream_id, timeout)) {
        return false;
    }
    if (!this->model_manual_receive(user_ofmaps, stream_id, timeout)) {
        return false;
    }
    return true;
}
