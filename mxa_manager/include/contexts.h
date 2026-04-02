// Copyright (c) 2025 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef CONTEXTS_H
#define CONTEXTS_H

#pragma once
#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <cstring>
#include <map>
#include <shared_mutex>
#include <unordered_map>
#include <filesystem>
#include <utility>
#include <tuple>

#include "mxasio.hpp"

#include <memx/accl/dfp.h>
#include <memx/accl/messages.h>
#include <memx/accl/utils/locked_var.h>
#include <memx/accl/utils/macros.h>
#include <memx/accl/utils/sha512.h>
#include <memx/accl/utils/id_tracker.h>
#include <memx/accl/utils/blocky_queue.h>

using mxasio::ip::tcp;

using namespace MX::RPC;
using namespace MX::Utils;
using namespace MX::sha512;

namespace MX
{
namespace Manager
{

struct ContextClient;

struct IomapItem {
    IomapItem(uint32_t num_fmaps_, uint64_t* sizes_)
    {
        num_fmaps = num_fmaps_;
        sizes = new uint64_t[num_fmaps];
        std::memcpy(sizes, sizes_, num_fmaps * sizeof(uint64_t));
        data = new uint8_t* [num_fmaps];
        for(uint32_t i = 0; i < num_fmaps; i++) {
            data[i] = new uint8_t[sizes[i]];
        }
    }
    ~IomapItem()
    {
        if(data != nullptr) {
            for(uint32_t i = 0; i < num_fmaps; i++) {
                if(data[i] != nullptr) {
                    delete [] data[i];
                    data[i] = nullptr;
                }
            }
            delete [] data;
            data = nullptr;
        }

        if(sizes != nullptr) {
            delete [] sizes;
            sizes = nullptr;
        }
    }

    uint32_t num_fmaps;
    uint64_t* sizes;

    // actual data
    // (2D array: [port][data])
    uint8_t** data;

    // for debugging
    uint32_t client_id;

    // copy of a ptr destination to write to
    // order is <freelist,queue>
    // (avoids having to access a shared structure)
    ContextClient* dest_client;
};


// tracks clients that are part of this context
class ContextClient
{
  public:
    // client ID and driver context ordering queue
    uint32_t                id;
    BQExtFlag<uint8_t>*     driver_ctx_fifo;

    // used for sizing and sorting ofmap buffers
    // in case of multi-device systems
    // -----------------------------------------
    std::vector<uint8_t> allowed_driver_ctxs;

    // ofmap data and queues are per-client
    // -----------------------------------------
    uint32_t                   obuffer_size;
    IomapItem**                ofmap_buffers;
    std::unordered_map<uint8_t, BQExtFlag<IomapItem*>*>* ofmap_queues;
    std::unordered_map<uint8_t, BQExtFlag<IomapItem*>*>* ofmap_freelists;

    explicit ContextClient(uint32_t id_, uint32_t obuffer_size_, uint32_t num_ofmaps_, uint64_t* ofmap_sizes,
                           std::vector<uint8_t> allowed_driver_ctxs_, SharedLockedVar<bool>* alive_flag);

    ~ContextClient();

    void ping_all_queues();
    void increment_pending_frames();
    void decrement_pending_frames();

    std::shared_mutex sm; // [s]hared [m]utex for thread-safe access this
    std::condition_variable_any cv_sm;

    // Frame is considered "in processing" from the moment it is pushed to the ifmap queue
    // until it completes processing in the output loop.
    int pending_frame_cnt;
    std::mutex       pending_frame_lock;
    std::condition_variable pending_frame_cv;
};


class CtxBlockyQueue; // forward declaration
class ModelContext
{
  public:
    ModelContext(uint32_t ibuffer_size_, uint32_t obuffer_size_, uint32_t num_ifmaps_, uint32_t num_ofmaps_, uint64_t* ifmap_sizes_,
                 uint64_t* ofmap_sizes_, std::vector<uint8_t> allowed_driver_ctxs_);
    ~ModelContext();


    // ---- DFP info ----
    // ------------------
    uint32_t num_ifmaps, num_ofmaps;
    uint64_t* ifmap_sizes;
    uint64_t* ofmap_sizes;


    // used for sizing and sorting ofmap buffers
    // in case of multi-device systems
    // -----------------------------------------
    std::vector<uint8_t> allowed_driver_ctxs;

    // ---- client tracking ----
    // -------------------------
    // stores mapping of ID -> ContextClient
    std::unordered_map<uint32_t, ContextClient*> clients;
    std::unordered_set<ContextClient*> clients_set;
    std::mutex client_table_lock;

    // creates a new client entry (including ofmap queues)
    ContextClient* add_client(uint32_t id, SharedLockedVar<bool>* alive_flag);

    bool is_client_existed(ContextClient* client);
    bool is_client_list_empty();

    std::vector<int> get_client_ids();

    // fetch existing clientmeta ptr
    ContextClient* get_meta(uint32_t id);

    // shutdown and delete a client entry
    bool remove_client(uint32_t id);

    // print current clients list
    void print_clients();

