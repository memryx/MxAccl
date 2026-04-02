// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <cstdint>
#include <thread>
#include <vector>
#include <string>
#include <filesystem>

#include "mxasio.hpp"
#include "spdlog/spdlog.h"

#include <memx/accl/client.h>
#include <memx/accl/utils/comm_sockets.h>
#include <memx/accl/utils/blocky_queue.h>

using namespace MX::Runtime;
using namespace MX::RPC;

using mxasio::ip::tcp;

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
// CTOR/DTOR
//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------

Client::Client()
{
    my_client_id = 0xDEADBEEF;
    server_address = "";
    base_port = 0;
    ctrl_io_context = nullptr;
    ctrl_socket = nullptr;
    ifmap_io_context = nullptr;
    ifmap_socket = nullptr;
    ofmap_io_context = nullptr;
    ofmap_socket = nullptr;
}

Client::~Client()
{
    if(ifmap_socket != nullptr) {
        delete static_cast<Socket*>(ifmap_socket);
        ifmap_socket = nullptr;
    }
    if(ofmap_socket != nullptr) {
        delete static_cast<Socket*>(ofmap_socket);
        ofmap_socket = nullptr;
    }
    if(ctrl_socket != nullptr) {
        delete static_cast<Socket*>(ctrl_socket);
        ctrl_socket = nullptr;
    }
    if(ctrl_io_context != nullptr) {
        delete static_cast<mxasio::io_context*>(ctrl_io_context);
        ctrl_io_context = nullptr;
    }
    if(ifmap_io_context != nullptr) {
        delete static_cast<mxasio::io_context*>(ifmap_io_context);
        ifmap_io_context = nullptr;
    }
    if(ofmap_io_context != nullptr) {
        delete static_cast<mxasio::io_context*>(ofmap_io_context);
        ofmap_io_context = nullptr;
    }
}


//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
// CONNECTION MANAGEMENT
//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------

bool Client::init_connection(const std::string &server_address_, unsigned short base_port_)
{
    base_port = base_port_;
    server_address = server_address_;

    try {

        // Check if the server address is valid
        if (server_address_.empty()) {
            spdlog::error("[Client] Server address is empty");
            return false;
        }
        if (base_port == 0) {
            spdlog::error("[Client] Base port is 0");
            return false;
        }

        // Resolve the server_address_ to an IPv4 address string, resolving any DNS names
        // to an IP address

        // Create a socket
        mxasio::io_context* io_context = new mxasio::io_context();
        Connector conn(*io_context);

        // Acquire the ctrl mutex
        std::lock_guard<std::mutex> lock(m_mutex);

        // Connect to the server's CTRL port
        Socket* socket = conn.connect(server_address_, base_port);

        // Prepare and send the basic header
        MsgHeader header;
        header.client_id = 0;
        header.msg_type = MSG_TYPE_CONN;
        socket->write(mxasio::buffer(&header, sizeof(header)));

        // Send the INIT_CONNECTION command
        MsgConnect msg_conn;
        msg_conn.cmd = INIT_CONNECTION_PROTO_2;
        socket->write(mxasio::buffer(&msg_conn, sizeof(msg_conn)));

        // Read the response header
        socket->read(mxasio::buffer(&header, sizeof(header)));
        if(header.msg_type != MSG_TYPE_STATUS) {
            delete socket;
            delete io_context;
            spdlog::error("[Client] Expected status message, got {}", msgtype2str(header.msg_type));
            return false;
        }

        // Read the status message
        MsgStatus status;
        socket->read(mxasio::buffer(&status, sizeof(status)));
        if(status.s != HERE_IS_YOUR_NEW_ID) {
            // Print special message for NO_DEVICES_IN_SYSTEM
            if (status.s == NO_DEVICES_IN_SYSTEM) {
                spdlog::error("[Client] No devices in system, please check the server");
            }
            else {
                spdlog::error("[Client] Error: {}, Data: {}", status2str(status.s), status.dat);
            }
            delete socket;
            delete io_context;
            return false;
        }

        // Now we have the client ID
        my_client_id = status.dat;

        // Set the socket and io_context ctrl pointers
        ctrl_socket = socket;
        ctrl_io_context = io_context;

    }
    catch (std::exception &e) {
        spdlog::error("[Client] Exception: {}", e.what());
        return false;
    }

    return true;
}

bool Client::end_connection()
{
    try {
        if (ctrl_socket == nullptr) {
            spdlog::error("[Client] No ctrl_socket connection to end");
            return false;
        }

        // Acquire the ctrl mutex
        std::lock_guard<std::mutex> lock(m_mutex);

        Socket* socket = static_cast<Socket*>(ctrl_socket);

        // If IFMAP/OFMAP sockets are open, close them first
        if (ifmap_socket != nullptr) {
            delete static_cast<Socket*>(ifmap_socket);
            ifmap_socket = nullptr;
        }
        if (ofmap_socket != nullptr) {
            delete static_cast<Socket*>(ofmap_socket);
            ofmap_socket = nullptr;
        }

        // Prepare the header and command
        MsgHeader header;
        header.client_id = my_client_id;
        header.msg_type = MSG_TYPE_CONN;

        MsgConnect end_command;
        end_command.cmd = END_CONNECTION;

        // Send the header and command
        socket->write(mxasio::buffer(&header, sizeof(header)));
        socket->write(mxasio::buffer(&end_command, sizeof(end_command)));

        // Read the response header
        socket->read(mxasio::buffer(&header, sizeof(header)));
        if(header.msg_type != MSG_TYPE_STATUS) {
            spdlog::error("[Client] Expected status message, got {}", msgtype2str(header.msg_type));
            return false;
        }

        // Read the status message
        MsgStatus status;
        socket->read(mxasio::buffer(&status, sizeof(status)));
        if (status.s == OK) {
            spdlog::debug("[Client] Successfully ended connection with server.");
            delete static_cast<Socket*>(ctrl_socket);
            ctrl_socket = nullptr;
            return true;
        }
        else {
            return false;
        }

    }
    catch (std::exception &e) {
        return false;
    }

    return false; // fallthrough
}



