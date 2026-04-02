// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_ACCL_BASE_H
#define MX_ACCL_BASE_H

#pragma once
#include <string>
#include <stdint.h>
#include <thread>
#include <map>
#include <atomic>
#include <array>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <memx/accl/DFPRunner.h>
#include <memx/accl/MxModel.h>
#include <memx/accl/DeviceManager.h>
#include <memx/accl/dfp.h>
#include <memx/accl/utils/id_tracker.h>
#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/path.h>
#include <memx/accl/utils/mxTypes.h>
#include <memx/accl/utils/locked_var.h>

using namespace std;
using namespace MX::Utils;
using namespace MX::RPC;

namespace MX
{
namespace Runtime
{

/**
 * @brief MxAcclBase class is the base class for MxAccl. It provides the basic functionality to connect to a DFP.
 * It is not intended to be used directly, but rather as a base class for MxAccl and MxAcclMT.
 */
class MxAcclBase
{
  public:

    /**
     * @brief The "all-in-one" Constructor that inits the object, connects the provided DFP, and sets options.
     *
     * @param dfp_path Path to the DFP file. char* and String types can also be passed.
     * @param device_ids_to_use IDs of MXA devices this object will use. Set to {-1} to mean "all".
     * @param use_model_shape Set of {input, output} bools to indicate whether to exactly match original model shape, or the shape used by the MXA.
     * @param local_mode If true, the DFP will be run in Local mode, which may be faster for some applications but doesn't support simultaneous use.
     * @param sched_options Scheduler options for the DFP. Default is {frame_limit = 20, timeout = 250, input_queue_size = 16, output_queue_size = 21}.
     * @param client_options Client options for the DFP. Default is {smooth_fps = false, smooth_fps_target = 0}.
     * @param server_addr Socket file path or IP address of the server. Default is "/run/mxa_manager/" (socketfile, Linux) or "localhost" (IP, Windows).
     * @param server_port_base Base port number for the server. Default is 10000. Applies to both socket filenames and IP addresses.
     * @param ignore_server_ (ADVANCED) If true, the server connection is ignored and the DFP is run in Local mode, without consideration for other processes on the system. Will lead to crashes if multiple objects / processes / containers try to use the same device.
    */
    MEMX_API_EXPORT MxAcclBase(const std::filesystem::path &dfp_path,
               std::vector<int> device_ids_to_use = {0},
               std::array<bool, 2> use_model_shape = {true, true},
               bool local_mode = false,
               SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
               ClientOptions client_options = {false, 0},
               std::string server_addr = "/run/mxa_manager/",
               unsigned int server_port_base = 10000,
               bool ignore_server_ = false);

    /**
     * @brief The "all-in-one" Constructor that inits the object, connects the provided DFP (from bytes array), and sets options.
     *
     * @param dfp_bytes Raw uint8_t* pointer to loaded DFP file data.
     * @param device_ids_to_use IDs of MXA devices this object will use. Set to {-1} to mean "all".
     * @param use_model_shape Set of {input, output} bools to indicate whether to exactly match original model shape, or the shape used by the MXA.
     * @param local_mode If true, the DFP will be run in Local mode, which may be faster for some applications but doesn't support simultaneous use.
     * @param sched_options Scheduler options for the DFP. Default is {frame_limit = 20, timeout = 250, input_queue_size = 16, output_queue_size = 21}.
     * @param client_options Client options for the DFP. Default is {smooth_fps = false, smooth_fps_target = 0}.
     * @param server_addr Socket file path or IP address of the server. Default is "/run/mxa_manager/" (socketfile, Linux) or "localhost" (IP, Windows).
     * @param server_port_base Base port number for the server. Default is 10000. Applies to both socket filenames and IP addresses.
     * @param ignore_server_ (ADVANCED) If true, the server connection is ignored and the DFP is run in Local mode, without consideration for other processes on the system. Will lead to crashes if multiple objects / processes / containers try to use the same device.
    */
    MEMX_API_EXPORT MxAcclBase(uint8_t* dfp_bytes,
               size_t dfp_byte_size,
               std::vector<int> device_ids_to_use = {0},
               std::array<bool, 2> use_model_shape = {true, true},
               bool local_mode = false,
               SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
               ClientOptions client_options = {false, 0},
               std::string server_addr = "/run/mxa_manager/",
               unsigned int server_port_base = 10000,
               bool ignore_server_ = false);