    // ----- input queue -----
    // -----------------------
    uint32_t                     ibuffer_size;
    uint32_t                     obuffer_size;
    IomapItem**                  ifmap_buffers;
    BlockyQueue<IomapItem*>*     ifmap_freelist;
    CtxBlockyQueue*              ifmap_queue;
};

// ifmap_queue is a BlockyQueue derviative that pushes
// the given ctx_id to the IomapItem->ContextClient->driver_ctx_fifo

class CtxBlockyQueue : public BlockyQueue<IomapItem*>
{
  public:
    explicit CtxBlockyQueue(ModelContext* mctx) : BlockyQueue<IomapItem*>(), mctx_(mctx) {}

    // bool pop_with_ctxpush(IomapItem* &ret, uint8_t ctx_id)
    // {
    //     std::unique_lock<std::mutex> lock(this->m);
    //     s_not_empty.wait(lock, [this] { return (!(this->q.empty())) || this->kill; });
    //     if(UNLIKELY(this->q.empty())) { return; } // return if kill was true

    //     // pop
    //     ret = this->q.front();
    //     this->q.pop_front();

    //     // wake up anyone waiting on full
    //     this->s_not_full.notify_one();

    //     if (mctx_->is_client_existed(ret->dest_client) == false) {
    //         // client no longer exists
    //         lock.unlock();
    //         return false;
    //     }

    //     // push the ctx_id to the driver's fifo
    //     ret->dest_client->driver_ctx_fifo->push(ctx_id);

    //     // clear lock
    //     lock.unlock();

    //     return true;
    // }

    // returns a pair of bools: {is_timeout, client_existed}
    bool pop_timeout_with_ctxpush(IomapItem* &ret, unsigned int timeout_ms, uint8_t ctx_id)
    {
        std::unique_lock<std::mutex> lock(this->m);
        bool got_data;

        if(timeout_ms > 0) {
            got_data = this->s_not_empty.wait_for(
                           lock,
                           std::chrono::milliseconds(timeout_ms),
                           [this] { return (!(this->q.empty())) || this->kill; }
                       );
        }
        else {
            this->s_not_empty.wait(
                lock,
                [this] { return (!(this->q.empty())) || this->kill; }
            );
            got_data = !(this->kill); // if we got here, it means we got data
        }

        if(UNLIKELY(!got_data)) { return false; }
        if(UNLIKELY(this->q.empty())) { return false; }
        ret = this->q.front();
        this->q.pop_front();
        this->s_not_full.notify_one();

        // push the ctx_id to the driver's fifo
        ret->dest_client->driver_ctx_fifo->push(ctx_id);

        lock.unlock();
        return true;
    }

  private:
    ModelContext* mctx_;
};

// this model's Port information
struct port_infos_t {
    uint8_t num_in;
    uint8_t num_out;
    uint8_t istart_idx;
    uint8_t istop_idx;
    uint8_t ostart_idx;
    uint8_t ostop_idx;
    uint64_t* iport_sizes;
    uint64_t* oport_sizes;
    ~port_infos_t()
    {
        if(iport_sizes != nullptr) {
            delete [] iport_sizes;
            iport_sizes = nullptr;
        }
        if(oport_sizes != nullptr) {
            delete [] oport_sizes;
            oport_sizes = nullptr;
        }
    }
};

struct dfp_info_t {
    Dfp::DfpObject* dfp;
    int num_models;
    int num_chips;
    size_t biggest_ofmap_bytes;
    // have start/stop in/out ports for each model (model index = vector index)
    std::vector<port_infos_t*> port_info;
    dfp_info_t(int num_models_)
    {
        num_models = num_models_;
        num_chips = -1; // invalid by default, must be set later
    }
    ~dfp_info_t()
    {
        // delete each port_infos_t obj in the vector
        for(auto it = port_info.begin(); it != port_info.end(); ++it) {
            if(*it != nullptr) {
                delete *it;
                *it = nullptr;
            }
        }
    }
};


class DFPContext
{

  public:
    DFPContext(uint64_t dfp_raw_size_, uint8_t* dfp_bytes, hash_t h, uint32_t ibuffer_size_, uint32_t obuffer_size_, std::vector<uint8_t> devices_to_use_);
    ~DFPContext();

    ModelContext*  get_mctx(int idx);

    dfp_info_t*     info;
    uint64_t        dfp_raw_size;
    uint8_t*        raw_dfp_bytes;
    Dfp::DfpObject* dfp_obj;
    hash_t          hash;

    // sum of all clients of all models
    // when this reaches 0, we can start considering
    // deleting the DFPContext
    LockedVar<int> client_ref_count;

    // number of ExecutorTasks that are currently referencing this DFPContext
    LockedVar<int> exec_ref_count;

    // print the dfp info
    void print_info();

    // print all clients
    void print_clients();

    bool successful_init;

    // used for sizing and sorting ofmap buffers
    // in case of multi-device systems
    // -----------------------------------------
    std::vector<uint8_t> allowed_driver_ctxs;

    // map of devices <--> driver contexts
    std::unordered_map<uint8_t, uint8_t> device2context_table;

    // driver ctx ID has to be unique across all devices, so we declare a static tracker
    inline static MX::Utils::DriverIDTracker driver_ctx_tracker;

  private:
    void parse_dfp(Dfp::DfpObject* d);
    ModelContext** mcontexts;

};



}
}

#endif // CONTEXTS_H
