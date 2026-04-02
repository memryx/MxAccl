// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_DFP_RUNNER_H
#define MX_DFP_RUNNER_H

#pragma once
#include <string>
#include <stdint.h>
#include <thread>
#include <map>
#include <array>
#include <unordered_map>
#include <mutex>
#include <utility>

#include <memx/accl/MxModel.h>
#include <memx/accl/DeviceManager.h>
#include <memx/accl/client.h>
#include <memx/accl/dfp.h>
#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/path.h>
#include <memx/accl/utils/mxTypes.h>

using namespace std;
using namespace MX::Utils;

namespace MX
{
namespace Runtime
{


/**
 * @brief DFPRunner class is used to manage the DFP execution, either through local mode or shared mode.
 * In local mode, it creates a single client and locally manages driver calls like memx_open, etc.
 */
class DFPRunner
{
  public:
    // Constructor: initializes stuff, but also creates and connects Clients
    DFPRunner(int dfp_id, Dfp::DfpObject* dfp, const std::string &server_address,
              unsigned short base_port, bool local_mode, const std::vector<int> &device_ids_to_use,
              std::array<bool, 2> use_model_shape,
              const SchedulerOptions &sched_options, const ClientOptions &client_options, DeviceManager* dev_man, bool ignore_server);

    // Just calls the other constructor with ignore_server = false
    DFPRunner(int dfp_id, Dfp::DfpObject* dfp, const std::string &server_address,
              unsigned short base_port, bool local_mode, const std::vector<int> &device_ids_to_use,
              std::array<bool, 2> use_model_shape,
              const SchedulerOptions &sched_options, const ClientOptions &client_options, DeviceManager* dev_man);

    bool start_all();
    bool stop_all();
    bool wait_all();

    bool start_model(int model_id);
    bool stop_model(int model_id);
    bool wait_model(int model_id);

    bool is_local();
    int get_num_chips();
    std::vector<int> get_converted_device_ids_to_use() const;

    ~DFPRunner();

    // Model management
    int num_models;
    std::vector<MxModel*> models; // includes .model_run, etc. flags too
    MxModel* get_model(int model_id);

    // Init everything for local mode execution
    bool init_local();

    // Init everything for shared mode execution
    bool init_shared();

    // Close contexts, etc. for local mode
    bool close_local();

    // Close contexts, etc. for shared mode
    bool close_shared();

    // Local and Shared versions of start, stop, and wait
    bool start_local(int model_id);
    bool stop_local(int model_id);
    bool wait_local(int model_id);
    bool start_shared(int model_id);
    bool stop_shared(int model_id);
    bool wait_shared(int model_id);

    // My unique DFP ID
    int dfp_id_;

    // DFP object
    Dfp::DfpObject* dfp_;

    // Devices to use for this DFP (Local mode only)
    int num_devices_;
    std::vector<int> device_ids_to_use_;
    std::vector<int> open_contexts_;
    std::vector<int> converted_dev_ids_; // after conversion by device manager

    // Whether local or shared
    bool local_mode_;

    // Shared mode options
    SchedulerOptions sched_options_;
    ClientOptions client_options_;

    // Whether to ignore server or not
    bool ignore_server_;

    Client* get_first_client()
    {
        if (clients.empty()) {
            return nullptr;
        }
        return clients[0];
    }
    

    // for auto-autoclock RESULTS information storage per device
    typedef struct {
        bool autoclock_enabled;
        unsigned int power_limit_mw;
        MX::Types::MxFrequencyOption freq;
    } autoclock_info_t;

  private:

    // Device manager
    DeviceManager* device_manager_;
    void devman_discover(Client* client_);

    // for auto-autoclock information per device
    //   (device_id -> autoclock_info_t)
    std::map<int, autoclock_info_t> autoclock_infos;


    // Either 1 total client for Local mode, or a client for
    // each separate model for Shared mode
    std::vector<Client*> clients;

    // Server address and port
    std::string server_address_;
    unsigned short base_port_;

    std::array<bool, 2> use_model_shape_;

};


} // namespace Runtime
} // namespace MX

#endif
