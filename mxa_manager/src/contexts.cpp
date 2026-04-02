// Copyright (c) 2025 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <iostream>

#define MX_FMT_GBF80     0
#define MX_FMT_BF16      4
#define MX_FMT_FP32      5
#define MX_FMT_GBF80_ROW 6

#include "spdlog/spdlog.h"
#ifdef _WIN32
    #include <spdlog/sinks/win_eventlog_sink.h>
#endif

#include "contexts.h"

using namespace MX::Manager;
using namespace MX::Utils;
using namespace MX::sha512;

// ContextClient
//===================================================================


ContextClient::ContextClient(uint32_t id_, uint32_t obuffer_size_, uint32_t num_ofmaps_, uint64_t* ofmap_sizes,
                             std::vector<uint8_t> allowed_driver_ctxs_, SharedLockedVar<bool>* alive_flag)
{
    id = id_;

    allowed_driver_ctxs = allowed_driver_ctxs_;

    driver_ctx_fifo = new BQExtFlag<uint8_t>(UINT_MAX, alive_flag, false);

    ofmap_buffers  = new IomapItem*[obuffer_size_ * allowed_driver_ctxs.size()];

    // create the queues and freelists for each device
    ofmap_queues   = new std::unordered_map<uint8_t, BQExtFlag<IomapItem*>*>();
    ofmap_freelists = new std::unordered_map<uint8_t, BQExtFlag<IomapItem*>*>();

    for(uint8_t d : allowed_driver_ctxs) {
        ofmap_queues->insert({d, new BQExtFlag<IomapItem*>(obuffer_size_, alive_flag, false)});
        ofmap_freelists->insert({d, new BQExtFlag<IomapItem*>(obuffer_size_, alive_flag, false)});
    }

    for(uint8_t d = 0; d < allowed_driver_ctxs.size(); d++) {
        for(uint32_t i = 0; i < obuffer_size_; i++) {
            // create the ofmap buffers
            ofmap_buffers[(obuffer_size_ * d) + i] = new IomapItem(num_ofmaps_, ofmap_sizes);
            ofmap_freelists->at(allowed_driver_ctxs[d])->push(ofmap_buffers[(obuffer_size_ * d) + i]);
        }
    }

    obuffer_size = obuffer_size_ * allowed_driver_ctxs.size();
    pending_frame_cnt = 0;
}

void ContextClient::increment_pending_frames()
{
    std::unique_lock<std::mutex> lock(pending_frame_lock);
    pending_frame_cnt++;
    // spdlog::debug("[ContextClient] increment client {}. Current pending frame count: {}", id, pending_frame_cnt);
}

void ContextClient::decrement_pending_frames()
{
    std::unique_lock<std::mutex> lock(pending_frame_lock);
    pending_frame_cnt--;
    // spdlog::debug("[ContextClient] decrement client {}. Current pending frame count: {}", id, pending_frame_cnt);

    if (pending_frame_cnt == 0) {
        pending_frame_cv.notify_all();
    }
}

ContextClient::~ContextClient()
{
    // okay we're ready to delete everything
    if(ofmap_buffers != nullptr) {
        for(uint32_t i = 0; i < obuffer_size ; i++) {
            if(ofmap_buffers[i] != nullptr) {
                delete ofmap_buffers[i];
                ofmap_buffers[i] = nullptr;
            }
        }
        delete [] ofmap_buffers;
        ofmap_buffers = nullptr;
    }

    // delete all queues and freelists
    for(auto it = ofmap_queues->begin(); it != ofmap_queues->end(); ++it) {
        delete it->second;
        it->second = nullptr;
    }
    for(auto it = ofmap_freelists->begin(); it != ofmap_freelists->end(); ++it) {
        delete it->second;
        it->second = nullptr;
    }

    if(driver_ctx_fifo != nullptr) {
        delete driver_ctx_fifo;
        driver_ctx_fifo = nullptr;
    }

    // delete the queues and freelists themselves
    delete ofmap_queues;
    ofmap_queues = nullptr;
    delete ofmap_freelists;
    ofmap_freelists = nullptr;

    spdlog::info("[ContextClient] Client {} destroyed", id);
}

void ContextClient::ping_all_queues()
{
    // notify driver_ctx_fifo
    driver_ctx_fifo->notify();

    // ping all queues to check if they are alive
    for(auto it = ofmap_queues->begin(); it != ofmap_queues->end(); ++it) {
        it->second->notify();
    }

    // same for freelists
    for(auto it = ofmap_freelists->begin(); it != ofmap_freelists->end(); ++it) {
        it->second->notify();
    }
}


