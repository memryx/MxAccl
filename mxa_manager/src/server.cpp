// Copyright (c) 2025 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <iostream>
#include <unordered_map>
#include <queue>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <cstdint>
#include <chrono>
#include <string>

#include <memx/memx.h>

#include "spdlog/spdlog.h"
#ifdef _WIN32
    #include <spdlog/sinks/win_eventlog_sink.h>
#endif

#include "server.h"
#include "parsing_macros.h"

using mxasio::ip::tcp;


namespace MX
{
namespace Manager
{

Server::Server(std::string addr_, unsigned short base_port_, unsigned int hw_monitor_interval_ms_)
    : hw_monitor_interval_ms(hw_monitor_interval_ms_)
{
    addr = addr_;
    base_port = base_port_;

    ctrl_endpoint_thread = nullptr;
    ifmap_endpoint_thread = nullptr;
    ofmap_endpoint_thread = nullptr;

    ctrl_listener_ptr = nullptr;
    ifmap_listener_ptr = nullptr;
    ofmap_listener_ptr = nullptr;

    running = true;

    // count the number of connected devices
    memx_status status = memx_operation_get_device_count(&all_devices_count);
    if(memx_status_error(status)) {
        spdlog::error("[Server] Error in memx_operation_get_device_count: {}", static_cast<int>(status));
        std::exit(EXIT_FAILURE);
    }
    else if(all_devices_count == 0) {
        // no devices found: print a warning and continue,
        // but do not start executor or scheduler threads
        spdlog::warn("[Server] No devices found on {}:{}", addr, base_port);
    }
    else {
        lock_table.init(all_devices_count);
        devinfo_table.resize(all_devices_count);
        spdlog::info("[Server] Found {} devices on {}:{}", all_devices_count, addr, base_port);

        // do all the 'discover devices' stuff
        for(uint8_t i = 0; i < all_devices_count; i++) {

            // GET_HW_INFO command
            uint64_t hwinfo64 = 0;
            status = memx_get_feature(i, 0, OPCODE_GET_INTERFACE_INFO, &hwinfo64);
            if(memx_status_error(status)) {
                spdlog::critical("[Server] Error in  memx_get_feature OPCODE_GET_INTERFACE_INFO for device {}: {}", i, static_cast<int>(status));
                std::exit(EXIT_FAILURE);
            }

            device_info_t devinfo;

            if ((hwinfo64 & 0xFF) == 2) {
                // PCIE interface
                devinfo.is_usb = false;
                status = memx_get_feature(i, 0, OPCODE_GET_HW_INFO, &hwinfo64);
                if(memx_status_error(status)) {
                    spdlog::critical("[Server] Error in OPCODE_GET_HW_INFO memx_get_feature for device {}: {}", i, static_cast<int>(status));
                    std::exit(EXIT_FAILURE);
                }

                {
                    // parse the hwinfo
                    constexpr uint64_t CHIP_COUNT_MASK      = uint64_t{0xFF} << 16;
                    constexpr uint64_t CHIPS_PER_GROUP_MASK = uint64_t{0xFF} << 32;
                    constexpr uint64_t NUM_GROUPS_MASK      = uint64_t{0xFF} << 48;
                    devinfo.chip_count      = (hwinfo64 & CHIP_COUNT_MASK)      >> 16;
                    devinfo.chips_per_group = (hwinfo64 & CHIPS_PER_GROUP_MASK) >> 32;
                    devinfo.num_groups      = (hwinfo64 & NUM_GROUPS_MASK)      >> 48;
                }
            }
            else {
                // USB interface
                uint32_t data = 0;
                devinfo.is_usb = true;
                status = memx_get_total_chip_count(i, (uint8_t*)&data);
                if(memx_status_error(status)) {
                    spdlog::critical("[Server] Error in  memx_get_total_chip_count for device {}: {}", i, static_cast<int>(status));
                    std::exit(EXIT_FAILURE);
                }
                devinfo.chip_count = data;

                status = memx_operation_get_mpu_group_count(i, (void*)&data);
                if(memx_status_error(status)) {
                    spdlog::critical("[Server] Error in  memx_operation_get_mpu_group_count for device {}: {}", i, static_cast<int>(status));
                    std::exit(EXIT_FAILURE);
                }
                devinfo.num_groups = data;
                devinfo.chips_per_group = devinfo.chip_count / devinfo.num_groups;
            }


            // figure out the current config
            if(devinfo.chip_count == 8 && devinfo.num_groups == 1) {
                devinfo.current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_EIGHT_MPUS;
            }
            else if(devinfo.chip_count == 4 && devinfo.num_groups == 1) {
                devinfo.current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_FOUR_MPUS;
            }
            else if(devinfo.chip_count == 4 && devinfo.num_groups == 2) {
                devinfo.current_config = MEMX_MPU_GROUP_CONFIG_TWO_GROUP_TWO_MPUS;
            }
            else if (devinfo.chip_count == 2 && devinfo.num_groups == 1) {
                devinfo.current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_TWO_MPUS;
            }
            else if (devinfo.chip_count == 1 && devinfo.num_groups == 1) {
                devinfo.current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_ONE_MPU;
            }
            else {
                // invalid configuration
                spdlog::error("[Server] Invalid chip count {} or group count {} for device {}",
                              devinfo.chip_count, devinfo.num_groups, i);
                std::exit(EXIT_FAILURE);
            }

            // check frequency and voltage
            uint64_t freq = 0;
            devinfo.freqs.resize(devinfo.chip_count, 0);
            for(int j = 0; j < devinfo.chip_count; j++) {
                status = memx_get_feature(i, j, OPCODE_GET_FREQUENCY, &freq);
                if(memx_status_error(status)) {
                    spdlog::warn("[Server] Error in memx_get_feature OPCODE_GET_FREQUENCY for device {}: {}", i, static_cast<int>(status));
                    freq = 600; // default frequency
                }
                devinfo.freqs[j] = (uint16_t) freq & 0xFFFF;
            }

            uint64_t volt = 0; // default voltage
            status = memx_get_feature(i, 0, OPCODE_GET_VOLTAGE, &volt);
            if(memx_status_error(status)) {
                spdlog::warn("[Server] Error in memx_get_feature OPCODE_GET_VOLTAGE for device {}: {}", i, static_cast<int>(status));
                volt = 700; // default voltage
            }
            devinfo.volt = (uint16_t) volt & 0xFFFF;

            // check if device supports power data
            uint64_t power = 0;
            status = memx_get_feature(i, 0, OPCODE_GET_POWER, &power);
            if(memx_status_no_error(status) && power > 0 && power < 17000) {
                devinfo.can_get_power_data = true;
            }
            else {
                devinfo.can_get_power_data = false;
            }

            devinfo_table[i] = devinfo;
        }

        //// print the device_info table for all devices
        //std::cout << "Device ID | Chip Count | Num Groups | GetPwr? |  Freq | Volt" << std::endl;
        //std::cout << "----------|------------|------------|---------|-------|-----" << std::endl;
        //for(uint8_t d = 0; d < all_devices_count ; d++) {
        //    std::cout << std::setw(9) << int(d) << " | ";
        //    std::cout << std::setw(10) << devinfo_table[d].chip_count << " | ";
        //    std::cout << std::setw(10) << devinfo_table[d].num_groups << " | ";
        //    std::cout << std::setw(7)  << (devinfo_table[d].can_get_power_data ? "Yes" : "No") << " | ";
        //    std::cout << std::setw(5)  << devinfo_table[d].freqs[0] << " | ";
        //    std::cout << std::setw(4)  << devinfo_table[d].volt;
        //    std::cout << std::endl;
        //}
        //std::cout << std::endl;

        // create executor queues and threads
        executor_queues = new BlockyQueue<ExecutorTask*>[all_devices_count];
        dfp_executor_threads = new std::thread*[all_devices_count];
        for(int i = 0; i < all_devices_count; i++) {
            dfp_executor_threads[i] = new std::thread(&Server::executor_thread, this, i);
        }

        // create the scheduler thread
        scheduler_thread = new std::thread(&Server::scheduler_thread_func, this);
    }
}

Server::~Server()
{
    // stop the server
    kill();
}

void Server::start()
{
    ctrl_endpoint_thread = new std::thread(&Server::control_endpoint_listener, this);
    ifmap_endpoint_thread = new std::thread(&Server::ifmap_endpoint_listener, this);
    ofmap_endpoint_thread = new std::thread(&Server::ofmap_endpoint_listener, this);
    spdlog::info("Server::start done");
}


void Server::kill()
{
    running = false;

    // close all listeners
    spdlog::info("[Server] Closing all listeners...");
    if(ctrl_listener_ptr != nullptr) {
        ctrl_listener_ptr->kill();
        ctrl_listener_ptr = nullptr;
    }
    if(ifmap_listener_ptr != nullptr) {
        ifmap_listener_ptr->kill();
        ifmap_listener_ptr = nullptr;
    }
    if(ofmap_listener_ptr != nullptr) {
        ofmap_listener_ptr->kill();
        ofmap_listener_ptr = nullptr;
    }

    // kill the threads
    spdlog::info("[Server] Shutting down Executor threads...");
    for(int i = 0; i < all_devices_count; i++) {
        // TODO: investigate why join() doesn't work here instead...
        dfp_executor_threads[i]->detach();
        delete dfp_executor_threads[i];
        dfp_executor_threads[i] = nullptr;
    }
    delete [] dfp_executor_threads;
    dfp_executor_threads = nullptr;
    all_devices_count = 0;

    // kill the scheduler thread
    spdlog::info("[Server] Shutting down Scheduler thread...");
    if(scheduler_thread != nullptr) {
        scheduler_thread->join();
        delete scheduler_thread;
        scheduler_thread = nullptr;
    }

    // delete the queues
    delete [] executor_queues;
    executor_queues = nullptr;


    spdlog::info("[Server] Stopping endpoint threads...");
    if(ctrl_endpoint_thread != nullptr) {
        // TODO: investigate why join() doesn't work here instead...
        ctrl_endpoint_thread->detach();
        delete ctrl_endpoint_thread;
        ctrl_endpoint_thread = nullptr;
    }

    if(ifmap_endpoint_thread != nullptr) {
        ifmap_endpoint_thread->detach();
        delete ifmap_endpoint_thread;
        ifmap_endpoint_thread = nullptr;
    }

    if(ofmap_endpoint_thread != nullptr) {
        ofmap_endpoint_thread->detach();
        delete ofmap_endpoint_thread;
        ofmap_endpoint_thread = nullptr;
    }

    spdlog::info("[Server] Server stopped successfully.");
}

// general message reply function
void Server::status_reply(MX::RPC::Socket* s, uint32_t client_id, MX::RPC::status_t msg, uint32_t data)
{
    MX::RPC::MsgHeader         response_header;
    MX::RPC::MsgStatus         msg_status;

    response_header.client_id = 0;
    response_header.msg_type = MX::RPC::MSG_TYPE_STATUS;

    msg_status.s = msg;
    msg_status.dat = data;

    try {
        s->write(mxasio::buffer(&response_header, sizeof(response_header)));
        s->write(mxasio::buffer(&msg_status, sizeof(msg_status)));
    }
    catch (std::exception &e) {
        spdlog::error("[Server] STATUS_REPLY to client {} exception: {}", client_id, e.what());
        std::lock_guard<std::mutex> lock(meta_lock);
        client_meta[client_id]->alive = false;
    }
}



//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
// Control Port: management, local requests, DFP downloads, etc....
//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------

// CTRL connection handler
void Server::control_endpoint_listener()
{
    try {
        // io context
        mxasio::io_context io_context;
        std::string addr_ = this->addr;
        unsigned short port_ = this->base_port;

        // Listener creation
        MX::RPC::Listener listener(io_context, addr_, port_);
        ctrl_listener_ptr = &listener;

        spdlog::info("[CTRL] control listener running on {}:{}", addr_, port_);

        while(running == true) {
            MX::RPC::Socket* socket = listener.accept();

            spdlog::info("[CTRL] new control connection accepted");

            // populate the meta table
            uint32_t client_id = id_list.get_new();
            ClientMeta* n = new ClientMeta(client_id);
            n->remote_endpoint = socket->remote_endpoint();
            n->ctrl_socket = socket;
            {
                std::lock_guard<std::mutex> lock(meta_lock);
                client_meta[client_id] = n;
            }

            // launch the ctrl thread
            std::thread(&Server::control_session, this, socket, std::move(n)).detach();

        }
    }
    catch (std::exception &e) {
        spdlog::error("[CTRL] control listener exception: {}", e.what());
    }
}


// Handles a client process's connection to the Control socket
void Server::control_session(MX::RPC::Socket* s, ClientMeta* meta)
{

    MX::RPC::MsgHeader         header;
    int                        proto_version = 1;
    MX::RPC::MsgConnect        msg_conn;
    MX::RPC::MsgSubmitDfp_v1   msg_dfp_v1;
    MX::RPC::MsgSubmitDfp      msg_dfp_v2;
    MX::RPC::MsgGetTempPower   msg_tpow;
    MX::RPC::MsgTempPower      msg_tpow_reply;

    // shared across proto versions
    int32_t  msg_dfp_len_devices_to_use = 0;
    int32_t* msg_dfp_devices_to_use     = nullptr;
    uint64_t msg_dfp_num_dfp_bytes      = 0;
    uint8_t* msg_dfp_dfp_bytes          = nullptr;

    MX::RPC::MsgLocalLock      msg_lock;
    MX::RPC::MsgStatus         msg_status;
    mxasio::error_code         error;

    size_t rbytes;
    uint32_t my_client_id = meta->id;

    while(meta->alive == true && running == true) {

        // read initial header
        rbytes = s->read(mxasio::buffer(&header, sizeof(header)), error);
        if(rbytes == 0 || error) {
            if(error == mxasio::error::eof) {
                spdlog::info("[CTRL] control connection from {} closed", s->remote_endpoint());
                goto cleanup;
            }
            else {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
            }
            break;
        }


        // CONNECTION PACKETS
        //-------------------------------------------------------------------------------
        if(header.msg_type == MX::RPC::MSG_TYPE_CONN) {
            rbytes = s->read(mxasio::buffer(&msg_conn, sizeof(msg_conn)), error);
            if(rbytes == 0 || error) {
                std::cerr << "ERROR: " << error.message() << std::endl;
                break;
            }

            if(msg_conn.cmd == MX::RPC::INIT_CONNECTION_PROTO_1 || msg_conn.cmd == MX::RPC::INIT_CONNECTION_PROTO_2) {
                // do we not actually have devices connected..?
                if(all_devices_count <= 0) {
                    spdlog::warn("[CTRL] got INIT_CONNECTION but we don't have any devices!");
                    msg_status.s = MX::RPC::NO_DEVICES_IN_SYSTEM;
                    msg_status.dat = 0;
                    status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                    break;
                }

                // else all is good
                else {
                    if(msg_conn.cmd == MX::RPC::INIT_CONNECTION_PROTO_2)
                        proto_version = 2;
                    else
                        proto_version = 1;

                    msg_status.s = MX::RPC::HERE_IS_YOUR_NEW_ID;
                    msg_status.dat = my_client_id;
                    spdlog::debug("[CTRL] control connection from {} assigned ID {}", s->remote_endpoint(), my_client_id);
                    status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                }


            }
            else if(msg_conn.cmd == MX::RPC::END_CONNECTION) {
                if(header.client_id != my_client_id) {
                    spdlog::warn("[CTRL] client {} tried to END_CONNECTION with ID {}", header.client_id, my_client_id);
                    msg_status.s = MX::RPC::YOU_LIED_ABOUT_YOUR_ID;
                    msg_status.dat = 0;
                    status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                    break;
                }
                else {
                    // release this client ID
                    spdlog::debug("[CTRL] control connection from {} released ID {}", s->remote_endpoint(), my_client_id);

                    // tell the server it was successful
                    status_reply(s, my_client_id, MX::RPC::OK, 0);

                    break;
                }
            }
        }

        // LOCAL LOCK PACKETS
        //-------------------------------------------------------------------------------
        else if(header.msg_type == MX::RPC::MSG_TYPE_LOCK) {
            rbytes = s->read(mxasio::buffer(&msg_lock, sizeof(msg_lock)), error);
            if(rbytes == 0 || error) {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                break;
            }

            // check for client ID mismatch
            if(header.client_id != my_client_id) {
                spdlog::warn("[CTRL] client {} tried to LOCK with ID {}", header.client_id, my_client_id);
                msg_status.s = MX::RPC::YOU_LIED_ABOUT_YOUR_ID;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                break;
            }

            // check for invalid ID
            if((msg_lock.device_id < 0) || (msg_lock.device_id > (all_devices_count - 1))) {
                spdlog::warn("[CTRL] client {} tried to Local (Un)Lock with invalid ID {}", header.client_id, my_client_id);
                status_reply(s, my_client_id, MX::RPC::INVALID_DEVICE, 0);
            }
            else {
                // do the indicated unlock/lock/trylock operation
                if(msg_lock.u0_l1_t2 == MsgLocalLock::UNLOCK) {
                    if(lock_table.unlock(msg_lock.device_id, header.client_id)) {
                        spdlog::info("[CTRL] client {} unlocked device {}", header.client_id, msg_lock.device_id);
                        status_reply(s, my_client_id, MX::RPC::OK, 0);
                    }
                    else {
                        status_reply(s, my_client_id, MX::RPC::LOCK_UNLOCK_YOU_ARENT_OWNER, 0);
                        spdlog::warn("[CTRL] client {} tried to unlock device {} but it is not locked by them", header.client_id, msg_lock.device_id);
                    }
                }
                else if(msg_lock.u0_l1_t2 == MsgLocalLock::LOCK) {
                    spdlog::debug("[CTRL] client {} is going to block-lock device {}...", header.client_id, msg_lock.device_id);
                    lock_table.lock(msg_lock.device_id, header.client_id);
                    status_reply(s, my_client_id, MX::RPC::OK, 0);
                    spdlog::info("[CTRL] client {} locked device {} via lock()", header.client_id, msg_lock.device_id);
                }
                else if(msg_lock.u0_l1_t2 == MsgLocalLock::TRYLOCK) {
                    if(lock_table.trylock(msg_lock.device_id, header.client_id)) {
                        spdlog::info("[CTRL] client {} locked device {} via trylock()", header.client_id, msg_lock.device_id);
                        status_reply(s, my_client_id, MX::RPC::OK, 0);
                    }
                    else {
                        spdlog::info("[CTRL] client {} trylock failed to get device {} because it is already locked", header.client_id,
                                     msg_lock.device_id);
                        status_reply(s, my_client_id, MX::RPC::LOCK_DEVICE_ALREADY_LOCKED, 0);
                    }
                }
                else {
                    spdlog::warn("[CTRL] client {} sent an invalid lock command", header.client_id);
                    status_reply(s, my_client_id, MX::RPC::LOCK_INVALID_COMMAND, 0);
                }
            }

            lock_table.print();
        }


        // DFP SUBMISSION
        //-------------------------------------------------------------------------------
        else if(header.msg_type == MX::RPC::MSG_TYPE_DFP) {
            // check for client ID mismatch
            if(header.client_id != my_client_id) {
                spdlog::warn("[CTRL] client {} tried to do a DFP command but had an invalid client_id {}", header.client_id, my_client_id);
                msg_status.s = MX::RPC::YOU_LIED_ABOUT_YOUR_ID;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                break;
            }

            // calls the big long message parsing macros defined in parsing_macros.h
            if(proto_version == 2){
                RECV_DFP_PROTO_V2;
            } else {
                RECV_DFP_PROTO_V1;
            }

            rbytes = s->read(mxasio::buffer(&(msg_dfp_len_devices_to_use), sizeof(msg_dfp_len_devices_to_use)), error);
            if(rbytes == 0 || error) {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                break;
            }
            if(msg_dfp_devices_to_use != nullptr) {
                delete [] msg_dfp_devices_to_use;
                msg_dfp_devices_to_use = nullptr;
            }
            msg_dfp_devices_to_use = new int32_t[msg_dfp_len_devices_to_use];
            rbytes = s->read(mxasio::buffer(msg_dfp_devices_to_use, msg_dfp_len_devices_to_use * sizeof(int32_t)), error);
            if(rbytes == 0 || error) {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                delete [] msg_dfp_devices_to_use;
                msg_dfp_devices_to_use = nullptr;
                break;
            }
            rbytes = s->read(mxasio::buffer(&(msg_dfp_num_dfp_bytes), sizeof(msg_dfp_num_dfp_bytes)), error);
            if(rbytes == 0 || error) {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                delete [] msg_dfp_devices_to_use;
                msg_dfp_devices_to_use = nullptr;
                break;
            }
            if(msg_dfp_dfp_bytes != nullptr) {
                delete [] msg_dfp_dfp_bytes;
                msg_dfp_dfp_bytes = nullptr;
            }
            msg_dfp_dfp_bytes = new uint8_t[msg_dfp_num_dfp_bytes];
            rbytes = s->read(mxasio::buffer(msg_dfp_dfp_bytes, msg_dfp_num_dfp_bytes), error);
            if(rbytes == 0 || error) {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                delete [] msg_dfp_dfp_bytes;
                msg_dfp_dfp_bytes = nullptr;
                break;
            }

            // validate args for DFPContext
            // NOTE: require doing this before init or reuse DFPContext
            std::vector<uint8_t> device_ctxs_allowed;
            for (int i = 0; i < msg_dfp_len_devices_to_use; i++) {
                int dev_id = msg_dfp_devices_to_use[i];
                if (dev_id >= 0 && dev_id < all_devices_count) {
                    device_ctxs_allowed.push_back(dev_id);
                }
                else {
                    spdlog::warn("[CTRL] client {} tried to use invalid device {}", my_client_id, dev_id);
                }
            }

            // check at least one valid device before trying to create the DFPContext
            if(device_ctxs_allowed.empty()) {
                spdlog::error("[CTRL] Client {} did not specify any valid devices for DFP context creation.", my_client_id);
                msg_status.s = MX::RPC::DFP_NO_VALID_CONTEXTS;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                break;
            }

            // generate a sha512 of the dfp bytes, and use this to see if this DFP is
            // already present in the dfp_contexts map
            MX::sha512::hash_t hash = MX::sha512::compute(msg_dfp_dfp_bytes, msg_dfp_num_dfp_bytes);
            DFPContext* dfp = nullptr;
            {
                std::unique_lock<std::mutex> lock(dfp_contexts_lock);
                if(dfp_contexts.count(hash) > 0) {
                    // double check the hash is unique by doing a full mem compare
                    uint8_t* existing_bytes = dfp_contexts[hash]->raw_dfp_bytes;
                    if(UNLIKELY(std::memcmp(existing_bytes, msg_dfp_dfp_bytes, msg_dfp_num_dfp_bytes) != 0)) {
                        // wow, this is a very rare collision...
                        lock.unlock(); // dont need the lock during client reply
                        spdlog::error("[CTRL] DFP SHA512 collision for {}", MX::sha512::to_base64(hash));
                        msg_status.s = MX::RPC::DFP_CHECKSUM_COLLISION;
                        msg_status.dat = 0;
                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        CLEAN_MSG_DFP;
                        break;
                    }
                    else {
                        // we have a match, so just use the existing DFPContext
                        dfp = dfp_contexts[hash];
                        dfp->client_ref_count++;
                        meta->my_dfp_context = dfp;

                        if(proto_version == 2){
                            // set our submodel_id
                            meta->submodel_id = msg_dfp_v2.submodel_id;
                            // set client options
                            meta->client_options.smoothing = (bool) msg_dfp_v2.smoothing;
                            meta->client_options.fps_target = msg_dfp_v2.fps_target;
                        } else {
                            meta->submodel_id = msg_dfp_v1.submodel_id;
                            meta->client_options.smoothing = (bool) msg_dfp_v1.smoothing;
                            meta->client_options.fps_target = msg_dfp_v1.fps_target;
                        }

                        // if the requested submodel_id is greater than the number of models in the Dfp, send the client an error message
                        if(meta->submodel_id >= dfp->info->num_models) {
                            spdlog::warn("[CTRL] requested submodel_id {} is greater than the number of models in the DFP {}", meta->submodel_id,
                                         dfp->info->num_models);
                            msg_status.s = MX::RPC::DFP_SUBMODEL_ID_OUT_OF_BOUNDS;
                            msg_status.dat = 0;
                            CLEAN_MSG_DFP;
                            meta->my_dfp_context->client_ref_count--;
                            meta->my_dfp_context = nullptr;

                            lock.unlock();

                            status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                            break;
                        }
                        else {
                            lock.unlock();
                        }

                        // add the client to the DFPContext
                        ContextClient* c = dfp->get_mctx(meta->submodel_id)->add_client(my_client_id, &(meta->alive));
                        if(c == nullptr) {
                            spdlog::error("[CTRL] failed to add client to DFPContext with hash {}, submodel_id {}", MX::sha512::to_base64(hash),
                                          meta->submodel_id);
                            msg_status.s = MX::RPC::DFP_CLIENT_ADD_FAILED;
                            msg_status.dat = 0;
                            status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                            CLEAN_MSG_DFP;
                            break;
                        }
                        else {
                            meta->my_client_context = c;
                            meta->my_model_context = dfp->get_mctx(meta->submodel_id);
                        }

                        // reply to the client
                        spdlog::info("[CTRL] client {} reusing existing DFPContext with hash {}, submodel_id {}", my_client_id,
                                     MX::sha512::to_base64(hash), meta->submodel_id);
                        meta->my_dfp_context->print_clients();
                        msg_status.s = MX::RPC::DFP_OK_BUT_IGNORING_OPTIONS;
                        msg_status.dat = 0;
                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        CLEAN_MSG_DFP;
                    }

                }
                else {
                    // create a new DFPContext
                    if(proto_version == 2){
                        dfp = new DFPContext(msg_dfp_num_dfp_bytes, msg_dfp_dfp_bytes, hash, msg_dfp_v2.ifmap_queue_size, msg_dfp_v2.ofmap_queue_size, device_ctxs_allowed);
                    } else {
                        dfp = new DFPContext(msg_dfp_num_dfp_bytes, msg_dfp_dfp_bytes, hash, msg_dfp_v1.ifmap_queue_size, msg_dfp_v1.ofmap_queue_size, device_ctxs_allowed);
                    }

                    if(dfp->successful_init == false) {
                        spdlog::warn("[CTRL] telling client {} we ran out of driver ctx IDs", my_client_id);
                        msg_status.s = MX::RPC::DFP_TOO_MANY_OPEN_CONTEXTS;
                        msg_status.dat = 32;

                        delete dfp;
                        dfp = nullptr;
                        CLEAN_MSG_DFP;
                        meta->my_dfp_context = nullptr;

                        lock.unlock();

                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        break;
                    }

                    dfp_contexts[hash] = dfp;
                    dfp->client_ref_count++;
                    meta->my_dfp_context = dfp;
                    if(proto_version == 2){
                        // set client options
                        meta->client_options.smoothing = (bool) msg_dfp_v2.smoothing;
                        meta->client_options.fps_target = msg_dfp_v2.fps_target;

                        // set our submodel_id
                        meta->submodel_id = msg_dfp_v2.submodel_id;
                    } else {
                        meta->submodel_id = msg_dfp_v1.submodel_id;
                        meta->client_options.smoothing = (bool) msg_dfp_v1.smoothing;
                        meta->client_options.fps_target = msg_dfp_v1.fps_target;
                    }

                    // if the requested submodel_id is greater than the number of models in the Dfp, send the client an error message
                    if(meta->submodel_id >= dfp->info->num_models) {
                        spdlog::warn("[CTRL] requested submodel_id {} is greater than the number of models in the DFP {}", meta->submodel_id,
                                     dfp->info->num_models);
                        msg_status.s = MX::RPC::DFP_SUBMODEL_ID_OUT_OF_BOUNDS;
                        msg_status.dat = 0;
                        // remove this DFPContext since we were the only one going to use it
                        dfp_contexts.erase(hash);
                        delete dfp;
                        dfp = nullptr;
                        CLEAN_MSG_DFP;

                        meta->my_dfp_context = nullptr;

                        lock.unlock();

                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        break;
                    }
                    else {

                        // also check the num chips matches all devices_to_use num chips
                        int expected_chip_cnt = dfp->info->num_chips;
                        std::shared_lock<std::shared_mutex> devlock(devinfo_lock);
                        msg_status.s = MX::RPC::OK;
                        for(int i = 0; i < msg_dfp_len_devices_to_use; i++) {
                            int dev_id = msg_dfp_devices_to_use[i];
                            if(devinfo_table[dev_id].chip_count < expected_chip_cnt) {
                                spdlog::warn("[CTRL] client {} tried to submit a DFP whose chip count {} is greater than device {}'s chip count {}",
                                             my_client_id, expected_chip_cnt, dev_id, devinfo_table[dev_id].chip_count);
                                msg_status.s = MX::RPC::DFP_WRONG_NUMBER_OF_CHIPS;
                                msg_status.dat = 0;

                                // remove this DFPContext since we were the only one going to use it
                                dfp_contexts.erase(hash);
                                delete dfp;
                                dfp = nullptr;
                                CLEAN_MSG_DFP;

                                meta->my_dfp_context = nullptr;

                                devlock.unlock();
                                lock.unlock();

                                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                                break;
                            }
                        }
                        // check for fail in inner loop
                        if(msg_status.s == MX::RPC::DFP_WRONG_NUMBER_OF_CHIPS) {
                            break;
                        }

                        // else we're good
                        devlock.unlock();
                        lock.unlock();
                    }


                    // add the client to the DFPContext
                    ContextClient* c = dfp->get_mctx(meta->submodel_id)->add_client(my_client_id, &(meta->alive));
                    if(c == nullptr) {
                        spdlog::error("[CTRL] failed to add client to DFPContext with hash {}, submodel_id {}", MX::sha512::to_base64(hash),
                                      meta->submodel_id);
                        msg_status.s = MX::RPC::DFP_CLIENT_ADD_FAILED;
                        msg_status.dat = 0;
                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        CLEAN_MSG_DFP;
                        break;
                    }
                    else {
                        meta->my_client_context = c;
                        meta->my_model_context = dfp->get_mctx(meta->submodel_id);
                    }

                    spdlog::info("[CTRL] client {} created new DFPContext with hash {}, submodel_id {}", my_client_id, MX::sha512::to_base64(hash),
                                 meta->submodel_id);

                    // pprint devices to use info
                    for (int dev_id: device_ctxs_allowed) {
                        spdlog::debug("[CTRL] client {} requested device {}", my_client_id, dev_id);
                    }

                    // create a new executor task for each device to use
                    for (int dev_id: device_ctxs_allowed) {

                        ExecutorTask* task = new ExecutorTask();
                        task->dfp_ctx = dfp;
                        task->allowed_device = dev_id;

                        // scheduler options
                        if(proto_version == 2){
                            task->frame_limit = msg_dfp_v2.frame_limit;
                            task->time_limit = msg_dfp_v2.time_limit;
                            task->autoclock_enabled = msg_dfp_v2.autoclock_enabled;
                            task->autoclock_done = false;
                            task->autoclock_check_fps_saturation = msg_dfp_v2.autoclock_check_fps_saturation;
                            task->power_limit_mw = msg_dfp_v2.autoclock_power_limit_mw;
                            task->autoclock_sample_interval_ms = msg_dfp_v2.autoclock_sample_interval_ms;
                            task->autoclock_num_samples = msg_dfp_v2.autoclock_num_samples;
                        } else {
                            task->frame_limit = msg_dfp_v1.frame_limit;
                            task->time_limit = msg_dfp_v1.time_limit;
                            task->autoclock_enabled = false;
                        }

                        task->dfp_ctx->exec_ref_count++;
                        scheduler_queue.push(task);
                    }

                    // cleanup
                    delete [] msg_dfp_devices_to_use;
                    msg_dfp_devices_to_use = nullptr;

                    // reply to client
                    msg_status.s = MX::RPC::OK;
                    msg_status.dat = 0;
                    status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                }
            }

        }

        // TEMPERATURE AND POWER REQUEST PACKETS
        //-------------------------------------------------------------------------------
        else if(header.msg_type == MX::RPC::MSG_TYPE_GET_TEMP_POWER) {

            rbytes = s->read(mxasio::buffer(&msg_tpow, sizeof(msg_tpow)), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                break;
            }

            // check for client ID mismatch
            if(UNLIKELY(header.client_id != my_client_id)) {
                spdlog::warn("[CTRL] client {} tried to GET_TEMP_POWER with ID {}", header.client_id, my_client_id);
                msg_status.s = MX::RPC::YOU_LIED_ABOUT_YOUR_ID;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                break;
            }

            // check the msg_tpow.device_id is valid
            if(UNLIKELY((msg_tpow.device_id < 0) || (msg_tpow.device_id >= all_devices_count))) {
                spdlog::warn("[CTRL] client {} tried to GET_TEMP_POWER with invalid device ID {}", header.client_id, msg_tpow.device_id);
                msg_status.s = MX::RPC::INVALID_DEVICE;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                continue;
            }
            else {

                DFPExecutor* exe = nullptr;
                {
                    std::shared_lock<std::shared_mutex> extlock(m_executor_table);
                    if(all_dfp_executors.count(msg_tpow.device_id) == 0) {
                        spdlog::warn("[CTRL] client {} tried to GET_TEMP_POWER for device {} but it is not in the executor table", header.client_id,
                                     msg_tpow.device_id);
                        msg_status.s = MX::RPC::INVALID_DEVICE;
                        msg_status.dat = 0;
                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        continue;
                    }
                    else {
                        exe = all_dfp_executors[msg_tpow.device_id];
                    }
                    extlock.unlock();
                }

                // check if the device is locked in the lock table
                uint32_t owner_id = 0xDEADBEEF;
                if(LIKELY(lock_table.check_lock(msg_tpow.device_id, &owner_id))) {
                    if(UNLIKELY(owner_id != 0)) {
                        // this is owned by another client, so we cannot get the temp/power
                        spdlog::warn("[CTRL] client {} tried to GET_TEMP_POWER for device {} but it is in Local mode and owned by client {}",
                                     header.client_id, msg_tpow.device_id, owner_id);
                        msg_status.s = MX::RPC::INFO_DEVICE_IS_LOCAL_LOCKED;
                        msg_status.dat = 0;
                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        continue;
                    }
                }


                if(msg_tpow.type == MX::RPC::MsgGetTempPower::TEMPERATURE) {

                    // max temp across the whole module
                    if(msg_tpow.target == MX::RPC::MsgGetTempPower::MODULE) {
                        msg_tpow_reply.num_items = 1;
                        float val = 0.0f;

                        if(msg_tpow.measure_mode == MX::RPC::MsgGetTempPower::INSTANT) {
                            // get the instant temperature of the module
                            val = exe->inst_max_temp();
                        }
                        else {
                            // get the averaged temperature of the module
                            val = exe->avg_max_temp();
                        }

                        // send the reply

                        // first send a header with msg_type_t as MSG_TYPE_GET_TEMP_POWER.
                        // if there was an error earlier, a header would have been sent
                        // with msg_type_t as MSG_TYPE_STATUS with value != OK
                        header.msg_type = MX::RPC::MSG_TYPE_GET_TEMP_POWER;
                        header.client_id = 0; // 0 means "server"
                        s->write(mxasio::buffer(&header, sizeof(header)), error);
                        if(UNLIKELY(error)) {
                            spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                            break;
                        }


                        // send the MsgTempPower reply packet
                        s->write(mxasio::buffer(&(msg_tpow_reply.num_items), sizeof(uint32_t)));
                        s->write(mxasio::buffer(&val, sizeof(float)));

                    }
                    else {
                        // temps for every chip on the module
                        std::vector<float> got_temps;

                        if(msg_tpow.measure_mode == MX::RPC::MsgGetTempPower::INSTANT) {
                            got_temps = exe->inst_all_temps();
                        }
                        else {
                            got_temps = exe->avg_all_temps();
                        }

                        msg_tpow_reply.num_items = got_temps.size();

                        // first send a header with msg_type_t as MSG_TYPE_GET_TEMP_POWER.
                        header.msg_type = MX::RPC::MSG_TYPE_GET_TEMP_POWER;
                        header.client_id = 0; // 0 means "server"
                        s->write(mxasio::buffer(&header, sizeof(header)), error);
                        if(UNLIKELY(error)) {
                            spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                            break;
                        }

                        // send the reply
                        s->write(mxasio::buffer(&(msg_tpow_reply.num_items), sizeof(uint32_t)));
                        s->write(mxasio::buffer(got_temps.data(), sizeof(float) * msg_tpow_reply.num_items));

                    }

                }
                else if(msg_tpow.type == MX::RPC::MsgGetTempPower::POWER) {
                    if(exe->can_get_power) {
                        // get the power reading
                        msg_tpow_reply.num_items = 1;
                        float val = 0.0f;

                        if(msg_tpow.measure_mode == MX::RPC::MsgGetTempPower::AVERAGED) {
                            val = exe->avg_power();
                        }
                        else {
                            val = exe->inst_power();
                        }

                        // first send a header with msg_type_t as MSG_TYPE_GET_TEMP_POWER.
                        header.msg_type = MX::RPC::MSG_TYPE_GET_TEMP_POWER;
                        header.client_id = 0; // 0 means "server"
                        s->write(mxasio::buffer(&header, sizeof(header)), error);
                        if(UNLIKELY(error)) {
                            spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                            break;
                        }

                        // send the reply
                        s->write(mxasio::buffer(&(msg_tpow_reply.num_items), sizeof(uint32_t)));
                        s->write(mxasio::buffer(&val, sizeof(float)));

                    }
                    else {
                        spdlog::warn("[CTRL] client {} tried to GET_TEMP_POWER for device {} but it does not support power readings", header.client_id,
                                     msg_tpow.device_id);
                        msg_status.s = MX::RPC::INFO_DEVICE_DOESNT_DO_POWER;
                        msg_status.dat = 0;
                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        continue;
                    }
                }
                else {
                    spdlog::warn("[CTRL] client {} sent invalid GET_TEMP_POWER type {}", header.client_id, static_cast<uint32_t>(msg_tpow.type));
                    msg_status.s = MX::RPC::INVALID_CTRL_COMMAND;
                    msg_status.dat = 0;
                    status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                }

            }


        }

        // PRESSURE REQUEST PACKET
        //-------------------------------------------------------------------------------
        else if(header.msg_type == MX::RPC::MSG_TYPE_GET_UTILIZATION) {
            // check for client ID mismatch
            if(header.client_id != my_client_id) {
                spdlog::warn("[CTRL] client {} tried to GET_DEVICE_INFO with ID {}", header.client_id, my_client_id);
                msg_status.s = MX::RPC::YOU_LIED_ABOUT_YOUR_ID;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                break;
            }

            // read the device_id
            int32_t device_id = -1;
            rbytes = s->read(mxasio::buffer(&device_id, sizeof(int32_t)), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                break;
            }

            if(UNLIKELY(device_id < 0 || device_id >= all_devices_count)) {
                spdlog::warn("[CTRL] client {} tried to GET_PRESSURE with invalid device ID {}", header.client_id, device_id);
                msg_status.s = MX::RPC::INVALID_DEVICE;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                continue;
            }
            else {

                DFPExecutor* exe = nullptr;
                {
                    std::shared_lock<std::shared_mutex> extlock(m_executor_table);
                    if(all_dfp_executors.count(device_id) == 0) {
                        spdlog::warn("[CTRL] client {} tried to GET_PRESSURE for device {} but it is not in the executor table", header.client_id, device_id);
                        msg_status.s = MX::RPC::INVALID_DEVICE;
                        msg_status.dat = 0;
                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        continue;
                    }
                    else {
                        exe = all_dfp_executors[device_id];
                    }
                    extlock.unlock();
                }

                // check if the device is locked in the lock table
                uint32_t owner_id = 0xDEADBEEF;
                if(LIKELY(lock_table.check_lock(device_id, &owner_id))) {
                    if(UNLIKELY(owner_id != 0)) {
                        // this is owned by another client, so we cannot get the temp/power
                        spdlog::warn("[CTRL] client {} tried to GET_PRESSURE for device {} but it is in Local mode and owned by client {}",
                                     header.client_id, device_id, owner_id);
                        msg_status.s = MX::RPC::INFO_DEVICE_IS_LOCAL_LOCKED;
                        msg_status.dat = 0;
                        status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                        continue;
                    }
                }

                float val = exe->avg_pressure();
                // send the reply
                msg_status.s = MX::RPC::OK;
                memcpy(&(msg_status.dat), &val, sizeof(float));
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);

            }
        }


        // SET POWERMODE REQUEST
        //-------------------------------------------------------------------------------
        else if(header.msg_type == MX::RPC::MSG_TYPE_SET_POWERMODE) {
            // check for client ID mismatch
            if(header.client_id != my_client_id) {
                spdlog::warn("[CTRL] client {} tried to SET_POWERMODE with ID {}", header.client_id, my_client_id);
                msg_status.s = MX::RPC::YOU_LIED_ABOUT_YOUR_ID;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                break;
            }

            // read the device_id
            int32_t device_id = -1;
            rbytes = s->read(mxasio::buffer(&device_id, sizeof(int32_t)), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                break;
            }

            // read the requested frequency
            uint16_t req_freq = 0;
            rbytes = s->read(mxasio::buffer(&req_freq, sizeof(uint16_t)), error);
            if(UNLIKELY(rbytes == 0 || error)) {
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message());
                break;
            }

            // validate that the req_freq frequency is an option in the MxFrequencyOption enum
            if(UNLIKELY(MX::Types::is_valid_frequency_option(req_freq) == false)) {
                spdlog::warn("[CTRL] client {} tried to SET_POWERMODE with invalid frequency option {}", header.client_id, req_freq);
                msg_status.s = MX::RPC::SET_POWERMODE_INVALID_OPTION;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                continue;
            }

            // set the next power mode for the device
            MX::Types::MxFrequencyOption freq_option = static_cast<MX::Types::MxFrequencyOption>(req_freq);
            DFPExecutor* exe = nullptr;
            {
                std::shared_lock<std::shared_mutex> extlock(m_executor_table);
                if(all_dfp_executors.count(device_id) == 0) {
                    extlock.unlock();
                    spdlog::warn("[CTRL] client {} tried to SET_POWERMODE for device {} but it is not in the executor table", header.client_id, device_id);
                    msg_status.s = MX::RPC::INVALID_DEVICE;
                    msg_status.dat = 0;
                    status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                    continue;
                }
                else {
                    exe = all_dfp_executors[device_id];

                    exe->set_next_power_mode(freq_option);
                    extlock.unlock();

                    // reply success
                    msg_status.s = MX::RPC::OK;
                    msg_status.dat = 0;
                    status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                }
            }

        }