//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
// LOCAL LOCKING
//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------

bool Client::try_local_lock(int32_t device_id)
{
    try {
        if (ctrl_socket == nullptr) {
            spdlog::error("[Client] No ctrl_socket connection to try local lock");
            return false;
        }

        // Acquire the ctrl mutex
        std::lock_guard<std::mutex> lock(m_mutex);

        Socket* socket = static_cast<Socket*>(ctrl_socket);

        // Prepare the header and command
        MsgHeader header;
        header.client_id = my_client_id;
        header.msg_type = MSG_TYPE_LOCK;

        MsgLocalLock msg_lock;
        msg_lock.device_id = device_id;
        msg_lock.u0_l1_t2 = MsgLocalLock::TRYLOCK; // trylock

        // Send the header and command
        socket->write(mxasio::buffer(&header, sizeof(header)));
        socket->write(mxasio::buffer(&msg_lock, sizeof(msg_lock)));

        // Read the response header
        socket->read(mxasio::buffer(&header, sizeof(header)));
        if(header.msg_type != MSG_TYPE_STATUS) {
            spdlog::error("[Client] Expected status message, got {}", msgtype2str(header.msg_type));
            return false;
        }

        // Receive the status
        MsgStatus status;
        socket->read(mxasio::buffer(&status, sizeof(status)));
        if (status.s == OK) {
            spdlog::debug("[Client] Successfully locked device {}", device_id);
            locked_devices[device_id] = true; // mark device as locked
            return true;
        }
        else {
            spdlog::warn("[Client] Trylock was not successful for device {}: {}", device_id, status2str(status.s));
            return false;
        }

    }
    catch (std::exception &e) {
        spdlog::error("[Client] Exception: {}", e.what());
        return false;
    }

    spdlog::error("[Client] Failed to try local lock for device {}", device_id);
    return false; // fallthrough
}


bool Client::local_lock_wait(int32_t device_id)
{
    try {
        if (ctrl_socket == nullptr) {
            spdlog::error("[Client] No ctrl_socket connection to wait for local lock");
            return false;
        }

        // Acquire the ctrl mutex
        std::lock_guard<std::mutex> lock(m_mutex);

        Socket* socket = static_cast<Socket*>(ctrl_socket);

        // Prepare the header and command
        MsgHeader header;
        header.client_id = my_client_id;
        header.msg_type = MSG_TYPE_LOCK;

        MsgLocalLock msg_lock;
        msg_lock.device_id = device_id;
        msg_lock.u0_l1_t2 = MsgLocalLock::LOCK; // wait lock

        // Send the header and command
        socket->write(mxasio::buffer(&header, sizeof(header)));
        socket->write(mxasio::buffer(&msg_lock, sizeof(msg_lock)));

        // Read the response header
        socket->read(mxasio::buffer(&header, sizeof(header)));
        if(header.msg_type != MSG_TYPE_STATUS) {
            spdlog::error("[Client] Expected status message, got {}", msgtype2str(header.msg_type));
            return false;
        }

        // Receive the status
        MsgStatus status;
        socket->read(mxasio::buffer(&status, sizeof(status)));
        if (status.s == OK) {
            spdlog::debug("[Client] Successfully locked device {}", device_id);
            locked_devices[device_id] = true; // mark device as locked
            return true;
        }
        else {
            spdlog::error("[Client] Failed to lock device {}: {}", device_id, status2str(status.s));
            return false;
        }

    }
    catch (std::exception &e) {
        spdlog::error("[Client] Exception: {}", e.what());
        return false;
    }

    spdlog::error("[Client] Failed to wait for local lock for device {}", device_id);
    return false; // fallthrough
}


bool Client::local_unlock(int32_t device_id)
{
    try {
        if (ctrl_socket == nullptr) {
            spdlog::error("[Client] No ctrl_socket connection to unlock device");
            return false;
        }

        // Acquire the ctrl mutex
        std::lock_guard<std::mutex> lock(m_mutex);

        Socket* socket = static_cast<Socket*>(ctrl_socket);

        // Prepare the header and command
        MsgHeader header;
        header.client_id = my_client_id;
        header.msg_type = MSG_TYPE_LOCK;

        MsgLocalLock msg_lock;
        msg_lock.device_id = device_id;
        msg_lock.u0_l1_t2 = MsgLocalLock::UNLOCK; // unlock

        // Send the header and command
        socket->write(mxasio::buffer(&header, sizeof(header)));
        socket->write(mxasio::buffer(&msg_lock, sizeof(msg_lock)));

        // Read the response header
        socket->read(mxasio::buffer(&header, sizeof(header)));
        if(header.msg_type != MSG_TYPE_STATUS) {
            spdlog::error("[Client] Expected status message, got {}", msgtype2str(header.msg_type));
            return false;
        }

        // Receive the status
        MsgStatus status;
        socket->read(mxasio::buffer(&status, sizeof(status)));
        if (status.s == OK) {
            spdlog::debug("[Client] Successfully unlocked device {}", device_id);
            locked_devices[device_id] = false; // mark device as unlocked
            locked_devices.erase(device_id);
            return true;
        }
        else {
            spdlog::error("[Client] Failed to unlock device {}: {}", device_id, status2str(status.s));
            return false;
        }

    }
    catch (std::exception &e) {
        spdlog::error("[Client] Exception: {}", e.what());
        return false;
    }

    spdlog::error("[Client] Failed to unlock device {}", device_id);
    return false; // fallthrough
}