    /**
     * @brief Constructor for MxAcclBase class. It initializes the MxAcclBase object with the given server IP and port info, etc., and creates a DeviceManager object.
     *
     * @param server_addr Socket file path or IP address of the server.
     * @param server_port_base Base port number for the server. Applies to both socket filenames and IP addresses.
     * @param ignore_server_ (ADVANCED) If true, the server connection is ignored and the DFP is run in Local mode, without consideration for other processes on the system. Will lead to crashes if multiple objects / processes / containers try to use the same device.
    */
    MxAcclBase(std::string server_addr, unsigned int server_port_base, bool ignore_server_ = false);

    // cleans up any DFPs and models that are still open
    ~MxAcclBase();

    /**
     * @brief Get number of models in the compiled DFP
     *
     * @return Number of models
     */
    MEMX_API_EXPORT int get_num_models();

    /**
     * @brief Get number of chips the dfp is compiled for
     *
     * @return Number of chips
     */
    MEMX_API_EXPORT int get_dfp_num_chips();

    /**
     * @brief get information of a particular model such as number of in out featureMaps and in out layer names
     * @param model_id model ID or the index for the required information
     * @return if valid model_id then MxModelInfo model_info with necessary information else throw runtime error invalid model_id
    */
    MEMX_API_EXPORT MX::Types::MxModelInfo get_model_info(int model_id = 0) const;

    /**
     * @brief get information of the pre-processing model set to a particular model such as number of in out featureMaps and their sizes and shapes
     * @param model_id model ID or the index for the required information
     * @return if valid model_id then MxModelInfo model_info with necessary information else throw runtime error invalid model_id
    */
    MEMX_API_EXPORT MX::Types::MxModelInfo get_pre_model_info(int model_id = 0) const;

    /**
     * @brief get information of the post-processing model set to a particular model such as number of in out featureMaps and their sizes and shapes
     * @param model_id model ID or the index for the required information
     * @return if valid model_id then MxModelInfo model_info with necessary information else throw runtime error invalid model_id
    */
    MEMX_API_EXPORT MX::Types::MxModelInfo get_post_model_info(int model_id = 0) const;

    /**
     * @brief Connect the information of the post-processing model that has been cropped by the neural compiler
     *
     * @param post_model_path  Abosulte path of the post-processing model. (Can be onnx/tflite etc)
     * @param model_id The index of model for which the post-processing is intended to be connected to. The default is set to 0
     * @param post_size_list If the output of the post-processing has a variable size or if the ouput sizes
     * are not deduced, the maximum possible sizes of the output need to be passed. The default is an empty vector.
    */
    MEMX_API_EXPORT void connect_post_model(std::filesystem::path post_model_path, int model_id = 0,
                                            const std::vector<size_t> &post_size_list = {});

    /**
     * @brief Connect the information of the pre-processing model that has been cropped by the neural compiler
     *
     * @param pre_model_path  Abosulte path of the pre-processing model. (Can be onnx/tflite etc)
     * @param model_id The index of model for which the post-processing is intended to be connected to. The default is set to 0
    */
    MEMX_API_EXPORT void connect_pre_model(std::filesystem::path pre_model_path, int model_id = 0);

    /**
     * @brief Configure multi-threaded FeatureMap data conversion using the given number of threads.
     * Conversion multithreading is mainly intended for high FPS single-stream scenarios, or userThreading mode.
     * In multi-stream autoThreading scenarios, this option should not be necessary, and may even
     * degrade performance due to increased CPU load.
     *
     * @param num_threads Number of worker threads for FeatureMaps. Use >= 2 to enable. Values < 2 disable.
     * @param model_id Index of model to enable the feature  The default is set to 0
    */
    MEMX_API_EXPORT void set_parallel_fmap_convert(int num_threads, int model_id = 0);

    /**
       * @brief Checks if power consumption data can be retrieved for the connected modules.
       *
       * @return true if power consumption data is available, false otherwise.
     */
    MEMX_API_EXPORT bool can_get_power_consumption(int device_id = 0);

    /**
       * @brief Retrieves the current power consumption of the specified device.
       *
       * @return The current power consumption value (in milliwatts) of the device.
     */
    MEMX_API_EXPORT float get_power(int device_id = 0);

    /**
       * @brief Retrieves the current Pressure ("Throughput Utilization") of the specified device.
       * @note Pressure represents how "full" the MXA's pipeline is.  
       * This does NOT represent things like Core utilization or % of maximum FPS.
       * 
       * @see See value descriptions here: MX::Types::Pressure
       *
       * @return The current Pressure value of the device.
     */
    MEMX_API_EXPORT MX::Types::Pressure get_pressure(int device_id = 0);

    /**
       * @brief Retrieves the current maximum temperature of the specified device.
       *
       * @return The current maximum temperature value (in degrees Celsius) of the device.
     */
    MEMX_API_EXPORT float get_max_temperature(int device_id = 0);