        // DEV INFO REQUESTS
        //-------------------------------------------------------------------------------
        else if(header.msg_type == MX::RPC::MSG_TYPE_GET_DEV_INFO) {
            // check for client ID mismatch
            if(header.client_id != my_client_id) {
                spdlog::warn("[CTRL] client {} tried to GET_DEVICE_INFO with ID {}", header.client_id, my_client_id);
                msg_status.s = MX::RPC::YOU_LIED_ABOUT_YOUR_ID;
                msg_status.dat = 0;
                status_reply(s, my_client_id, msg_status.s, msg_status.dat);
                break;
            }

            // immediately reply with the number of devices as a simple uint32_t
            s->write(mxasio::buffer(&all_devices_count, sizeof(int32_t)), error);
            if(UNLIKELY(error)) {
                spdlog::error("[CTRL] control connection from client {} error: {}", my_client_id, error.message());
                break;
            }

            // if all_devices_count is 0, we can just return
            if(all_devices_count == 0) {
                spdlog::warn("[CTRL] client {} requested device info, but there are no devices", my_client_id);
                continue;
            }

            // then reply that many times, with the device_info_t data one at a time
            std::shared_lock<std::shared_mutex> devlock(devinfo_lock);
            for(int32_t i = 0; i < all_devices_count; i++) {

                // send chip count
                s->write(mxasio::buffer(&(devinfo_table[i].chip_count), sizeof(int32_t)), error);
                if(UNLIKELY(error)) {
                    spdlog::error("[CTRL] control connection from client {} error: {}", my_client_id, error.message());
                    devlock.unlock();
                    break;
                }

                // send current_config
                s->write(mxasio::buffer(&(devinfo_table[i].current_config), sizeof(int32_t)), error);
                if(UNLIKELY(error)) {
                    spdlog::error("[CTRL] control connection from client {} error: {}", my_client_id, error.message());
                    devlock.unlock();
                    break;
                }

                // send num_groups
                s->write(mxasio::buffer(&(devinfo_table[i].num_groups), sizeof(int32_t)), error);
                if(UNLIKELY(error)) {
                    spdlog::error("[CTRL] control connection from client {} error: {}", my_client_id, error.message());
                    devlock.unlock();
                    break;
                }

                // send chips_per_group
                s->write(mxasio::buffer(&(devinfo_table[i].chips_per_group), sizeof(int32_t)), error);
                if(UNLIKELY(error)) {
                    spdlog::error("[CTRL] control connection from client {} error: {}", my_client_id, error.message());
                    devlock.unlock();
                    break;
                }

                // send can_get_power_data
                s->write(mxasio::buffer(&(devinfo_table[i].can_get_power_data), 1), error);
                if(UNLIKELY(error)) {
                    spdlog::error("[CTRL] control connection from client {} error: {}", my_client_id, error.message());
                    devlock.unlock();
                    break;
                }

                // for each chip in freqs, send freq
                for(int32_t j = 0; j < devinfo_table[i].chip_count; j++) {
                    s->write(mxasio::buffer(&(devinfo_table[i].freqs[j]), sizeof(uint16_t)), error);
                    if(UNLIKELY(error)) {
                        spdlog::error("[CTRL] control connection from client {} error: {}", my_client_id, error.message());
                        devlock.unlock();
                        break;
                    }
                }

                // finally, send voltage
                s->write(mxasio::buffer(&(devinfo_table[i].volt), sizeof(uint16_t)), error);
                if(UNLIKELY(error)) {
                    spdlog::error("[CTRL] control connection from client {} error: {}", my_client_id, error.message());
                    devlock.unlock();
                    break;
                }

            }

            // unlock the device info lock
            devlock.unlock();



        }

