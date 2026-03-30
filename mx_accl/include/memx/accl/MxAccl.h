// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_ACCL_H
#define MX_ACCL_H

#pragma once
#include <string>
#include <stdint.h>
#include <thread>
#include <map>
#include <unordered_map>
#include <mutex>
#include <utility>

#include <memx/accl/MxModel.h>
#include <memx/accl/MxAcclBase.h>
#include <memx/accl/dfp.h>
#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/path.h>
#include <memx/accl/DeviceManager.h>
#include <memx/accl/utils/mxTypes.h>

using namespace std;

namespace MX
{
namespace Runtime
{
class MxAccl : public MxAcclBase
{
  public:

    /**
    * @brief All-in-one constructor that initializes the MxAccl object, loads the provided DFP,
    *        and applies runtime configuration options.
    *
    * This constructor streamlines the setup of a MemryX accelerator instance by combining DFP
    * loading, device selection, scheduler setup, and client/server configuration into a single step.
    *
    * @param dfp_path
    * Path to the compiled DFP (Dataflow Program) file. Accepts `std::filesystem::path`, `const char*`, or `std::string`.
    *
    * @param device_ids_to_use
    * List of MXA device IDs to use for execution. Specify `{-1}` to indicate that all available devices should be used.
    * Default is `{0}`.
    *
    * @param use_model_shape
    * A pair of boolean flags `{input, output}` indicating whether to preserve the original model input/output shapes (`true`),
    * or to use the shape as interpreted by the MXA runtime (`false`). Default is `{true, true}`.
    *
    * @param local_mode
    * If set to `true`, the DFP runs in local (non-shared) mode. This mode may improve performance for single-process
    * applications but does not support multi-process or multi-DFP usage. Default is `false`.
    *
    * @param sched_options
    * Runtime scheduler configuration used when operating in shared mode. Includes frame limits, timeouts,
    * and queue sizes. Default is `{frame_limit = 20, timeout = 250, input_queue_size = 16, output_queue_size = 21}`.
    * See @ref MX::RPC::SchedulerOptions for detailed field descriptions.
    *
    * @param client_options
    * Client-specific execution options such as FPS smoothing and target frame rate.
    * Default is `{smoothing = false, fps_target = 0}`.
    * See @ref MX::RPC::ClientOptions for detailed field descriptions.
    *
    * @param server_addr
    * Server address or UNIX socket file path for manager communication.
    * On Linux, the default is `"/run/mxa_manager/"` (UNIX socket). On Windows or remote setups, use an IP address (e.g., `"localhost"`).
    *
    * @param server_port_base
    * Base port number used for socket or IP-based communication. Applies to both socket filenames and IP addresses.
    * Default is `10000`.
    *
    * @param ignore_server_
    * (Advanced) If set to `true`, the MxAccl instance will ignore the manager server and operate in local mode only.
    * @warning
    * Setting `ignore_server_` to `true` disables coordination with other processes and may result in device conflicts
    * if multiple clients or containers access the same device concurrently. Use with caution.
    */
    MEMX_API_EXPORT MxAccl(const std::filesystem::path &dfp_path,
                           std::vector<int> device_ids_to_use = {0},
                           std::array<bool, 2> use_model_shape = {true, true},
                           bool local_mode = false,
                           SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
                           ClientOptions client_options = {false, 0},
                           std::string server_addr = "/run/mxa_manager/",
                           unsigned int server_port_base = 10000,
                           bool ignore_server_ = false) : MxAcclBase(dfp_path, device_ids_to_use,
                                       use_model_shape, local_mode, sched_options, client_options, server_addr,
                                       server_port_base, ignore_server_) {}

    /**
    * @brief All-in-one constructor that initializes the MxAccl object, loads the provided DFP,
    *        and applies runtime configuration options.
    *
    * This constructor streamlines the setup of a MemryX accelerator instance by combining DFP
    * loading, device selection, scheduler setup, and client/server configuration into a single step.
    *
    * @param dfp_bytes
    * Raw `uint8_t*` pointer to memory containing the already-loaded DFP file data.
    *
    * @param dfp_byte_size
    * Size of the DFP data in bytes.
    *
    * @param device_ids_to_use
    * List of MXA device IDs to use for execution. Specify `{-1}` to indicate that all available devices should be used.
    * Default is `{0}`.
    *
    * @param use_model_shape
    * A pair of boolean flags `{input, output}` indicating whether to preserve the original model input/output shapes (`true`),
    * or to use the shape as interpreted by the MXA runtime (`false`). Default is `{true, true}`.
    *
    * @param local_mode
    * If set to `true`, the DFP runs in local (non-shared) mode. This mode may improve performance for single-process
    * applications but does not support multi-process or multi-DFP usage. Default is `false`.
    *
    * @param sched_options
    * Runtime scheduler configuration used when operating in shared mode. Includes frame limits, timeouts,
    * and queue sizes. Default is `{frame_limit = 20, timeout = 250, input_queue_size = 16, output_queue_size = 21}`.
    * See @ref MX::RPC::SchedulerOptions for detailed field descriptions.
    *
    * @param client_options
    * Client-specific execution options such as FPS smoothing and target frame rate.
    * Default is `{smoothing = false, fps_target = 0}`.
    * See @ref MX::RPC::ClientOptions for detailed field descriptions.
    *
    * @param server_addr
    * Server address or UNIX socket file path for manager communication.
    * On Linux, the default is `"/run/mxa_manager/"` (UNIX socket). On Windows or remote setups, use an IP address (e.g., `"localhost"`).
    *
    * @param server_port_base
    * Base port number used for socket or IP-based communication. Applies to both socket filenames and IP addresses.
    * Default is `10000`.
    *
    * @param ignore_server_
    * (Advanced) If set to `true`, the MxAccl instance will ignore the manager server and operate in local mode only.
    * @warning
    * Setting `ignore_server_` to `true` disables coordination with other processes and may result in device conflicts
    * if multiple clients or containers access the same device concurrently. Use with caution.
    */
    MEMX_API_EXPORT MxAccl(uint8_t* dfp_bytes,
                           size_t dfp_byte_size,
                           std::vector<int> device_ids_to_use = {0},
                           std::array<bool, 2> use_model_shape = {true, true},
                           bool local_mode = false,
                           SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
                           ClientOptions client_options = {false, 0},
                           std::string server_addr = "/run/mxa_manager/",
                           unsigned int server_port_base = 10000,
                           bool ignore_server_ = false) : MxAcclBase(dfp_bytes, dfp_byte_size, device_ids_to_use,
                                       use_model_shape, local_mode, sched_options, client_options, server_addr,
                                       server_port_base, ignore_server_) {}