// ModelContext
//===================================================================

ModelContext::ModelContext(uint32_t ibuffer_size_, uint32_t obuffer_size_, uint32_t num_ifmaps_, uint32_t num_ofmaps_,
                           uint64_t* ifmap_sizes_, uint64_t* ofmap_sizes_, std::vector<uint8_t> allowed_driver_ctxs_)
{


    num_ifmaps = num_ifmaps_;
    num_ofmaps = num_ofmaps_;

    allowed_driver_ctxs = allowed_driver_ctxs_;

    ifmap_sizes = new uint64_t[num_ifmaps];
    ofmap_sizes = new uint64_t[num_ofmaps];

    std::memcpy(ifmap_sizes, ifmap_sizes_, num_ifmaps * sizeof(uint64_t));
    std::memcpy(ofmap_sizes, ofmap_sizes_, num_ofmaps * sizeof(uint64_t));

    spdlog::debug("[ModelContext] ModelContext created with {} ifmaps and {} ofmaps", num_ifmaps, num_ofmaps);

    //// print ifmap_sizes and ofmap_sizes
    //std::cout << "ModelContext: ifmap_sizes: ";
    //for(uint32_t i = 0; i < num_ifmaps; i++) {
    //    std::cout << ifmap_sizes[i] << " ";
    //}
    //std::cout << std::endl;
    //std::cout << "ModelContext: ofmap_sizes: ";
    //for(uint32_t i = 0; i < num_ofmaps; i++) {
    //    std::cout << ofmap_sizes[i] << " ";
    //}
    //std::cout << std::endl;


    ibuffer_size = ibuffer_size_;
    obuffer_size = obuffer_size_;

    ifmap_buffers  = new IomapItem*[ibuffer_size];
    ifmap_freelist = new BlockyQueue<IomapItem*>(ibuffer_size);
    ifmap_queue    = new CtxBlockyQueue(this);

    for(uint32_t i = 0; i < ibuffer_size; i++) {
        ifmap_buffers[i] = new IomapItem(num_ifmaps, ifmap_sizes);
        ifmap_freelist->push(ifmap_buffers[i]);
    }

}


std::vector<int> ModelContext::get_client_ids()
{
    std::lock_guard<std::mutex> lock(client_table_lock);
    std::vector<int> client_ids;
    for(auto it = clients.begin(); it != clients.end(); ++it) {
        client_ids.push_back(it->first);
    }
    return client_ids;
}

ModelContext::~ModelContext()
{
    // remove all clients, if any are in the client table
    for(int id : this->get_client_ids()) {
        remove_client(id);
    }

    if(ifmap_buffers != nullptr) {
        for(uint32_t i = 0; i < ibuffer_size; i++) {
            if(ifmap_buffers[i] != nullptr) {
                delete ifmap_buffers[i];
                ifmap_buffers[i] = nullptr;
            }
        }
        delete [] ifmap_buffers;
        ifmap_buffers = nullptr;
    }

    delete ifmap_freelist;
    ifmap_freelist = nullptr;

    delete ifmap_queue;
    ifmap_queue = nullptr;

    delete [] ifmap_sizes;
    delete [] ofmap_sizes;

}


ContextClient* ModelContext::add_client(uint32_t id, SharedLockedVar<bool>* alive_flag)
{
    if(id == 0 || id == 0xDEADBEEF || alive_flag == nullptr) {
        return nullptr;
    }
    else {
        std::lock_guard<std::mutex> lock(client_table_lock);
        clients[id] = new ContextClient(id, obuffer_size, num_ofmaps, ofmap_sizes, allowed_driver_ctxs, alive_flag);
        spdlog::debug("[ModelContext] Client {} added to ModelContext", id);
        clients_set.insert(clients[id]);
        return clients[id];
    }
}

bool ModelContext::is_client_existed(ContextClient* client)
{
    std::lock_guard<std::mutex> lock(client_table_lock);
    return clients_set.count(client) > 0;
}

ContextClient* ModelContext::get_meta(uint32_t id)
{
    if(id == 0 || id == 0xDEADBEEF) {
        return nullptr;
    }
    else {
        std::lock_guard<std::mutex> lock(client_table_lock);
        if(clients.count(id) == 0) {
            return nullptr;
        }
        else {
            return clients[id];
        }
    }
}