//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
// DFP MANAGEMENT
//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
bool Client::connect_dfp(size_t num_dfp_bytes, uint8_t* dfp_bytes, int32_t model_id,
                         const SchedulerOptions &sched_options, const ClientOptions &client_options,
                         int32_t num_devices, const int32_t* devices_to_use)
{
    int32_t* devices_to_use_arr = new int32_t[num_devices];

    try {
        if (ctrl_socket == nullptr) {
            spdlog::error("[Client] No ctrl_socket connection to send DFP");
            return false;
        }

        // Acquire the ctrl mutex
        std::lock_guard<std::mutex> lock(m_mutex);

        Socket* socket = static_cast<Socket*>(ctrl_socket);

        // Prepare the header and command
        MsgHeader header;
        header.client_id = my_client_id;
        header.msg_type = MSG_TYPE_DFP;

        MsgSubmitDfp msg_dfp;
        msg_dfp.submodel_id = model_id;

        // Set the scheduler options
        msg_dfp.time_limit = sched_options.time_limit; // Time limit in milliseconds
        msg_dfp.frame_limit = sched_options.frame_limit; // Frame limit
        msg_dfp.ifmap_queue_size = sched_options.ifmap_queue_size; // IFMAP queue size
        msg_dfp.ofmap_queue_size = sched_options.ofmap_queue_size; // OFMAP queue size
        msg_dfp.autoclock_enabled = sched_options.autoclock_enabled; // Auto clocking enabled
        msg_dfp.autoclock_power_limit_mw = sched_options.autoclock_power_limit_mw; // Clock power limit in mW
        msg_dfp.autoclock_check_fps_saturation = sched_options.autoclock_check_fps_saturation; // Clock check FPS saturation
        msg_dfp.autoclock_sample_interval_ms = sched_options.autoclock_sample_interval_ms; // Clock sample interval in ms
        msg_dfp.autoclock_num_samples = sched_options.autoclock_num_samples; // UpClocking number of samples

        // Set the client options
        msg_dfp.smoothing = client_options.smoothing; // Smoothing option
        msg_dfp.fps_target = client_options.fps_target; // FPS target

        msg_dfp.len_devices_to_use = num_devices; // Number of devices to use
        msg_dfp.devices_to_use = devices_to_use_arr;
        for (int i = 0; i < msg_dfp.len_devices_to_use; i++) {
            msg_dfp.devices_to_use[i] = devices_to_use[i];
        }
        msg_dfp.num_dfp_bytes = num_dfp_bytes;
        msg_dfp.dfp_bytes = dfp_bytes;

        // Send the header and command
        socket->write(mxasio::buffer(&header, sizeof(header)));
        socket->write(mxasio::buffer(&(msg_dfp.submodel_id), sizeof(msg_dfp.submodel_id)));
        socket->write(mxasio::buffer(&(msg_dfp.time_limit), sizeof(msg_dfp.time_limit)));
        socket->write(mxasio::buffer(&(msg_dfp.frame_limit), sizeof(msg_dfp.frame_limit)));
        socket->write(mxasio::buffer(&(msg_dfp.ifmap_queue_size), sizeof(msg_dfp.ifmap_queue_size)));
        socket->write(mxasio::buffer(&(msg_dfp.ofmap_queue_size), sizeof(msg_dfp.ofmap_queue_size)));
        socket->write(mxasio::buffer(&(msg_dfp.autoclock_enabled), 1));
        socket->write(mxasio::buffer(&(msg_dfp.autoclock_power_limit_mw), sizeof(msg_dfp.autoclock_power_limit_mw)));
        socket->write(mxasio::buffer(&(msg_dfp.autoclock_check_fps_saturation), 1));
        socket->write(mxasio::buffer(&(msg_dfp.autoclock_sample_interval_ms), sizeof(msg_dfp.autoclock_sample_interval_ms)));
        socket->write(mxasio::buffer(&(msg_dfp.autoclock_num_samples), sizeof(msg_dfp.autoclock_num_samples)));
        socket->write(mxasio::buffer(&(msg_dfp.smoothing), 1));
        socket->write(mxasio::buffer(&(msg_dfp.fps_target), sizeof(msg_dfp.fps_target)));
        socket->write(mxasio::buffer(&(msg_dfp.len_devices_to_use), sizeof(msg_dfp.len_devices_to_use)));
        socket->write(mxasio::buffer(msg_dfp.devices_to_use, msg_dfp.len_devices_to_use * sizeof(int32_t)));
        socket->write(mxasio::buffer(&(msg_dfp.num_dfp_bytes), sizeof(msg_dfp.num_dfp_bytes)));
        socket->write(mxasio::buffer(msg_dfp.dfp_bytes, msg_dfp.num_dfp_bytes));

        // Read the response header
        socket->read(mxasio::buffer(&header, sizeof(header)));
        if(header.msg_type != MSG_TYPE_STATUS) {
            spdlog::error("[Client] Expected status message, got {}", msgtype2str(header.msg_type));
            delete [] devices_to_use_arr;
            return false;
        }

        // Read the status message
        MsgStatus status;
        socket->read(mxasio::buffer(&status, sizeof(status)));
        if (status.s == DFP_OK_BUT_IGNORING_OPTIONS) {
            spdlog::debug("[Client] DFP OK but you weren't the first to submit this model, so we ignored SchedulerOptions");
        }
        else if (status.s == DFP_WRONG_NUMBER_OF_CHIPS) {
            spdlog::error("[Client] Error: {}. Please check that the number of chips this DFP is compiled for matches the target device(s)", status2str(status.s));
            throw std::runtime_error("DFP_WRONG_NUMBER_OF_CHIPS");
            return false;
        }
        else if (status.s != OK) {
            spdlog::error("[Client] Error: {}, Data: {}", status2str(status.s), status.dat);
            delete [] devices_to_use_arr;
            return false;
        }

        // Open the IFMAP and OFMAP sockets

        // IFMAP
        //-------
        mxasio::io_context* ifmap_io_context_ = new mxasio::io_context();
        Connector conn(*ifmap_io_context_);
        Socket* ifmap_socket_ = conn.connect(server_address, base_port + 1);

        // Prepare ifmap header
        MsgHeader ifmap_header;
        ifmap_header.client_id = my_client_id;
        ifmap_header.msg_type = MSG_TYPE_FMAP_IN_INIT;

        // Send the header
        ifmap_socket_->write(mxasio::buffer(&ifmap_header, sizeof(ifmap_header)));

        // set the void ptrs
        ifmap_io_context = ifmap_io_context_;
        ifmap_socket = ifmap_socket_;

        // OFMAP
        //-------
        mxasio::io_context* ofmap_io_context_ = new mxasio::io_context();
        Socket* ofmap_socket_ = conn.connect(server_address, base_port + 2);

        // Prepare ofmap header
        MsgHeader ofmap_header;
        ofmap_header.client_id = my_client_id;
        ofmap_header.msg_type = MSG_TYPE_FMAP_OUT_INIT;

        // Send the header
        ofmap_socket_->write(mxasio::buffer(&ofmap_header, sizeof(ofmap_header)));

        // set the void ptrs
        ofmap_io_context = ofmap_io_context_;
        ofmap_socket = ofmap_socket_;

        // Clean up
        delete [] devices_to_use_arr;

    }
    catch (std::exception &e) {
        spdlog::error("[Client] Exception: {}", e.what());
        delete [] devices_to_use_arr;

        // throw it back up so we don't print a ton of extra layers of errors
        throw std::runtime_error(e.what());

        return false;
    }

    spdlog::debug("[Client] Successfully connected to DFP and started IFMAP and OFMAP connections");
    return true;
}