        // INVALID COMMAND
        //-------------------------------------------------------------------------------
        else {
            // invalid! send back an invalid command error
            spdlog::warn("[CTRL] client {} sent invalid command type {}", my_client_id, static_cast<uint32_t>(header.msg_type));
            msg_status.s = MX::RPC::INVALID_CTRL_COMMAND;
            msg_status.dat = 0;
            status_reply(s, my_client_id, msg_status.s, msg_status.dat);
            break;
        }

        //printf("\nCurrent status:\n");
        //id_list.print();
        //printf("\n");

    } // while alive

cleanup:
    // extra sanity check to remove dangling IDs
    meta->alive = false;

    // for all ofmap_queues and ofmap_freelists in my meta->client_context,
    // do a .notify() to wake up any threads waiting on *alive
    if(meta->my_client_context != nullptr) {
        meta->my_client_context->ping_all_queues();
    }

    // Wait for the ifmap session to finish.
    // ifmap_socket is nullptr if init_connection failed.
    // Otherwise, block until ifmap_session_finish is true.
    {
        std::unique_lock<std::mutex> zlock(meta->ifmap_session_lock);
        spdlog::debug("[CTRL] waiting for ifmap session to finish for client {}", my_client_id);
        meta->ifmap_session_cv.wait(zlock, [&meta]() {
            return (meta->ifmap_socket == nullptr || meta->ifmap_session_finish == true);
        });
        zlock.unlock();
    }