bool ModelContext::remove_client(uint32_t id)
{
    if(id == 0 || id == 0xDEADBEEF) {
        return false;
    }
    else {
        std::lock_guard<std::mutex> lock(client_table_lock);
        spdlog::debug("[ModelContext] Attempting to remove client {}", id);
        if(clients.count(id) == 0) {
            return false;
        }
        else {
            clients_set.erase(clients[id]);
            delete clients[id];
            return (bool) clients.erase(id);
        }
    }
}

bool ModelContext::is_client_list_empty()
{
    std::lock_guard<std::mutex> lock(client_table_lock);
    return clients_set.empty();
}

void ModelContext::print_clients()
{
    //std::lock_guard<std::mutex> lock(client_table_lock);
    //int client_count = clients.size();
    //for(int i = 0; i < client_count; i++) {
    //    std::cout << "    Client ID: " << clients[i]->id << std::endl;
    //}
}



// DFPContext
//===================================================================

DFPContext::DFPContext(uint64_t dfp_raw_size_, uint8_t* dfp_bytes, hash_t h, uint32_t ibuffer_size_, uint32_t obuffer_size_,
                       std::vector<uint8_t> devices_to_use_)
{
    successful_init = false;
    info = nullptr;

    dfp_raw_size = dfp_raw_size_;
    raw_dfp_bytes = dfp_bytes;
    dfp_obj = new Dfp::DfpObject(raw_dfp_bytes, dfp_raw_size);

    client_ref_count = 0;
    exec_ref_count = 0;

    // for each device to use, create a driver ctx
    allowed_driver_ctxs.resize(devices_to_use_.size());
    bool all_good = true;
    for(size_t i = 0; i < devices_to_use_.size(); i++) {
        uint8_t ctx = driver_ctx_tracker.get_new();
        if(ctx == 0xFF) {
            spdlog::warn("[DFPContext] Failed to get a new driver context for device {}", devices_to_use_[i]);
            all_good = false;
            break;
        }
        device2context_table[devices_to_use_[i]] = ctx;
        allowed_driver_ctxs[i] = ctx;
    }

    if(!all_good) {
        spdlog::warn("[DFPContext] Failed to create DFPContext due to driver context allocation failure");
    }
    else {

        // populate info
        parse_dfp(dfp_obj);

        // store original hash
        hash = h;

        spdlog::debug("[DFPContext] DFPContext created with hash: {}", MX::sha512::to_base64(hash));

        // print info
        //driver_ctx_tracker.print();

        // create num_models many ModelContexts
        mcontexts = new ModelContext*[info->num_models];
        for(int i = 0; i < info->num_models ; i++) {
            mcontexts[i] = new ModelContext(ibuffer_size_, obuffer_size_,
                                            info->port_info[i]->num_in,
                                            info->port_info[i]->num_out,
                                            info->port_info[i]->iport_sizes,
                                            info->port_info[i]->oport_sizes,
                                            allowed_driver_ctxs);
        }

        successful_init = true;
    }
}

DFPContext::~DFPContext()
{

    for(uint8_t d : allowed_driver_ctxs) {
        driver_ctx_tracker.retire(d);
    }
    //driver_ctx_tracker.print();

    if(info != nullptr) {
        for(int i = 0; i < info->num_models ; i++) {
            if(mcontexts != nullptr) {
                if(mcontexts[i] != nullptr) {
                    delete mcontexts[i];
                    mcontexts[i] = nullptr;
                }
            }
        }
        if(mcontexts != nullptr) {
            delete [] mcontexts;
        }
    }
    mcontexts = nullptr;

    if(info != nullptr) {
        delete info;
        info = nullptr;
    }

    if(dfp_obj != nullptr) {
        delete dfp_obj;
        dfp_obj = nullptr;
    }

    if(raw_dfp_bytes != nullptr) {
        delete [] raw_dfp_bytes;
        raw_dfp_bytes = nullptr;
    }
}

ModelContext* DFPContext::get_mctx(int idx)
{
    if(idx < 0 || idx >= info->num_models) {
        return nullptr;
    }
    else {
        return mcontexts[idx];
    }
}