    /**
       * @brief Retrieves temperatures of each chip on the specified device.
       *
       * @return A reference to a vector<vector> containing the current temperature values (in degrees Celsius) of each chip on the device.
     */
    MEMX_API_EXPORT std::vector<float> get_chip_temperatures(int device_id = 0);

    /**
       * @brief Sets the operating frequency of the device.
       *
       * @note This function must be called before invoking `connect_dfp()`.
       *       Calling it after `connect_dfp()` has will throw runtime error.
       *
       * @param device_id Device to set the power mode for.
       * @param freq_option The desired frequency option. Defaults to 600 MHz if not specified.
       * @return true if the frequency was successfully set, false otherwise.
     */
    MEMX_API_EXPORT bool set_operating_frequency(int device_id,
            MX::Types::MxFrequencyOption freq_option = MX::Types::MxFrequencyOption::FREQ_USE_CONF);

    /**
       * @brief Returns True if we have any valid DFPRunners, or if ignore_server_ is set to true then we check if the device manager has > 0 devices.
       *
     */
    MEMX_API_EXPORT bool is_ready();

    /**
       * @brief Returns the list of converted device IDs that this MxAccl object is using.
       *
       * @return A vector of integers representing the device IDs in use (after conversion).
     */
    MEMX_API_EXPORT std::vector<int> get_converted_device_ids_to_use() const;

    MxModel* get_model(int model_id) const;

  protected:
    /**
     * @brief Connect a dfp to MxAccl object. Currently only one connect_dfp per MxAccl object is allowed.
     *
     * @param file_path Absolute path of DFP file. char* and String types can also be passed.
     * @param device_ids_to_use IDs of MXA devices this process intends to use. takes in a vector of IDs and will return an error if an empty vector is passed
     * @param local_mode If true, the DFP will be run in local mode. Default is true.
     * @param options Scheduler options for the DFP. Default is {frame_limit = 20, timeout = 250, input_queue_size = 16, output_queue_size = 21}.
     * @param ClientOptions Client options for the DFP. Default is {false, 0}.
     *
     * @return dfp_id which is later to be passed in connect_stream function to specify that specific stream to a dfp
     */
    MEMX_API_EXPORT int connect_dfp(const std::filesystem::path dfp_path, std::vector<int> device_ids_to_use = {0},
                                    std::array<bool, 2> use_model_shape = {true, true},
                                    bool local_mode = false,
                                    SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
                                    ClientOptions client_options = {false, 0});

    /**
     * @brief Connect a dfp as bytes to MxAccl object. Currently only one connect_dfp per MxAccl object is allowed.
     *
     * @param dfp_bytes Raw uint8_t* pointer to DFP data
     * @param dfp_byte_size Size of DFP data in bytes
     * @param device_ids_to_use IDs of MXA devices this process intends to use. takes in a vector of IDs and will return an error if an empty vector is passed
     * @param local_mode If true, the DFP will be run in local mode. Default is true.
     * @param options Scheduler options for the DFP. Default is {frame_limit = 20, timeout = 250, input_queue_size = 16, output_queue_size = 21}.
     *
     * @return dfp_id which is later to be passed in connect_stream function to specify that specific stream to a dfp
     */
    MEMX_API_EXPORT int connect_dfp(uint8_t* dfp_bytes, size_t dfp_byte_size, std::vector<int> device_ids_to_use = {0},
                                    std::array<bool, 2> use_model_shape = {true, true},
                                    bool local_mode = false,
                                    SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
                                    ClientOptions client_options = {false, 0});

    // gets model or throws runtime error if not found
    MxModel* get_model_or_throw(int model_id, const std::string &class_name, const std::string &func_name) const;

    // device manager for locally-managed devices
    MX::Runtime::DeviceManager*  device_manager;

    // map of device to dfp_id
    std::map<int, int>          device_to_dfp_id_map;

    // DFP ID generator/tracker
    DFPRunner* dfp_runner = nullptr;
    DfpIDTracker dfp_id_tracker;

    // server connection info
    bool         ignore_server_;
    std::string  server_addr_;
    unsigned int server_port_base_;


    // when in local mode, make a thread to periodically
    // poll and update an average pressure value for each
    // device. then get_pressure will just return the average
    void pressure_thread_func(std::vector<int> device_ids);
    std::thread* pressure_thread = nullptr;
    std::vector<std::deque<float>> pressure_history;
    std::vector<float> pressure_avgs;
    std::atomic_bool pressure_thread_running;
    const int pressure_poll_interval_ms = 500;
    const unsigned int pressure_history_len = 4; // 2 seconds

};
} // namespace Runtime
} // namespace MX

#endif