    // Wait for the ofmap session to finish.
    // ofmap_socket is nullptr if init_connection failed.
    // Otherwise, block until ofmap_session_finish is true.
    {
        std::unique_lock<std::mutex> zlock(meta->ofmap_session_lock);
        spdlog::debug("[CTRL] waiting for ofmap session to finish for client {}", my_client_id);
        meta->ofmap_session_cv.wait(zlock, [&meta]() {
            return (meta->ofmap_socket == nullptr || meta->ofmap_session_finish == true);
        });
        zlock.unlock();
    }


    // Wait for pending frames finish processing, so that we can safely remove the client
    //
    // About client remove synchronization strategy, pls refer to this issue comment:
    // https://github.com/memryx/MX_API/pull/239#discussion_r2271895396
    {
        ContextClient* my_client = meta->my_client_context;
        if(my_client != nullptr) {
            std::unique_lock<std::mutex> lock(my_client->pending_frame_lock);
            my_client->pending_frame_cv.wait(lock, [&my_client]() {
                return my_client->pending_frame_cnt == 0;
            });
        }
    }

    // decrement the client_ref_count of my dfp (if present),
    // and remove the client from the DFPContext
    {
        // dfp context might be already deleted in scheduler thread
        if(meta->my_dfp_context != nullptr) {

            spdlog::debug("[CTRL] attempt to remove client {}", my_client_id);

            // remove the client from the DFPContext
            meta->my_dfp_context->client_ref_count--;
            if(meta->my_dfp_context->get_mctx(meta->submodel_id)->remove_client(my_client_id)) {
                spdlog::debug("[CTRL] client {} removed from DFPContext with hash {}, submodel_id {}", my_client_id,
                              MX::sha512::to_base64(meta->my_dfp_context->hash), meta->submodel_id);
            }
            else {
                spdlog::error("[CTRL] failed to remove client from DFPContext with hash {}, submodel_id {}",
                              MX::sha512::to_base64(meta->my_dfp_context->hash), meta->submodel_id);
            }

            // the scheduler thread will take care of removing the DFPContext if it is no longer needed

        }
        else {
            // probably in Local Mode
            spdlog::debug("[CTRL] client {} has no DFPContext", my_client_id);
        }
    }