void DFPContext::parse_dfp(Dfp::DfpObject* d)
{

    // get DfpMeta struct
    Dfp::DfpMeta* m = d->get_dfp_meta();

    // print all data from the DfpMeta struct
    spdlog::debug("DfpMeta:");
    spdlog::debug("  num_inports: {}", m->num_inports);
    spdlog::debug("  num_outports: {}", m->num_outports);
    spdlog::debug("  num_used_inports: {}", m->num_used_inports);
    spdlog::debug("  num_used_outports: {}", m->num_used_outports);
    spdlog::debug("  num_models: {}", m->num_models);
    spdlog::debug("  num_chips: {}", m->num_chips);

    spdlog::debug("  model_inports:");
    for (int i = 0; i < m->num_models; ++i) {
        const auto &vec = m->model_inports[i];
        // first print the .size() of the model_inports[i]
        spdlog::debug("    .size: {}", vec.size());

        std::ostringstream oss;
        for (size_t j = 0; j < vec.size(); ++j) {
            oss << vec[j];
            if (j + 1 < vec.size()) { oss << ' '; }
        }
        spdlog::debug("    model {}: {}", i, oss.str());
    }

    spdlog::debug("  model_outports:");
    for (int i = 0; i < m->num_models; ++i) {
        const auto &vec = m->model_outports[i];

        std::ostringstream oss;
        for (size_t j = 0; j < vec.size(); ++j) {
            oss << vec[j];
            if (j + 1 < vec.size()) { oss << ' '; }
        }
        spdlog::debug("    model {}: {}", i, oss.str());
    }

    // create
    info = new dfp_info_t(m->num_models);
    size_t biggest_ofmap_so_far = 0;

    // ptr to actual data
    info->dfp = d;
    info->num_models = m->num_models;
    info->num_chips = m->num_chips;

    for(int i = 0; i < info->num_models ; i++) {

        port_infos_t* n = new port_infos_t();
        n->num_in = (uint8_t) m->model_inports[i].size();
        n->num_out = (uint8_t) m->model_outports[i].size();
        n->istart_idx = (uint8_t) m->model_inports[i].front();
        n->istop_idx = (uint8_t) m->model_inports[i].back();
        n->ostart_idx = (uint8_t) m->model_outports[i].front();
        n->ostop_idx = (uint8_t) m->model_outports[i].back();

        assert(n->num_in == ((n->istop_idx - n->istart_idx) + 1));
        assert(n->num_out == ((n->ostop_idx - n->ostart_idx) + 1));

        n->iport_sizes = new uint64_t[n->num_in];
        n->oport_sizes = new uint64_t[n->num_out];

        // input port data sizes in bytes
        for(uint8_t j = 0 ; j < n->num_in ; j++) {
            Dfp::PortInfo* p = d->input_port(n->istart_idx + j);

            if(p->format == MX_FMT_FP32) {
                // just size * 4
                n->iport_sizes[j] = (uint64_t) p->dim_h * p->dim_w * p->dim_z * p->dim_c * 4;
            }
            else if(p->format == MX_FMT_BF16) {
                // *2, sorta
                n->iport_sizes[j] = (uint64_t) p->dim_h * p->dim_w * p->dim_z * p->dim_c * 2;
                if(n->iport_sizes[j] % 4) {
                    n->iport_sizes[j] += (4 - (n->iport_sizes[j] % 4));    // round up to nearest 4 bytes
                }
            }
            else if(p->format == MX_FMT_GBF80) {
                // more complicated
                uint64_t num_xyz = (uint64_t) p->dim_h * p->dim_w * p->dim_z;
                bool any_remainder = ((p->dim_c % 8) != 0);
                uint64_t num_gbf_words = (p->dim_c / 8) + (any_remainder ? 1 : 0);
                n->iport_sizes[j] = num_xyz * num_gbf_words * 10;
                if(n->iport_sizes[j] % 4) {
                    n->iport_sizes[j] += (4 - (n->iport_sizes[j] % 4));    // round up to nearest 4 bytes
                }
            }
            else if(p->format == MX_FMT_GBF80_ROW) {
                // hooooohboy
                bool any_remainder = ((p->dim_c % 8) != 0);
                uint64_t num_gbf_words = (p->dim_c / 8) + (any_remainder ? 1 : 0);

                n->iport_sizes[j] = (uint64_t) p->dim_h * ((p->dim_w * p->dim_z * num_gbf_words * 10 + 3) & ~0x3);
                if(n->iport_sizes[j] % 4) {
                    n->iport_sizes[j] += (4 - (n->iport_sizes[j] % 4));    // round up to nearest 4 bytes
                }
            }
            // TODO: else error


            // print all the shape info for this port
            spdlog::debug("[DFPContext] Input port {}: format: {}, dim_h: {}, dim_w: {}, dim_z: {}, dim_c: {}, size: {}",
                          n->istart_idx + j, p->format, p->dim_h, p->dim_w, p->dim_z, p->dim_c, n->iport_sizes[j]);

        }


        // output port data sizes in bytes
        for(uint8_t j = 0 ; j < n->num_out ; j++) {
            Dfp::PortInfo* p = d->output_port(n->ostart_idx + j);

            uint32_t real_ch = 0;
            if(p->hpoc_en != 0) {
                real_ch = p->hpoc_dim_c;
            }
            else {
                real_ch = p->dim_c;
            }

            if(p->format == MX_FMT_FP32) {
                // just size * 4
                n->oport_sizes[j] = (uint64_t) p->dim_h * p->dim_w * p->dim_z * real_ch * 4;
            }
            else if(p->format == MX_FMT_BF16) {
                // *2, sorta
                n->oport_sizes[j] = (uint64_t) p->dim_h * p->dim_w * p->dim_z * real_ch * 2;
                if(n->oport_sizes[j] % 4) {
                    n->oport_sizes[j] += (4 - (n->oport_sizes[j] % 4));    // round up to nearest 4 bytes
                }
            }
            else if(p->format == MX_FMT_GBF80) {
                // more complicated
                uint64_t num_xyz = p->dim_h * p->dim_w * p->dim_z;
                bool any_remainder = ((real_ch % 8) != 0);
                uint64_t num_gbf_words = (real_ch / 8) + (any_remainder ? 1 : 0);
                n->oport_sizes[j] = num_xyz * num_gbf_words * 10;
                if(n->oport_sizes[j] % 4) {
                    n->oport_sizes[j] += (4 - (n->oport_sizes[j] % 4));    // round up to nearest 4 bytes
                }
            }
            else if(p->format == MX_FMT_GBF80_ROW) {
                // hooooohboy
                bool any_remainder = ((real_ch % 8) != 0);
                uint64_t num_gbf_words = (real_ch / 8) + (any_remainder ? 1 : 0);

                n->oport_sizes[j] = (uint64_t) p->dim_h * ((p->dim_w * p->dim_z * num_gbf_words * 10 + 3) & ~0x3);
                if(n->oport_sizes[j] % 4) {
                    n->oport_sizes[j] += (4 - (n->oport_sizes[j] % 4));    // round up to nearest 4 bytes
                }
            }
            // TODO: else error

            if(n->oport_sizes[j] > biggest_ofmap_so_far) {
                biggest_ofmap_so_far = n->oport_sizes[j];
            }

            // print all the shape info for this port
            spdlog::debug("[DFPContext] Output port {}: format: {}, dim_h: {}, dim_w: {}, dim_z: {}, dim_c: {}, size: {}",
                          n->ostart_idx + j, p->format, p->dim_h, p->dim_w, p->dim_z, real_ch, n->oport_sizes[j]);
        }

        info->biggest_ofmap_bytes = biggest_ofmap_so_far;
        info->port_info.push_back(n);
    }
}