//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
// SEND/RECEIVE to IFMAP/OFMAP
//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
bool Client::send(const uint8_t* data, size_t data_size)
{
    mxasio::error_code  error;
    try {
        if (UNLIKELY(ifmap_socket == nullptr)) {
            spdlog::error("[Client] No IFMAP socket connection to send data");
            return false;
        }

        Socket* socket = static_cast<Socket*>(ifmap_socket);

        // Send the data
        socket->write(mxasio::buffer(data, data_size), error);

    }
    catch (std::exception &e) {
        // if it's End of File, just do spdlog::warn
        // else it's an error so use spdlog:error
        if(error == mxasio::error::eof) {
            spdlog::warn("[Client] EOF in send(): {}", e.what());
        }
        else {
            spdlog::error("[Client] Exception in send(): {}", e.what());
        }
        return false;
    }

    return true;
}

bool Client::recv(uint8_t* data, size_t data_size)
{
    mxasio::error_code  error;
    try {
        if (UNLIKELY(ofmap_socket == nullptr)) {
            spdlog::error("[Client] No OFMAP socket connection to receive data");
            return false;
        }

        Socket* socket = static_cast<Socket*>(ofmap_socket);

        // Receive the data
        socket->read(mxasio::buffer(data, data_size), error);

    }
    catch (std::exception &e) {
        if(error == mxasio::error::eof) {
            spdlog::warn("[Client] EOF in recv(): {}", e.what());
        }
        else {
            spdlog::error("[Client] Exception in recv(): {}", e.what());
        }
        return false;
    }

    return true;
}


//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
// TEMPERATURE AND POWER QUERIES
//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------