    // close ifmap ofmap sockets (if present)
    if(meta->ifmap_socket != nullptr) {
        delete meta->ifmap_socket;
        meta->ifmap_socket = nullptr;
    }
    if(meta->ofmap_socket != nullptr) {
        delete meta->ofmap_socket;
        meta->ofmap_socket = nullptr;
    }
    if(meta->ctrl_socket != nullptr) {
        delete meta->ctrl_socket;
        meta->ctrl_socket = nullptr;
    }

    // remove any dangling lock_table locks
    lock_table.clear_client(my_client_id);

    {
        std::lock_guard<std::mutex> lock(meta_lock);
        delete meta;
        client_meta.erase(my_client_id);
    }
    id_list.retire(my_client_id);


    spdlog::info("[CTRL] control connection closed for client {}", my_client_id);

} // end



//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
// IFMAP Port: input feature maps
//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------

// IFMAP connection handler
void Server::ifmap_endpoint_listener()
{
    try {
        // io context
        mxasio::io_context io_context;
        std::string addr_ = this->addr;
        unsigned short port_ = this->base_port;

        // Listener creation
        MX::RPC::Listener listener(io_context, addr_, port_ + 1);
        ifmap_listener_ptr = &listener;

        spdlog::info("[IFMAP] ifmap listener running on {}:{}", addr_, port_ + 1);

        while(running == true) {
            MX::RPC::Socket* socket = listener.accept();

            spdlog::debug("[IFMAP] ifmap connection from {}", socket->remote_endpoint());

            // get the client's client_id to connect to the right ClientMeta pointer
            MX::RPC::MsgHeader h;
            size_t rbytes = socket->read(mxasio::buffer(&h, sizeof(h)));
            if(rbytes == 0) {
                spdlog::error("[IFMAP] ifmap connection from {} disconnected early", socket->remote_endpoint());
                delete socket;
                continue;
            }
            if(h.msg_type != MX::RPC::MSG_TYPE_FMAP_IN_INIT) {
                spdlog::error("[IFMAP] ifmap connection from {} sent invalid message type {}", socket->remote_endpoint(),
                              static_cast<uint32_t>(h.msg_type));
                delete socket;
                continue;
            }

            // look up the .client_id and get a pointer to the ClientMeta
            ClientMeta* meta = nullptr;
            {
                std::lock_guard<std::mutex> lock(meta_lock);
                if(client_meta.count(h.client_id) == 0) {
                    spdlog::error("[IFMAP] ifmap connection from {} client ID {} is not found", socket->remote_endpoint(), h.client_id);
                    delete socket;
                    continue;
                }

                // get the ClientMeta pointer
                meta = client_meta[h.client_id];

                // Exception occurs when the client init connection (e.g. program stop or crash in the middle of init connection)
                if (meta->alive == false) {
                    spdlog::error("[IFMAP] ifmap connection from {} client ID {} is not alive", socket->remote_endpoint(), h.client_id);
                    delete socket;
                    continue;
                }

                // set the ifmap socket in the ClientMeta
                meta->ifmap_socket = socket;
            }

            // launch the ifmap session thread
            std::thread(&Server::ifmap_session, this, socket, meta).detach();

        }
    }
    catch (std::exception &e) {
        spdlog::error("[IFMAP] ifmap endpoint exception: {}", e.what());
    }
}

