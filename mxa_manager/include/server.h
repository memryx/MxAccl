// Copyright (c) 2025 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef SERVER_H
#define SERVER_H

#pragma once
#include <string>
#include <vector>
#include <deque>
#include <stack>
#include <shared_mutex>
#include <thread>
#include <cstdint>
#include <cstring>
#include <array>
#include <map>
#include <unordered_map>
#include <filesystem>

#include "mxasio.hpp"

#include <memx/accl/messages.h>

#include <memx/accl/utils/mxTypes.h>
#include <memx/accl/utils/comm_sockets.h>
#include <memx/accl/utils/sha512.h>
#include <memx/accl/utils/locked_var.h>
#include <memx/accl/utils/blocky_queue.h>
#include <memx/accl/utils/id_tracker.h>

#include "dfp_executor.h"
#include "lock_table.h"

using mxasio::ip::tcp;

using namespace MX::Utils; // BlockyQueue, SharedLockedVar

// ----- THE BIG KAHUNA -----
//
// This defines the top-level object, which:
//
// 1. Detects and manages connected MXA Devices
// 2. Handles all network traffic and commands from clients (e.g. Local mode locks)
// 3. Schedules DFPContexts to the device DFPExecutors

namespace MX
{
namespace Manager
{

struct ClientMeta {
    SharedLockedVar<bool> alive;
    uint32_t              id;

    std::mutex              ifmap_session_lock;
    bool                    ifmap_session_finish;
    std::condition_variable ifmap_session_cv;
    std::mutex              ofmap_session_lock;
    bool                    ofmap_session_finish;
    std::condition_variable ofmap_session_cv;

    std::string      remote_endpoint;
    MX::RPC::Socket*  ctrl_socket;
    MX::RPC::Socket*  ifmap_socket;
    MX::RPC::Socket*  ofmap_socket;
    DFPContext*       my_dfp_context;
    ContextClient*    my_client_context;
    ModelContext*     my_model_context;
    int32_t          submodel_id;


    MX::RPC::ClientOptions client_options;

    ClientMeta(uint32_t id_)
    {
        id = id_;
        ctrl_socket = nullptr;
        ifmap_socket = nullptr;
        ofmap_socket = nullptr;
        my_dfp_context = nullptr;
        my_client_context = nullptr;
        ifmap_session_finish = false;
        ofmap_session_finish = false;
        alive = true;
    }

    ~ClientMeta()
    {
        if(ifmap_socket != nullptr) {
            delete ifmap_socket;
            ifmap_socket = nullptr;
        }
        if(ofmap_socket != nullptr) {
            delete ofmap_socket;
            ofmap_socket = nullptr;
        }
        if(ctrl_socket != nullptr) {
            delete ctrl_socket;
            ctrl_socket = nullptr;
        }
    }
};




class Server
{
  public:
    Server(std::string addr_, unsigned short base_port_, unsigned int hw_monitor_interval_ms_ = 500);
    ~Server();

    // Start and run the server
    void start();

    // Stop the server
    void kill();

//   private:
    SharedLockedVar<bool> running;

    //=====================================================
    // CONFIGURATION OPTIONS
    //=====================================================
    unsigned short base_port;
    std::string addr;
    const unsigned int hw_monitor_interval_ms;


    //=====================================================
    // CONNECTION LISTENERS
    //=====================================================

    // CTRL socket (default port 10000)
    void control_endpoint_listener();
    std::thread* ctrl_endpoint_thread;
    MX::RPC::Listener* ctrl_listener_ptr;

    // IFMAP socket (default port 10001)
    void ifmap_endpoint_listener();
    std::thread* ifmap_endpoint_thread;
    MX::RPC::Listener* ifmap_listener_ptr;

    // OFMAP socket (default port 10002)
    void ofmap_endpoint_listener();
    std::thread* ofmap_endpoint_thread;
    MX::RPC::Listener* ofmap_listener_ptr;


    //=====================================================
    // SESSION HANDLERS
    //=====================================================

    // spins up for each client CTRL connection
    void control_session(MX::RPC::Socket* s, ClientMeta* meta);

    // spins up for each client IFMAP connection
    void ifmap_session(MX::RPC::Socket* s, ClientMeta* meta);

    // spins up for each client OFMAP connection
    void ofmap_session(MX::RPC::Socket* s, ClientMeta* meta);


    //=====================================================
    // FUNCTIONS
    //=====================================================

    void status_reply(MX::RPC::Socket* s, uint32_t client_id, MX::RPC::status_t msg, uint32_t data);



    //=====================================================
    // TRACKING TABLES
    //=====================================================

    // total num detected devices
    int32_t all_devices_count;

    // client ID list
    MX::Utils::ClientIDTracker id_list;

    // device lock ownership table
    LockTable lock_table;

    // all the DFPContexts, indexed by hash
    std::mutex dfp_contexts_lock;
    std::map<MX::sha512::hash_t, DFPContext*> dfp_contexts;

    // all the client meta data, indexed by client ID
    std::mutex meta_lock;
    std::unordered_map<uint32_t, ClientMeta*> client_meta;

    // device info table
    std::shared_mutex devinfo_lock;
    std::vector<device_info_t> devinfo_table;


    //=====================================================
    // SCHEDULING
    //=====================================================
    // NOTE: it *IS* safe for multiple executors to run the same DFPContext!
    //       This is because the lower layers (ifmap/ofmap queues & freelists)
    //       are made to support potentially multiple executors accessing them.
    //       In fact, this is encouraged for multi-device load balancing.
    //
    std::thread** dfp_executor_threads;

    // one queue of tasks per executor
    // queue sizes are set very small (2, like a ping-pong)
    BlockyQueue<ExecutorTask*>* executor_queues;
    std::shared_mutex m_executor_table;
    std::unordered_map<uint8_t, DFPExecutor*> all_dfp_executors;
    void executor_thread(uint8_t device_id_);

    // scheduler thread
    std::thread* scheduler_thread;
    void scheduler_thread_func();
    BlockyQueue<ExecutorTask*> scheduler_queue;


};

}
}

#endif // SERVER_H
