// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_ACCL_CLIENT_H
#define MX_ACCL_CLIENT_H

#pragma once
#include <string>
#include <cstdint>
#include <thread>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <filesystem>

#include <memx/accl/messages.h>
#include <memx/accl/utils/featureMap.h>

using namespace MX::RPC;

namespace MX
{
namespace Runtime
{

class Client
{
  public:
    Client();
    ~Client();

    uint32_t       my_client_id;

    // Sends INIT_CONNECTION and gets a client_id
    bool init_connection(const std::string &server_address_, unsigned short base_port_);

    // Sends END_CONNECTION and closes all connections
    bool end_connection();

    // For Local Locking
    bool try_local_lock(int32_t device_id);
    bool local_lock_wait(int32_t device_id);
    bool local_unlock(int32_t device_id);

    // Sends the DFP to the server and adds myself to the client for this DFP ('s submodel)
    // NOTE: this also starts the IFMAP and OFMAP connections
    bool connect_dfp(size_t num_dfp_bytes, uint8_t* dfp_bytes, int32_t model_id,
                     const SchedulerOptions &options, const ClientOptions &client_options,
                     int32_t num_devices, const int32_t* devices_to_use);

    // Sends the feature map data to the server
    // NOTE: you must round-robin all ports for this model IN ORDER
    bool send(const uint8_t* data, size_t data_size);

    // Receives the feature map data from the server
    // NOTE: you must round-robin all ports for this model IN ORDER
    bool recv(uint8_t* data, size_t data_size);

    // for getting temp and power info from the server
    float get_avg_max_temp(int32_t device_id);
    float get_inst_max_temp(int32_t device_id);
    float get_avg_power(int32_t device_id);
    float get_inst_power(int32_t device_id);
    float get_pressure(int32_t device_id);
    std::vector<float> get_avg_temp_per_chip(int32_t device_id);
    std::vector<float> get_inst_temp_per_chip(int32_t device_id);

    // setting power mode
    bool set_power_mode(int32_t device_id, uint16_t freq_mhz);

    // for getting device info from the server
    std::vector<MX::RPC::device_info_t> get_device_infos();

  private:
    std::string    server_address;
    unsigned short base_port;

    std::map<int32_t, bool> locked_devices;
    std::mutex             m_mutex;

    void* ctrl_io_context; // will be casted to mxasio::io_context
    void* ctrl_socket;     // will be casted to mxasio::ip::tcp::socket

    void* ifmap_io_context; // will be casted to mxasio::io_context
    void* ifmap_socket;     // will be casted to mxasio::ip::tcp::socket

    void* ofmap_io_context; // will be casted to mxasio::io_context
    void* ofmap_socket;     // will be casted to mxasio::ip::tcp::socket
};


}
}

#endif // MX_ACCL_CLIENT_H