// Handles a client process's connection to the ifmap socket
void Server::ifmap_session(MX::RPC::Socket* s, ClientMeta* meta)
{
    mxasio::error_code           error;

    size_t rbytes;
    ContextClient* my_client_context = meta->my_client_context;
    ModelContext* my_model_context = meta->my_model_context;
    uint32_t my_client_id = meta->id;

    IomapItem* item = nullptr;

    // non-stop unidirectional data flood, woohoo!
    while(LIKELY(meta->alive == true && running == true)) {

        // get a free IomapItem ptr from the ModelContext's ifmap_freelist
        my_model_context->ifmap_freelist->pop(item);

        if(UNLIKELY(item == nullptr)) {
            spdlog::warn("[IFMAP] ifmap connection from {} popped NULLPTR item from freelist", s->remote_endpoint());
            break;
        }

        // set this client as the dest
        item->dest_client = my_client_context;

        for(uint32_t i = 0; i < item->num_fmaps; i++) {
            // read the data
            rbytes = s->read(mxasio::buffer(item->data[i], item->sizes[i]), error);
            if(UNLIKELY(rbytes == 0 || error)) {

                // return to freelist
                item->dest_client = nullptr;
                my_model_context->ifmap_freelist->push(item);

                // if error is EOF, exit this thread
                if(error == mxasio::error::eof) {
                    spdlog::debug("[IFMAP] ifmap connection from {} closed", s->remote_endpoint());
                }
                else {
                    // otherwise, print the error and return to the freelist
                    spdlog::error("[IFMAP] ifmap connection from {} error: {}", s->remote_endpoint(), error.message());
                }

                // yeah yeah, "goto is lazy" and all that but it's good for exception handling
                goto icleanup;
            }
        }

        // push the item to the ModelContext's ifmap_queue
        my_client_context->increment_pending_frames();
        my_model_context->ifmap_queue->push(item);

        item = nullptr;
    }

icleanup:
    meta->alive = false;

    // finish ifmap_session
    {
        spdlog::debug("[IFMAP] ifmap session for client {} closing...", my_client_id);
        std::unique_lock<std::mutex> lock(meta->ifmap_session_lock);
        meta->ifmap_session_finish = true;

        meta->ifmap_session_cv.notify_all(); // notify the control thread that we are done
        lock.unlock();
    }

    spdlog::debug("[IFMAP] ifmap session for client {} CLOSED.", my_client_id);

}


