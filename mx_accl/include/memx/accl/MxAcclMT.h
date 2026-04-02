// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_ACCLMT_H
#define MX_ACCLMT_H

#pragma once
#include <string>
#include <stdint.h>
#include <thread>

#include <memx/accl/MxModel.h>
#include <memx/accl/MxAcclBase.h>
#include <memx/accl/dfp.h>
#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/path.h>
#include <memx/accl/DeviceManager.h>

using namespace std;

namespace MX
{
namespace Runtime
{
class MxAcclMT : public MxAcclBase
{

  public:

    /**
    * @brief All-in-one constructor that initializes the MxAcclMT object in manual threading mode, connects the provided DFP,
    *        and applies runtime configuration options.
    *
    * This constructor sets up the accelerator by loading the specified DFP, configuring device assignments,
    * scheduling behavior, and communication options. It builds on the functionality of the base class `MxAcclBase`.
    *
    * @param dfp_path
    * Path to the compiled DFP (Dataflow Program) file. Accepts `std::filesystem::path`, `const char*`, or `std::string`.
    *
    * @param device_ids_to_use
    * List of MXA device IDs to use for execution. Specify `{-1}` to indicate that all available devices should be used.
    * Default is `{0}`.
    *
    * @param use_model_shape
    * A pair of boolean flags `{input, output}` indicating whether to preserve the original model input/output shapes (`true`)
    * or use the shape interpreted by the MXA runtime (`false`). Default is `{true, true}`.
    *
    * @param local_mode
    * If `true`, the DFP will be executed in local (non-shared) mode. This may offer better performance in single-process
    * environments, but it disables multi-process and multi-DFP coordination. Default is `false`.
    *
    * @param sched_options
    * Scheduler options used during shared execution mode. Configures frame limits, timeouts, and queue sizes.
    * Default is `{frame_limit = 20, timeout = 250, input_queue_size = 16, output_queue_size = 21}`.
    * See @ref MX::RPC::SchedulerOptions for detailed field descriptions.
    *
    * @param client_options
    * Client-side behavior options such as FPS smoothing and throttling.
    * Default is `{smoothing = false, fps_target = 0}`.
    * See @ref MX::RPC::ClientOptions for detailed field descriptions.
    *
    * @param server_addr
    * Address of the manager server. On Linux, this is typically a UNIX socket path (e.g., `"/run/mxa_manager/"`).
    * On Windows or remote setups, this should be an IP address such as `"localhost"`.
    *
    * @param server_port_base
    * Base port number used by the manager server for communication. Applies to both socket filenames and IP-based addressing.
    * Default is `10000`.
    *
    * @param ignore_server_
    * (Advanced) If set to `true`, the object will bypass the manager server and run the DFP in local mode.
    * @warning
    * Setting `ignore_server_` to `true` disables coordination with other processes and may result in device conflicts
    * if multiple clients or containers access the same device concurrently. Use with caution.
    *
    */
    MEMX_API_EXPORT MxAcclMT(const std::filesystem::path &dfp_path,
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
    * @brief All-in-one constructor that initializes the MxAcclMT object in manual threading mode, connects the provided DFP
    *        from a raw byte array, and applies runtime configuration options.
    *
    * This constructor is intended for cases where the DFP (Dataflow Program) is already loaded in memory
    * and represented as a raw byte buffer. It sets up the accelerator by configuring device usage,
    * scheduling options, and client/server communication in a single step.
    *
    * @param dfp_bytes
    * Pointer to the raw, in-memory representation of the compiled DFP (`uint8_t*`).
    * This buffer must remain valid and accessible throughout the initialization phase.
    *
    * @param dfp_byte_size
    * Size of the DFP data in bytes.
    *
    * @param device_ids_to_use
    * List of MXA device IDs to use for execution. Specify `{-1}` to indicate that all available devices should be used.
    * Default is `{0}`.
    *
    * @param use_model_shape
    * A pair of boolean flags `{input, output}` indicating whether to preserve the original model input/output shapes (`true`)
    * or use the shape interpreted by the MXA runtime (`false`). Default is `{true, true}`.
    *
    * @param local_mode
    * If `true`, the DFP will be executed in local (non-shared) mode. This may offer better performance in single-process
    * environments but disables multi-process and multi-DFP coordination. Default is `false`.
    *
    * @param sched_options
    * Scheduler options used during shared execution mode. Configures frame limits, timeouts, and queue sizes.
    * Default is `{frame_limit = 20, timeout = 250, input_queue_size = 16, output_queue_size = 21}`.
    * See @ref MX::RPC::SchedulerOptions for detailed field descriptions.
    *
    * @param client_options
    * Client-side behavior options such as FPS smoothing and throttling.
    * Default is `{smoothing = false, fps_target = 0}`.
    * See @ref MX::RPC::ClientOptions for detailed field descriptions.
    *
    * @param server_addr
    * Address of the manager server. On Linux, this is typically a UNIX socket path (e.g., `"/run/mxa_manager/"`).
    * On Windows or remote setups, this should be an IP address such as `"localhost"`.
    *
    * @param server_port_base
    * Base port number used by the manager server for communication. Applies to both socket filenames and IP-based addressing.
    * Default is `10000`.
    *
    * @param ignore_server_
    * (Advanced) If set to `true`, the object will bypass the manager server and run the DFP in local mode.
    * Default is `false`.
    *
    * @warning
    * Setting `ignore_server_` to `true` disables coordination with other processes and may result in device conflicts
    * if multiple clients or containers access the same device concurrently. Use with caution.
    *
    * @warning
    * The memory pointed to by `dfp_bytes` must remain valid throughout object construction. Passing a dangling or
    * temporary buffer may lead to undefined behavior.
    */
    MEMX_API_EXPORT MxAcclMT(uint8_t* dfp_bytes, size_t dfp_byte_size,
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
    MEMX_API_EXPORT ~MxAcclMT();

    /**
    * @brief Sends input data to the accelerator in user-threaded (Manual Threading) mode.
    *
    * This method submits input data to a specific model and stream instance using user-managed threading.
    * It optionally waits for buffer availability based on the provided timeout.
    *
    * @param in_data
    * A vector of pointers to input data buffers. Each `float*` corresponds to a model input port.
    *
    * @param model_id
    * Index of the model the input data is targeted to.
    *
    * @param stream_id
    * Index of the stream associated with this input. This must match the stream ID used during stream setup.
    *
    * @param timeout
    * Maximum time to wait (in milliseconds) for the input to be accepted.
    * A value of `0` indicates that the call will block indefinitely until space is available.
    * Default is `0`.
    *
    * @return
    * Returns `true` if the input was accepted and inference started successfully.
    * Returns `false` if the operation timed out before data could be submitted.
    */
    MEMX_API_EXPORT bool send_input(std::vector<float*> in_data, int model_id, int stream_id, int32_t timeout = 0);

    /**
    * @brief Receives output data from the accelerator in user-threaded (manual threading) mode.
    *
    * This method retrieves output feature maps from the accelerator for a given model and stream.
    * It is intended for use in `MxAcclMT` mode, where the user manages threading and stream lifecycle manually.
    * The call will block until output is available or the specified timeout is reached.
    *
    * @param out_data
    * A vector of pointers where the output data will be written. Each `float*` must point to a
    * preallocated buffer matching the size of the corresponding model output.
    *
    * @param model_id
    * Index of the model from which output data is to be retrieved.
    *
    * @param stream_id
    * Index of the stream associated with this output. It can be any user-defined stream ID that
    * the application uses to manage context.
    *
    * @param timeout
    * Maximum time to wait (in milliseconds) for output data to become available.
    * A value of `0` indicates that the call will block indefinitely until data is ready.
    * Default is `0`.
    *
    * @return
    * Returns `true` if output was successfully received.
    * Returns `false` if the operation timed out before data was available.
    */
    MEMX_API_EXPORT bool receive_output(std::vector<float*> &out_data, int model_id, int stream_id, int32_t timeout = 0);

    /**
    * @brief Runs inference on a batch of inputs in user-threaded (manual threading) mode.
    *
    * This method sends input data to the accelerator and waits for the corresponding outputs in a single call.
    * It is designed for use in `MxAcclMT` mode, where the user manages threading and stream assignment manually.
    * The call blocks until inference completes or the specified timeout expires.
    *
    * @param in_data
    * A vector of pointers to input data buffers. Each `float*` corresponds to one input port of the model.
    *
    * @param out_data
    * A vector of pointers where the output data will be written. Each `float*` must point to a
    * preallocated buffer matching the expected size of the corresponding model output.
    *
    * @param model_id
    * Index of the model on which inference should be performed.
    *
    * @param stream_id
    * Index of the stream used to run inference. This can be any user-defined ID that the application uses to track context.
    *
    * @param timeout
    * Maximum wait time (in milliseconds) for inference to complete.
    * A value of `0` indicates that the function blocks indefinitely until results are ready.
    * Default is `0`.
    *
    * @return
    * Returns `true` if inference was successful and output was received.
    * Returns `false` if the operation timed out.
    *
    * @note
    * This method combines `send_input()` and `receive_output()` into one operation yet send and receive to the accelerator is performed asynchronously internally.
    */
    MEMX_API_EXPORT bool run(std::vector<float*> in_data, std::vector<float*> &out_data, int model_id, int stream_id, int32_t timeout = 0);

  private:
    std::mutex manual_run_mutex;

}; // MxAcclMT
} // namespace Runtime
} // namespace MX

#endif