    // dtor
    MEMX_API_EXPORT ~MxAccl();

    /**
    * @brief Connects a stream to a model using the specified input and output callback functions.
    *
    * This method registers a data stream for the given model and binds it to both input and output
    * callback functions. Streams are uniquely identified using `stream_id`. This function must be
    * called before `start()`.
    *
    * - `callback_t` is a function pointer type with the following signature:
    *   `bool foo(std::vector<const MX::Types::FeatureMap*>, int stream_id);`
    *
    * - If the input callback (`in_cb`) returns `false`, the corresponding stream is stopped. When
    *   all registered streams have stopped, `wait()` is invoked automatically.
    *
    * @param in_cb
    * Input callback function that supplies data to the model. Must match `callback_t` signature.
    *
    * @param out_cb
    * Output callback function invoked with the model’s output feature maps. Must match `callback_t` signature.
    *
    * @param stream_id
    * Unique identifier for this stream. It is passed to the callback functions to distinguish streams.
    *
    * @param model_id
    * Index of the model to which this stream is connected. Default is `0`.
    */
    MEMX_API_EXPORT void connect_stream(callback_t in_cb, callback_t out_cb, int stream_id, int model_id = 0);

    /**
    * @brief Starts execution for the specified model or models.
    *
    * This function initiates processing for the given model ID. All streams associated
    * with the model must be connected beforehand via `connect_stream()`. Use `-1` to
    * indicate that all available DFPs and models should be started.
    *
    * @pre connect_stream() must be called before this method.
    *
    * @param model_id
    * Index of the model to start. Use `-1` to start all models.
    * Default is `-1`.
    */
    MEMX_API_EXPORT void start(int model_id = -1);

    /**
    * @brief Blocks until all streams for the specified model(s) have completed execution.
    *
    * This function waits until all active input callbacks for the given model have returned `false`,
    * indicating the end of their respective streams. It should only be called after `start()` has
    * been invoked for the target model(s).
    *
    * Use `-1` to wait for all models and DFPs currently in execution.
    *
    * @param model_id
    * Index of the model to wait on. Use `-1` to wait on all active models.
    * Default is `-1`.
    */
    MEMX_API_EXPORT void wait(int model_id = -1);

    /**
    * @brief Stops execution of the specified model or models.
    *
    * **[Deprecated]** 
    * Should not use this function anymore. Now it literally does nothing,
    * just a placeholder to avoid breaking existing code.
    * 
    *
    * @param model_id
    * Index of the model to stop. Use `-1` to stop all models.
    * Default is `-1`.
    */
    MEMX_API_EXPORT void stop(int model_id = -1);


    /**
    * @brief Configures the number of worker threads for input and output streams for a given model.
    *
    * By default, the number of input and output workers is equal to the number of connected streams,
    * which typically yields maximum performance. This method allows overriding that behavior to
    * fine-tune parallelism.
    *
    * This method should be called **after** all required streams have been connected, and **before**
    * `start()` is invoked. If not explicitly set, the accelerator will operate using default worker counts.
    * @pre connect_stream() must be called before this method
    *
    * @param input_num_workers
    * Number of worker threads to assign for input processing.

    * @param output_num_workers
    * Number of worker threads to assign for output processing.

    * @param model_id
    * Index of the model to apply the worker configuration to. Default is `0`.
    */
    MEMX_API_EXPORT void set_num_workers(int input_num_workers, int output_num_workers, int model_id = 0);

    /**
    * @brief Returns the number of streams currently connected to the specified model.
    *
    * This method can be used to query how many streams have been registered for a given model
    * using `connect_stream()`. It is useful for debugging, monitoring, or configuring worker allocation.
    *
    * @param model_id
    * Index of the model to query. Default is `0`.
    *
    * @return int
    * Number of streams connected to the specified model.
    */
    MEMX_API_EXPORT int get_num_streams(int model_id = 0);

    /**
     * @brief Returns true if all streams for this model are done.
     */
    bool all_tasks_done(int model_id) const;

  private:
    void start_model(int model_id);
    void stop_model(int model_id);
    void wait_model(int model_id);
    void start_all();
    void stop_all();
    void wait_all();

};
} // namespace Runtime
} // namespace MX

#endif