//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
// OFMAP Port: output feature maps
//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------

// IFMAP connection handler
void Server::ofmap_endpoint_listener()
{
    try {
        // io context
        mxasio::io_context io_context;
        std::string addr_ = this->addr;
        unsigned short port_ = this->base_port;

        // Listener creation
        MX::RPC::Listener listener(io_context, addr_, port_ + 2);
        ofmap_listener_ptr = &listener;

        spdlog::info("[OFMAP] ofmap listener running on {}:{}", addr_, port_ + 2);

        while(running == true) {
            MX::RPC::Socket* socket = listener.accept();

            spdlog::debug("[OFMAP] ofmap connection from {}", socket->remote_endpoint());

            // get the client's client_id to connect to the right ClientMeta pointer
            MX::RPC::MsgHeader h;
            size_t rbytes = socket->read(mxasio::buffer(&h, sizeof(h)));
            if(rbytes == 0) {
                spdlog::error("[OFMAP] ofmap connection from {} disconnected early", socket->remote_endpoint());
                delete socket;
                continue;
            }
            if(h.msg_type != MX::RPC::MSG_TYPE_FMAP_OUT_INIT) {
                spdlog::error("[OFMAP] ofmap connection from {} sent invalid message type {}", socket->remote_endpoint(),
                              static_cast<uint32_t>(h.msg_type));
                delete socket;
                continue;
            }

            // look up the .client_id and get a pointer to the ClientMeta
            ClientMeta* meta = nullptr;
            {
                std::lock_guard<std::mutex> lock(meta_lock);
                if(client_meta.count(h.client_id) == 0) {
                    spdlog::error("[OFMAP] ofmap connection from {} client ID {} is not found", socket->remote_endpoint(), h.client_id);
                    delete socket;
                    continue;
                }

                // get the ClientMeta pointer
                meta = client_meta[h.client_id];

                // Exception occurs when the client init connection (e.g. program stop or crash in the middle of init connection)
                if (meta->alive == false) {
                    spdlog::error("[OFMAP] ofmap connection from {} client ID {} is not alive", socket->remote_endpoint(), h.client_id);
                    delete socket;
                    continue;
                }

                // set the ofmap socket in the ClientMeta
                meta->ofmap_socket = socket;
            }

            // launch the ofmap session thread
            std::thread(&Server::ofmap_session, this, socket, meta).detach();

        }
    }
    catch (std::exception &e) {
        spdlog::error("[OFMAP] ofmap endpoint exception: {}", e.what());
    }
}