float Client::get_avg_max_temp(int32_t device_id)
{

    if (UNLIKELY(ctrl_socket == nullptr)) {
        spdlog::error("[Client] No ctrl_socket connection to get average temperature");
        return -1000.0f;
    }

    // Acquire the ctrl mutex
    std::lock_guard<std::mutex> lock(m_mutex);

    Socket* socket = static_cast<Socket*>(ctrl_socket);

    mxasio::error_code         error;
    size_t rbytes;

    // Prepare the header and command
    MsgHeader header;
    header.client_id = my_client_id;
    header.msg_type = MSG_TYPE_GET_TEMP_POWER;

    MsgGetTempPower msg_tpow;
    msg_tpow.device_id = device_id;
    msg_tpow.type = MsgGetTempPower::TEMPERATURE;
    msg_tpow.measure_mode = MsgGetTempPower::AVERAGED;
    msg_tpow.target = MsgGetTempPower::MODULE;

    // Send the header and command
    socket->write(mxasio::buffer(&header, sizeof(header)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing header: {}", error.message());
        return -1000.0f;
    }
    socket->write(mxasio::buffer(&msg_tpow, sizeof(msg_tpow)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing MsgGetTempPower: {}", error.message());
        return -1000.0f;
    }

    // Read the response header
    socket->read(mxasio::buffer(&header, sizeof(header)));
    if(UNLIKELY(header.msg_type == MSG_TYPE_STATUS)) {

        // read the status message
        MsgStatus status;
        rbytes = socket->read(mxasio::buffer(&status, sizeof(status)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading status message: {}", error.message());
            return -1000.0f;
        }

        spdlog::error("[Client] Expected TEMP_POWER message, got a status message instead: {}", status2str(status.s));
        return -1000.0f;
    }

    // Read the MsgTempPower response
    uint32_t num_items = 0;
    rbytes = socket->read(mxasio::buffer(&num_items, sizeof(uint32_t)), error);
    if(UNLIKELY(rbytes == 0 || error || num_items != 1)) {
        spdlog::error("[Client] Error reading temperature response: {}", error.message());
        return -1000.0f;
    }

    float temp = -1000.0f;
    rbytes = socket->read(mxasio::buffer(&temp, sizeof(float)), error);
    if(UNLIKELY(rbytes == 0 || error)) {
        spdlog::error("[Client] Error reading temperature values: {}", error.message());
        return -1000.0f;
    }

    return temp;
}

float Client::get_inst_max_temp(int32_t device_id)
{

    if (UNLIKELY(ctrl_socket == nullptr)) {
        spdlog::error("[Client] No ctrl_socket connection to get average temperature");
        return -1000.0f;
    }

    // Acquire the ctrl mutex
    std::lock_guard<std::mutex> lock(m_mutex);

    Socket* socket = static_cast<Socket*>(ctrl_socket);

    mxasio::error_code         error;
    size_t rbytes;

    // Prepare the header and command
    MsgHeader header;
    header.client_id = my_client_id;
    header.msg_type = MSG_TYPE_GET_TEMP_POWER;

    MsgGetTempPower msg_tpow;
    msg_tpow.device_id = device_id;
    msg_tpow.type = MsgGetTempPower::TEMPERATURE;
    msg_tpow.measure_mode = MsgGetTempPower::INSTANT;
    msg_tpow.target = MsgGetTempPower::MODULE;

    // Send the header and command
    socket->write(mxasio::buffer(&header, sizeof(header)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing header: {}", error.message());
        return -1000.0f;
    }
    socket->write(mxasio::buffer(&msg_tpow, sizeof(msg_tpow)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing MsgGetTempPower: {}", error.message());
        return -1000.0f;
    }

    // Read the response header
    socket->read(mxasio::buffer(&header, sizeof(header)));
    if(UNLIKELY(header.msg_type == MSG_TYPE_STATUS)) {

        // read the status message
        MsgStatus status;
        rbytes = socket->read(mxasio::buffer(&status, sizeof(status)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading status message: {}", error.message());
            return -1000.0f;
        }

        spdlog::error("[Client] Expected TEMP_POWER message, got a status message instead: {}", status2str(status.s));
        return -1000.0f;
    }

    // Read the MsgTempPower response
    uint32_t num_items = 0;
    rbytes = socket->read(mxasio::buffer(&num_items, sizeof(uint32_t)), error);
    if(UNLIKELY(rbytes == 0 || error || num_items != 1)) {
        spdlog::error("[Client] Error reading temperature response: {}", error.message());
        return -1000.0f;
    }

    float temp = -1000.0f;
    rbytes = socket->read(mxasio::buffer(&temp, sizeof(float)), error);
    if(UNLIKELY(rbytes == 0 || error)) {
        spdlog::error("[Client] Error reading temperature values: {}", error.message());
        return -1000.0f;
    }

    return temp;
}

std::vector<float> Client::get_avg_temp_per_chip(int32_t device_id)
{

    if (UNLIKELY(ctrl_socket == nullptr)) {
        spdlog::error("[Client] No ctrl_socket connection to get average temperature");
        return std::vector<float>();
    }

    // Acquire the ctrl mutex
    std::lock_guard<std::mutex> lock(m_mutex);

    Socket* socket = static_cast<Socket*>(ctrl_socket);

    mxasio::error_code         error;
    size_t rbytes;
    std::vector<float> temps;

    // Prepare the header and command
    MsgHeader header;
    header.client_id = my_client_id;
    header.msg_type = MSG_TYPE_GET_TEMP_POWER;

    MsgGetTempPower msg_tpow;
    msg_tpow.device_id = device_id;
    msg_tpow.type = MsgGetTempPower::TEMPERATURE;
    msg_tpow.measure_mode = MsgGetTempPower::AVERAGED;
    msg_tpow.target = MsgGetTempPower::PER_CHIP;

    // Send the header and command
    socket->write(mxasio::buffer(&header, sizeof(header)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing header: {}", error.message());
        return std::vector<float>();
    }
    socket->write(mxasio::buffer(&msg_tpow, sizeof(msg_tpow)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing MsgGetTempPower: {}", error.message());
        return std::vector<float>();
    }

    // Read the response header
    socket->read(mxasio::buffer(&header, sizeof(header)));
    if(UNLIKELY(header.msg_type == MSG_TYPE_STATUS)) {

        // read the status message
        MsgStatus status;
        rbytes = socket->read(mxasio::buffer(&status, sizeof(status)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading status message: {}", error.message());
            return std::vector<float>();
        }

        spdlog::error("[Client] Expected TEMP_POWER message, got a status message instead: {}", status2str(status.s));
        return std::vector<float>();
    }

    // Read the MsgTempPower response
    uint32_t num_items = 0;
    rbytes = socket->read(mxasio::buffer(&num_items, sizeof(uint32_t)), error);
    if(UNLIKELY(rbytes == 0 || error)) {
        spdlog::error("[Client] Error reading temperature response: {}", error.message());
        return std::vector<float>();
    }

    temps.resize(num_items);

    for(uint32_t i = 0; i < num_items; i++) {
        float temp = -1000.0f;
        rbytes = socket->read(mxasio::buffer(&temp, sizeof(float)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading temperature values: {}", error.message());
            return std::vector<float>();
        }
        temps[i] = temp;
    }

    return temps;
}


std::vector<float> Client::get_inst_temp_per_chip(int32_t device_id)
{

    if (UNLIKELY(ctrl_socket == nullptr)) {
        spdlog::error("[Client] No ctrl_socket connection to get average temperature");
        return std::vector<float>();
    }

    // Acquire the ctrl mutex
    std::lock_guard<std::mutex> lock(m_mutex);

    Socket* socket = static_cast<Socket*>(ctrl_socket);

    mxasio::error_code         error;
    size_t rbytes;
    std::vector<float> temps;

    // Prepare the header and command
    MsgHeader header;
    header.client_id = my_client_id;
    header.msg_type = MSG_TYPE_GET_TEMP_POWER;

    MsgGetTempPower msg_tpow;
    msg_tpow.device_id = device_id;
    msg_tpow.type = MsgGetTempPower::TEMPERATURE;
    msg_tpow.measure_mode = MsgGetTempPower::INSTANT;
    msg_tpow.target = MsgGetTempPower::PER_CHIP;

    // Send the header and command
    socket->write(mxasio::buffer(&header, sizeof(header)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing header: {}", error.message());
        return std::vector<float>();
    }
    socket->write(mxasio::buffer(&msg_tpow, sizeof(msg_tpow)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing MsgGetTempPower: {}", error.message());
        return std::vector<float>();
    }

    // Read the response header
    socket->read(mxasio::buffer(&header, sizeof(header)));
    if(header.msg_type == MSG_TYPE_STATUS) {

        // read the status message
        MsgStatus status;
        rbytes = socket->read(mxasio::buffer(&status, sizeof(status)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading status message: {}", error.message());
            return std::vector<float>();
        }

        spdlog::error("[Client] Expected TEMP_POWER message, got a status message instead: {}", status2str(status.s));
        return std::vector<float>();
    }

    // Read the MsgTempPower response
    uint32_t num_items = 0;
    rbytes = socket->read(mxasio::buffer(&num_items, sizeof(uint32_t)), error);
    if(UNLIKELY(rbytes == 0 || error)) {
        spdlog::error("[Client] Error reading temperature response: {}", error.message());
        return std::vector<float>();
    }

    temps.resize(num_items);

    for(uint32_t i = 0; i < num_items; i++) {
        float temp = -1000.0f;
        rbytes = socket->read(mxasio::buffer(&temp, sizeof(float)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading temperature values: {}", error.message());
            return std::vector<float>();
        }
        temps[i] = temp;
    }

    return temps;
}

float Client::get_avg_power(int32_t device_id)
{
    if (UNLIKELY(ctrl_socket == nullptr)) {
        spdlog::error("[Client] No ctrl_socket connection to get average temperature");
        return -1000.0f;
    }

    // Acquire the ctrl mutex
    std::lock_guard<std::mutex> lock(m_mutex);

    Socket* socket = static_cast<Socket*>(ctrl_socket);

    mxasio::error_code         error;
    size_t rbytes;

    // Prepare the header and command
    MsgHeader header;
    header.client_id = my_client_id;
    header.msg_type = MSG_TYPE_GET_TEMP_POWER;

    MsgGetTempPower msg_tpow;
    msg_tpow.device_id = device_id;
    msg_tpow.type = MsgGetTempPower::POWER;
    msg_tpow.measure_mode = MsgGetTempPower::AVERAGED;
    msg_tpow.target = MsgGetTempPower::MODULE;

    // Send the header and command
    socket->write(mxasio::buffer(&header, sizeof(header)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing header: {}", error.message());
        return -1000.0f;
    }
    socket->write(mxasio::buffer(&msg_tpow, sizeof(msg_tpow)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing MsgGetTempPower: {}", error.message());
        return -1000.0f;
    }

    // Read the response header
    socket->read(mxasio::buffer(&header, sizeof(header)));
    if(UNLIKELY(header.msg_type == MSG_TYPE_STATUS)) {

        // read the status message
        MsgStatus status;
        rbytes = socket->read(mxasio::buffer(&status, sizeof(status)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading status message: {}", error.message());
            return -1000.0f;
        }

        if(LIKELY(status.s == INFO_DEVICE_DOESNT_DO_POWER)){
            spdlog::debug("[Client] Device {} does not support power measurements.", device_id);
            return -1000.0f;
        } else {
            spdlog::error("[Client] Expected TEMP_POWER message, got a status message instead: {}", status2str(status.s));
            return -1000.0f;
        }
    }

    // Read the MsgTempPower response
    uint32_t num_items = 0;
    rbytes = socket->read(mxasio::buffer(&num_items, sizeof(uint32_t)), error);
    if(UNLIKELY(rbytes == 0 || error || num_items != 1)) {
        spdlog::error("[Client] Error reading temperature response: {}", error.message());
        return -1000.0f;
    }

    float power = -1000.0f;
    rbytes = socket->read(mxasio::buffer(&power, sizeof(float)), error);
    if(UNLIKELY(rbytes == 0 || error)) {
        spdlog::error("[Client] Error reading temperature values: {}", error.message());
        return -1000.0f;
    }

    return power;
}

float Client::get_inst_power(int32_t device_id)
{
    if (UNLIKELY(ctrl_socket == nullptr)) {
        spdlog::error("[Client] No ctrl_socket connection to get average temperature");
        return -1000.0f;
    }

    // Acquire the ctrl mutex
    std::lock_guard<std::mutex> lock(m_mutex);

    Socket* socket = static_cast<Socket*>(ctrl_socket);

    mxasio::error_code         error;
    size_t rbytes;

    // Prepare the header and command
    MsgHeader header;
    header.client_id = my_client_id;
    header.msg_type = MSG_TYPE_GET_TEMP_POWER;

    MsgGetTempPower msg_tpow;
    msg_tpow.device_id = device_id;
    msg_tpow.type = MsgGetTempPower::POWER;
    msg_tpow.measure_mode = MsgGetTempPower::INSTANT;
    msg_tpow.target = MsgGetTempPower::MODULE;

    // Send the header and command
    socket->write(mxasio::buffer(&header, sizeof(header)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing header: {}", error.message());
        return -1000.0f;
    }
    socket->write(mxasio::buffer(&msg_tpow, sizeof(msg_tpow)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing MsgGetTempPower: {}", error.message());
        return -1000.0f;
    }

    // Read the response header
    socket->read(mxasio::buffer(&header, sizeof(header)));
    if(UNLIKELY(header.msg_type == MSG_TYPE_STATUS)) {

        // read the status message
        MsgStatus status;
        rbytes = socket->read(mxasio::buffer(&status, sizeof(status)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading status message: {}", error.message());
            return -1000.0f;
        }

        if(LIKELY(status.s == INFO_DEVICE_DOESNT_DO_POWER)){
            spdlog::debug("[Client] Device {} does not support power measurements.", device_id);
            return -1000.0f;
        } else {
            spdlog::error("[Client] Expected TEMP_POWER message, got a status message instead: {}", status2str(status.s));
            return -1000.0f;
        }
    }

    // Read the MsgTempPower response
    uint32_t num_items = 0;
    rbytes = socket->read(mxasio::buffer(&num_items, sizeof(uint32_t)), error);
    if(UNLIKELY(rbytes == 0 || error || num_items != 1)) {
        spdlog::error("[Client] Error reading temperature response: {}", error.message());
        return -1000.0f;
    }

    float power = -1000.0f;
    rbytes = socket->read(mxasio::buffer(&power, sizeof(float)), error);
    if(UNLIKELY(rbytes == 0 || error)) {
        spdlog::error("[Client] Error reading temperature values: {}", error.message());
        return -1000.0f;
    }

    return power;
}


float Client::get_pressure(int32_t device_id)
{
    if (UNLIKELY(ctrl_socket == nullptr)) {
        spdlog::error("[Client] No ctrl_socket connection to get_pressure from");
        return -1000.0f;
    }

    // Acquire the ctrl mutex
    std::lock_guard<std::mutex> lock(m_mutex);

    Socket* socket = static_cast<Socket*>(ctrl_socket);

    mxasio::error_code         error;
    size_t rbytes;

    // Prepare the header and command
    MsgHeader header;
    header.client_id = my_client_id;
    header.msg_type = MSG_TYPE_GET_UTILIZATION;

    // Send the header
    socket->write(mxasio::buffer(&header, sizeof(header)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing get_pressure header: {}", error.message());
        return -1000.0f;
    }
    socket->write(mxasio::buffer(&device_id, sizeof(int32_t)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing get_pressure device_id: {}", error.message());
        return -1000.0f;
    }

    // Read the response header
    socket->read(mxasio::buffer(&header, sizeof(header)));
    if(LIKELY(header.msg_type == MSG_TYPE_STATUS)) {

        // read the status message
        MsgStatus status;
        rbytes = socket->read(mxasio::buffer(&status, sizeof(status)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading status message: {}", error.message());
            return -1000.0f;
        }

        if(UNLIKELY(status.s != OK)) {
            spdlog::error("[Client] Expected OK status message, got: {}", status2str(status.s));
            return -1000.0f;
        }

        // utilization value is status.dat
        // reinterpret the u32 bits directly as float bits
        float pressure = 0.0f;
        std::memcpy(&pressure, &(status.dat), sizeof(float));

        return pressure;
    }
    else {
        spdlog::error("[Client] Expected STATUS message, got: {}", msgtype2str(header.msg_type));
        return -1000.0f;
    }
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
// SET POWER MODE
//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------

bool Client::set_power_mode(int32_t device_id, uint16_t freq_mhz)
{

    if (UNLIKELY(ctrl_socket == nullptr)) {
        spdlog::error("[Client] No ctrl_socket connection to get_pressure from");
        return false;
    }

    // Acquire the ctrl mutex
    std::lock_guard<std::mutex> lock(m_mutex);

    Socket* socket = static_cast<Socket*>(ctrl_socket);

    mxasio::error_code         error;
    size_t rbytes;

    // Prepare the header and command
    MsgHeader header;
    header.client_id = my_client_id;
    header.msg_type = MSG_TYPE_SET_POWERMODE;

    // Send the header
    socket->write(mxasio::buffer(&header, sizeof(header)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing set_power_mode header: {}", error.message());
        return false;
    }

    // send device_id and freq_mhz packet
    socket->write(mxasio::buffer(&device_id, sizeof(int32_t)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing set_power_mode device_id: {}", error.message());
        return false;
    }
    socket->write(mxasio::buffer(&freq_mhz, sizeof(uint16_t)), error);
    if(UNLIKELY(error)) {
        spdlog::error("[Client] Error writing set_power_mode freq_mhz: {}", error.message());
        return false;
    }

    // Read the response header
    socket->read(mxasio::buffer(&header, sizeof(header)));
    if(LIKELY(header.msg_type == MSG_TYPE_STATUS)) {

        // read the status message
        MsgStatus status;
        rbytes = socket->read(mxasio::buffer(&status, sizeof(status)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] set_power_mode: Error reading status reply message: {}", error.message());
            return false;
        }

        if(UNLIKELY(status.s != OK)) {
            spdlog::error("[Client] set_power_mode: Expected OK status reply message, got: {}", status2str(status.s));
            return false;
        }
    }
    else {
        spdlog::error("[Client] set_power_mode: Expected STATUS message, got: {}", msgtype2str(header.msg_type));
        return false;
    }

    return true;
}


//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
// DEVICE INFO REQUESTS
//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------

std::vector<MX::RPC::device_info_t> Client::get_device_infos()
{
    std::vector<MX::RPC::device_info_t> device_info;

    try {
        if (ctrl_socket == nullptr) {
            spdlog::error("[Client] No ctrl_socket connection to get device info");
            return device_info;
        }

        // Acquire the ctrl mutex
        std::lock_guard<std::mutex> lock(m_mutex);

        Socket* socket = static_cast<Socket*>(ctrl_socket);

        // Prepare the header and command
        MsgHeader header;
        header.client_id = my_client_id;
        header.msg_type = MSG_TYPE_GET_DEV_INFO;

        // Send the header
        socket->write(mxasio::buffer(&header, sizeof(header)));

        // Start reading the data back immediately
        int32_t all_devices_count = 0;
        mxasio::error_code         error;
        size_t rbytes = socket->read(mxasio::buffer(&all_devices_count, sizeof(int32_t)), error);
        if(UNLIKELY(rbytes == 0 || error)) {
            spdlog::error("[Client] Error reading device info count: {}", error.message());
            return device_info;
        }

        if(all_devices_count <= 0) {
            spdlog::warn("[Client] No devices found in the system");
            return device_info; // return empty vector
        }
        else {
            spdlog::debug("[Client] Found {} devices in the system", all_devices_count);
            device_info.resize(all_devices_count);
        }

        // Read the device info one by one
        for(int32_t i = 0; i < all_devices_count; i++) {

            // Read chip count
            rbytes = socket->read(mxasio::buffer(&(device_info[i].chip_count), sizeof(int32_t)), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[Client] Error reading chip count for device {}: {}", i, error.message());
                device_info.clear(); // clear the vector on error
                return device_info;
            }

            // Read current config
            rbytes = socket->read(mxasio::buffer(&(device_info[i].current_config), sizeof(int32_t)), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[Client] Error reading current config for device {}: {}", i, error.message());
                device_info.clear(); // clear the vector on error
                return device_info;
            }

            // Read number of groups
            rbytes = socket->read(mxasio::buffer(&(device_info[i].num_groups), sizeof(int32_t)), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[Client] Error reading number of groups for device {}: {}", i, error.message());
                device_info.clear(); // clear the vector on error
                return device_info;
            }

            // Read chips per group
            rbytes = socket->read(mxasio::buffer(&(device_info[i].chips_per_group), sizeof(int32_t)), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[Client] Error reading chips per group for device {}: {}", i, error.message());
                device_info.clear(); // clear the vector on error
                return device_info;
            }

            // Read can_get_power_data
            rbytes = socket->read(mxasio::buffer(&(device_info[i].can_get_power_data), 1), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[Client] Error reading can_get_power_data for device {}: {}", i, error.message());
                device_info.clear(); // clear the vector on error
                return device_info;
            }

            // For each chip, read freqs
            device_info[i].freqs.resize(device_info[i].chip_count, 0);
            for(int32_t j = 0; j < device_info[i].chip_count; j++) {
                uint16_t freq = 0;
                rbytes = socket->read(mxasio::buffer(&freq, sizeof(uint16_t)), error);
                if(UNLIKELY(rbytes == 0 || error)) {
                    spdlog::error("[Client] Error reading frequency for device {} chip {}: {}", i, j, error.message());
                    device_info.clear(); // clear the vector on error
                    return device_info;
                }
                device_info[i].freqs[j] = freq;
            }

            // Read voltage
            rbytes = socket->read(mxasio::buffer(&(device_info[i].volt), sizeof(uint16_t)), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[Client] Error reading voltage for device {}: {}", i, error.message());
                device_info.clear(); // clear the vector on error
                return device_info;
            }

        }

        spdlog::debug("[Client] Successfully retrieved device info for {} devices", all_devices_count);
        return device_info; // return the filled vector
    }
    catch (std::exception &e) {
        spdlog::error("[Client] Exception: {}", e.what());
    }

    // fallthrough
    device_info.clear(); // clear the vector on error
    return device_info;
}