void DFPContext::print_info()
{
    spdlog::debug("DFPContext {}", MX::sha512::to_base64(hash));
    spdlog::debug("  DFP size: {} bytes", dfp_raw_size);
    spdlog::debug("  Number of models: {}", info->num_models);

    // print all the data in the port_info
    for (int i = 0; i < info->num_models; ++i) {
        auto &p = info->port_info[i];
        spdlog::debug("    [Model {}] num_in={}, num_out={}, istart_idx={}, istop_idx={}, ostart_idx={}, ostop_idx={}",
                      i, p->num_in, p->num_out,
                      p->istart_idx, p->istop_idx,
                      p->ostart_idx, p->ostop_idx);
        {
            std::ostringstream oss;
            for (uint8_t j = 0; j < p->num_in; ++j) {
                oss << p->iport_sizes[j];
                if (j + 1 < p->num_in) { oss << ' '; }
            }
            spdlog::debug("    iport_sizes: {}", oss.str());
        }
        {
            std::ostringstream oss;
            for (uint8_t j = 0; j < p->num_out; ++j) {
                oss << p->oport_sizes[j];
                if (j + 1 < p->num_out) { oss << ' '; }
            }
            spdlog::debug("    oport_sizes: {}", oss.str());
        }
    }
}

void DFPContext::print_clients()
{
    //spdlog::debug("DFPContext {}", MX::sha512::to_base64(hash));
    //spdlog::debug("  Number of total DFP clients: {}", client_ref_count.load());

    //for (int i = 0; i < info->num_models; ++i) {
    //    spdlog::debug("  Model {}:", i);
    //    mcontexts[i]->print_clients();
    //}
}