// Handles a client process's connection to the ofmap socket
void Server::ofmap_session(MX::RPC::Socket* s, ClientMeta* meta)
{
    mxasio::error_code           error;

    size_t rbytes;
    ContextClient* my_client_context = meta->my_client_context;
    ModelContext* my_model_context = meta->my_model_context;
    uint32_t my_client_id = meta->id;

    IomapItem* item = nullptr;

    bool use_smoothing = meta->client_options.smoothing;
    float min_time_between_outputs = 1000.0f / meta->client_options.fps_target; // in milliseconds

    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    // non-stop unidirectional data flood, woohoo!
    while(LIKELY(meta->alive == true && running == true)) {

        // Add the "smoothing" delay here: make sure at least min_time_between_outputs milliseconds
        // have passed since the last time we got to the top of this loop
        if(use_smoothing) {
            // if smoothing is enabled, we need to wait for the next time point
            std::this_thread::sleep_until(start_time + std::chrono::milliseconds(static_cast<int64_t>(min_time_between_outputs)));

            // wake up and save start_time for the next iteration
            start_time = std::chrono::steady_clock::now();
        }


        // pop the next intended driver ctx from the client's driver_ctx_fifo
        uint8_t next_driver_ctx = 0xFF;
        if(my_client_context->driver_ctx_fifo->pop(next_driver_ctx) == false) {
            // if we can't pop, then we are done with this session
            spdlog::info("[OFMAP] driver_ctx_fifo pop was killed, exiting ofmap session for client {}", meta->id);
            if(meta->alive == true) {
                spdlog::error("[OFMAP] we should not have reached this point with alive == true");
            }
            break;
        }

        if(UNLIKELY(next_driver_ctx == 0xFF)) {
            spdlog::warn("[OFMAP] ofmap connection from {} popped invalid driver_ctx 0xFF", s->remote_endpoint());
            break;
        }

        // the server might shut down while we were waiting
        if(UNLIKELY(running == false)) {
            spdlog::warn("[OFMAP] server shutting down, exiting ofmap session for client {}", meta->id);
            break;
        }

        // pop from my corresponding ofmap_queue
        if(my_client_context->ofmap_queues->at(next_driver_ctx)->pop(item) == false) {
            spdlog::warn("[OFMAP] ofmap connection from {} client died before we could continue", s->remote_endpoint());
            break;
        }
        if(UNLIKELY(item == nullptr)) {
            spdlog::warn("[OFMAP] ofmap connection from {} popped NULLPTR item from queue", s->remote_endpoint());
            break;
        }

        // for all the fmaps, mxasio::write to send the data to the client
        for(uint32_t i = 0; i < item->num_fmaps; i++) {
            rbytes = s->write(mxasio::buffer(item->data[i], item->sizes[i]), error);
            if(UNLIKELY(rbytes == 0 || error)) {

                // if error is EOF, exit this thread
                if(error == mxasio::error::eof) {
                    spdlog::debug("[OFMAP] ofmap connection from {} closed", s->remote_endpoint());
                }
                else {
                    // otherwise, print the error and return to the freelist
                    spdlog::error("[OFMAP] ofmap connection from {} error: {}", s->remote_endpoint(), error.message());
                }

                goto ocleanup;
            }
        }

        // then push the item back to the ModelContext's ofmap_freelist
        item->dest_client = nullptr;
        my_client_context->ofmap_freelists->at(next_driver_ctx)->push(item);

        item = nullptr;
    }

ocleanup:
    meta->alive = false;

    // finish ofmap_session
    {
        spdlog::debug("[OFMAP] ofmap session for client {} closing...", my_client_id);
        std::unique_lock<std::mutex> lock(meta->ofmap_session_lock);
        meta->ofmap_session_finish = true;

        meta->ofmap_session_cv.notify_all(); // notify the control thread that we are done
        lock.unlock();
    }

    spdlog::debug("[OFMAP] ofmap session for client {} CLOSED.", my_client_id);

}



//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
// SCHEDULER
//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------

// executor_threads: pops tasks from my executor thread, run them in my
// associated DFPExecutor, then push the Task back to the scheduler_queue
void Server::executor_thread(uint8_t device_id_)
{
    ExecutorTask* task = nullptr;
    ExecutorTask* last_task = nullptr;

    DFPContext* dfp_ctx = nullptr;

    DFPExecutor my_exec(device_id_, &devinfo_table, &(executor_queues[device_id_]), hw_monitor_interval_ms);
    {
        std::unique_lock<std::shared_mutex> lock(m_executor_table);
        all_dfp_executors[device_id_] = &my_exec;
        lock.unlock();
    }

    // don't lock anything until we get a task
    bool currently_locked = false;

    while(running == true) {
        // pop a task from the scheduler queue with 1s timeout
        if(executor_queues[device_id_].pop_timeout(task, 1000)) {
            if(!currently_locked) {
                // if we weren't locked, then we need to lock the device
                if(lock_table.trylock(device_id_, 0)) {
                    currently_locked = true;
                    last_task = nullptr;
                    spdlog::debug("[EXECUTOR {}] Device {} is now open & locked and ready to run tasks", device_id_, device_id_);
                }
                else {
                    // we got a task, but this device is busy..
                    // push it back to the scheduler queue so it can try to assign
                    // to a different device (if available)
                    spdlog::debug("[EXECUTOR {}] Device {} is busy with Local Mode, pushing task back to scheduler queue", device_id_, device_id_);
                    if(task == last_task) {
                        // if the same task is being pushed back over and over here,
                        // we need to slow things down until that local lock releases
                        spdlog::info("[EXECUTOR {}] Device {} is local-locked and still busy. Slowing down scheduler thread", device_id_, device_id_);
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    }

                    last_task = task;
                    scheduler_queue.push(task);
                    task = nullptr;
                    continue;
                }
            }

            // run the task in the executor
            my_exec.run_dfp(task);

            // push the task back to the scheduler queue
            scheduler_queue.push(task);
            task = nullptr;
        }
        else {
            // if we timed out, then we need to unlock the device
            if(currently_locked) {
                spdlog::debug("[EXECUTOR {}] Device {} is idle, closing & unlocking it for now", device_id_, device_id_);

                // reset to default frequency
                my_exec.set_next_power_mode(MX::Types::MxFrequencyOption::FREQ_USE_CONF);

                if(my_exec.close_device() == false) {
                    spdlog::error("[EXECUTOR {}] Device {} failed to close device", device_id_, device_id_);
                }
                lock_table.unlock(device_id_, 0);
                currently_locked = false;
            }
        }
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_executor_table);
        // remove this executor from the all_dfp_executors map
        all_dfp_executors.erase(device_id_);
        lock.unlock();
        spdlog::info("[EXECUTOR {}] Device {} is shutting down, removing from executor table", device_id_, device_id_);
        return;
    }
}


// scheduler thread: pops tasks from the scheduler queue and pushes them to the executor queues
void Server::scheduler_thread_func()
{

    ExecutorTask* task = nullptr;

    // simple device round-robin, start at 0
    int32_t device_id = 0;

    while(running == true) {
        // pop a task from the scheduler queue
        if(scheduler_queue.pop_timeout(task, 5000 /* 5s timeout for checking for shutdowns */)) {

            {
                std::unique_lock<std::mutex> dlock(dfp_contexts_lock);
                
                // if the DFPContext of this task has 0 clients, delete the task
                if(task->dfp_ctx->client_ref_count.load() <= 0) {
                    spdlog::debug("[SCHEDULER] Task with DFPContext {} has no clients, deleting task", MX::sha512::to_base64(task->dfp_ctx->hash));

                    task->dfp_ctx->exec_ref_count--;
                    if(task->dfp_ctx->exec_ref_count == 0) {
                        // no more executors, delete the DFPContext
                        spdlog::info("[SCHEDULER] DFPContext {} has no more executors, deleting it", MX::sha512::to_base64(task->dfp_ctx->hash));

                        // go through dfp_ctx->device2context_table, and for each device_id [key],
                        // call that Executor's close_ctx() method on the ctx_id [value]
                        for(auto &pair : task->dfp_ctx->device2context_table) {
                            uint8_t device_id = pair.first;
                            DFPExecutor* executor = all_dfp_executors[device_id];
                            if(executor != nullptr) {
                                spdlog::debug("[SCHEDULER] Closing DFPContext {} on device {}", MX::sha512::to_base64(task->dfp_ctx->hash), device_id);
                                executor->close_ctx(pair.second);
                            }
                            else {
                                spdlog::error("[SCHEDULER] No executor found for device {}, skipping close_ctx()", device_id);
                            }
                        }

                        dfp_contexts.erase(task->dfp_ctx->hash);
                        delete task->dfp_ctx;
                        task->dfp_ctx = nullptr;
                        dlock.unlock();
                    }
                    delete task;
                    task = nullptr;
                    continue;
                }

            }

            // find an allowed device_id for the task and push there
            if(task->allowed_device >= 0 && task->allowed_device < all_devices_count) {
                // have to push to the required device only
                executor_queues[task->allowed_device].push(task);
                spdlog::debug("[SCHEDULER] Pushed task to executor queue {}", task->allowed_device);
            }
            else {
                // else we round-robin where we push
                device_id = (device_id + 1) % all_devices_count;
                executor_queues[device_id].push(task);
                spdlog::debug("[SCHEDULER] Pushed task to executor queue {}", device_id);
            }
        }
    }

}


} // namespace Manager
} // namespace MX
