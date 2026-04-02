// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <stdint.h>
#include <thread>
#include <map>
#include <unordered_map>
#include <mutex>
#include <utility>

#include "spdlog/spdlog.h"

#include <memx/accl/DFPRunner.h>
#include <memx/accl/utils/auto_clocker.h>

using namespace std;
using namespace MX::Utils;
using namespace MX::Runtime;
using namespace MX::RPC;
using namespace MX::Types;

DFPRunner::DFPRunner(int dfp_id, Dfp::DfpObject* dfp, const std::string &server_address, unsigned short base_port,
                     bool local_mode, const std::vector<int> &device_ids_to_use,
                     std::array<bool, 2> use_model_shape,
                     const SchedulerOptions &sched_options, const ClientOptions &client_options,
                     DeviceManager* dev_man, bool ignore_server)
{

    // set variables
    dfp_id_ = dfp_id;
    dfp_ = dfp;
    use_model_shape_ = use_model_shape;
    local_mode_ = local_mode;
    device_ids_to_use_ = device_ids_to_use;
    sched_options_ = sched_options;
    client_options_ = client_options;
    ignore_server_ = ignore_server;
    server_address_ = server_address;
    base_port_ = base_port;
    device_manager_ = dev_man;

    clients.clear();
    autoclock_infos.clear();
}

DFPRunner::DFPRunner(int dfp_id, Dfp::DfpObject* dfp, const std::string &server_address, unsigned short base_port,
                     bool local_mode, const std::vector<int> &device_ids_to_use,
                     std::array<bool, 2> use_model_shape,
                     const SchedulerOptions &sched_options, const ClientOptions &client_options, DeviceManager* dev_man)
    : DFPRunner(dfp_id, dfp, server_address, base_port, local_mode, device_ids_to_use, use_model_shape, sched_options, client_options, dev_man,
                false) {}


bool DFPRunner::is_local()
{
    return local_mode_;
}

int DFPRunner::get_num_chips()
{
    // return the number of chips the DfpObject is compiled for from the DfpMeta
    // this is the number of chips in the DFP, not the number of chips on the device
    return dfp_->get_dfp_meta()->num_chips;
}

std::vector<int> DFPRunner::get_converted_device_ids_to_use() const
{
    if (converted_dev_ids_.empty()) {
        spdlog::error("[DFPRunner] Error in get_converted_device_ids_to_use(): converted_dev_ids_ is empty. This should have been set during init_loca() or init_shared().");
    }
    return converted_dev_ids_;
}

DFPRunner::~DFPRunner()
{
    // close all contexts
    if(local_mode_) {
        close_local();
    }
    else {
        close_shared();
    }

    // delete the clients
    for(auto client : clients) {
        if(client != nullptr) {
            delete client;
        }
    }

    delete dfp_;
}

void DFPRunner::devman_discover(Client* client_)
{
    // discover devices using the device manager
    if(device_manager_ != nullptr) {
        if(device_manager_->all_devices_count == -1) { // not discovered yet
            if(ignore_server_) {
                device_manager_->discover_devices_direct();
            }
            else {
                if(client_ == nullptr) {
                    spdlog::error("[DFPRunner] Client is null, cannot discover devices");
                }
                else {
                    device_manager_->discover_devices_remote(client_);
                }
            }
        }
    }
    else {
        spdlog::error("[DFPRunner] DeviceManager is null, cannot discover devices");
    }
}

//==============================================================================================
//==============================================================================================
// LOCAL MODE
//==============================================================================================
//==============================================================================================

bool DFPRunner::init_local()
{
    // Initialize local mode
    spdlog::debug("[DFPRunner] Initializing local mode");
    if(ignore_server_) {
        spdlog::warn("[DFPRunner] Ignoring MXA-Manager locking!");
        devman_discover(nullptr);
        clients.push_back(nullptr);
    }
    else {
        Client* client_ = new Client();

        if(client_->init_connection(server_address_, base_port_) == false) {
            spdlog::error("[DFPRunner] Error in client->init_conenction for local mode");
            delete client_;
            return false;
        }

        devman_discover(client_);

        // NOTE: Device ids conversion has to go after devman_discover
        converted_dev_ids_ = device_manager_->convert_device_ids(device_ids_to_use_);

        // for each device id in the vector, do try_local_lock and if any of them
        // fail, return false (after first unlocking any that were locked by us)
        // use a temporary vector to store the device ids that we locked
        vector<int> locked_device_ids;
        for(auto device_id : converted_dev_ids_) {
            if(client_->try_local_lock(device_id) == false) {
                spdlog::error("[DFPRunner] Error in client->try_local_lock for device id: {}", device_id);
                // unlock any that were locked by us
                for(auto id : locked_device_ids) {
                    client_->local_unlock(id);
                }
                delete client_;
                return false;
            }
            else {
                locked_device_ids.push_back(device_id);
            }
        }

        // set the client to the vector of clients
        clients.push_back(client_);
    }

    // set the number of devices to the size of the vector
    num_devices_ = converted_dev_ids_.size();

    // search device_manager_->local_device_in_use and if there's
    // any from converted_dev_ids_ that are already in use, close the client
    // and return false
    for(auto device_id : converted_dev_ids_) {
        if(device_id < 0 || device_id >= device_manager_->all_devices_count || device_id >= (int)device_manager_->local_device_in_use.size()) {
            spdlog::error("[DFPRunner] Invalid device id: {}. all_devices_count: {}, local_device_in_use size: {}",
                          device_id, device_manager_->all_devices_count, device_manager_->local_device_in_use.size());
            // unlock any that were locked by us
            if(clients[0] != nullptr) {
                for(auto id : converted_dev_ids_) {
                    clients[0]->local_unlock(id);
                }
                delete clients[0];
                clients.clear();
            }
            return false;
        }
        if(device_manager_->local_device_in_use[device_id]) {
            spdlog::error("[DFPRunner] Device id {} is already in use", device_id);
            // unlock any that were locked by us
            if(clients[0] != nullptr) {
                for(auto id : converted_dev_ids_) {
                    clients[0]->local_unlock(id);
                }
                delete clients[0];
                clients.clear();
            }
            return false;
        }
    }


    // if SchedulerOptions has autoclock_enabled, first do AutoClocker for each device
    // and record the frequency in autoclock_infos
    if(sched_options_.autoclock_enabled) {
        spdlog::debug("[DFPRunner] Auto-autoclock is enabled, starting AutoClocker for each device");
        for(size_t i = 0; i < converted_dev_ids_.size(); i++) {
            int device_id = converted_dev_ids_[i];

            // if device can_get_power_data is false, skip auto-autoclock
            if(device_manager_->device_infos[device_id].can_get_power_data == false) {
                spdlog::warn("[DFPRunner] Device id {} cannot get power data, skipping AutoClocker for it", device_id);
                autoclock_infos[device_id].autoclock_enabled = false;
                autoclock_infos[device_id].power_limit_mw = 100000;
                autoclock_infos[device_id].freq = FREQ_USE_CONF;
                continue;
            }

            spdlog::info("Running AutoClocker for device id {}, please wait.", device_id);
            AutoClocker au;
            MxFrequencyOption freq = au.run(device_id, dfp_, sched_options_.autoclock_power_limit_mw,
                                                 device_id, // driver_ctx_to_use
                                                 sched_options_.autoclock_sample_interval_ms,
                                                 sched_options_.autoclock_num_samples,
                                                 sched_options_.autoclock_check_fps_saturation);

            if(freq == MX_FREQUENCY_OPTION_INVALID) {
                spdlog::error("[DFPRunner] AutoClocker failed for device id: {}", device_id);
                // default to FREQ_USE_CONF
                autoclock_infos[device_id].autoclock_enabled = false;
                autoclock_infos[device_id].power_limit_mw = 100000;
                autoclock_infos[device_id].freq = FREQ_USE_CONF;
            }
            else {
                spdlog::debug("[DFPRunner] AutoClocker succeeded for device id: {}, chosen frequency: {}",
                              device_id, mxFrequencyOptionToString(freq));
                autoclock_infos[device_id].autoclock_enabled = true;
                autoclock_infos[device_id].power_limit_mw = sched_options_.autoclock_power_limit_mw;
                autoclock_infos[device_id].freq = freq;
            }
        }
    }
    else {
        // set all autoclock_infos to autoclock_enabled = false
        for(int device_id : converted_dev_ids_) {
            autoclock_infos[device_id].autoclock_enabled = false;
            autoclock_infos[device_id].power_limit_mw = 100000;
            autoclock_infos[device_id].freq = FREQ_USE_CONF;
        }
    }


    // open the memx_open contexts for each device id
    for(auto device_id : converted_dev_ids_) {
        uint8_t driver_context_id = (uint8_t) device_id;
        // open the context for the device id
        memx_status status;

        if(ignore_server_) {
            status = memx_lock(device_id);
        }

        // not really thread-safe, but in local mode there's only one thread
        // calling init_local() at a time
        device_manager_->local_device_in_use[device_id] = true;

        if(device_manager_->device_infos[device_id].is_usb == false){
            // use OPCDOE_SET_DEVICE_DMA_TRIGGER_TYPE to set all chips' DMA trigger type to TRIGGER_TYPE_HOST 
            for(int chip_idx = 0; chip_idx < device_manager_->device_infos[device_id].chip_count; chip_idx++) {
                status = memx_set_feature(device_id, chip_idx, OPCDOE_SET_DEVICE_DMA_TRIGGER_TYPE,
                                          MEMX_CHIP_INPUT_DMA_TRIGGER_TYPE_HOST);
                if(memx_status_error(status)) {
                    spdlog::error("[DFPRunner] Error in set trigger for device id: {} chip idx: {}", device_id, chip_idx);
                }
            }
        }

        status = memx_open(driver_context_id, device_id, MEMX_DEVICE_CASCADE_PLUS);
        if(memx_status_error(status)) {
            spdlog::error("[DFPRunner] Error in memx_open for device id: {}", device_id);

            // if client is not null, unlock any that were locked by us
            if(clients[0] != nullptr) {
                for(auto id : converted_dev_ids_) {
                    clients[0]->local_unlock(id);
                    device_manager_->local_device_in_use[id] = false;
                }
                delete clients[0];
                clients.clear();
            }
            if(ignore_server_) {
                memx_unlock(device_id);
            }
            return false;
        }
        else {
            spdlog::debug("[DFPRunner] Successfully opened context for device id: {}", device_id);
            // add the context id to the vector of context ids
            open_contexts_.push_back(driver_context_id);
        }

        if(dfp_->get_dfp_meta()->num_chips < device_manager_->device_infos[device_id].chip_count) {
            if (dfp_->get_dfp_meta()->num_chips == 1) {
                device_manager_->device_infos[device_id].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_ONE_MPU;
            } else if (dfp_->get_dfp_meta()->num_chips == 2) {
                device_manager_->device_infos[device_id].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_TWO_MPUS;
            } else if (dfp_->get_dfp_meta()->num_chips == 3) {
                device_manager_->device_infos[device_id].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_THREE_MPUS;
            } else if (dfp_->get_dfp_meta()->num_chips == 4) {
                device_manager_->device_infos[device_id].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_FOUR_MPUS;
            } else if (dfp_->get_dfp_meta()->num_chips == 8) {
                device_manager_->device_infos[device_id].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_EIGHT_MPUS;
            } else if (dfp_->get_dfp_meta()->num_chips == 12) {
                device_manager_->device_infos[device_id].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_TWELVE_MPUS;
            } else if (dfp_->get_dfp_meta()->num_chips == 16) {
                device_manager_->device_infos[device_id].current_config = MEMX_MPU_GROUP_CONFIG_ONE_GROUP_SIXTEEN_MPUS;
            } else {
                // error
                spdlog::error("[DFPRunner] Unsupported number of chips in DFP: {} for device id: {} which has chip_count: {}",
                              dfp_->get_dfp_meta()->num_chips, device_id, device_manager_->device_infos[device_id].chip_count);
                // unlock any that were locked by us
                if(clients[0] != nullptr) {
                    for(auto id : converted_dev_ids_) {
                        clients[0]->local_unlock(id);
                        device_manager_->local_device_in_use[id] = false;
                    }
                    delete clients[0];
                    clients.clear();
                }
                // memx_close any open contexts
                for(auto context_id : open_contexts_) {
                    memx_close(context_id);
                    if(ignore_server_) {
                        memx_unlock(context_id);
                    }
                }
                open_contexts_.clear();
                return false;
            }
        }

        memx_config_mpu_group(device_id, device_manager_->device_infos[device_id].current_config);
    }

    // make sure the length of open_contexts is equal to the length of device_ids_to_use
    if(open_contexts_.size() != converted_dev_ids_.size()) {
        spdlog::error("[DFPRunner] Error in opening contexts for all device ids");
        // unlock any that were locked by us
        if(clients[0] != nullptr) {
            for(auto id : converted_dev_ids_) {
                clients[0]->local_unlock(id);
                device_manager_->local_device_in_use[id] = false;
            }
            delete clients[0];
            clients.clear();
        }
        // memx_close any open contexts
        for(auto context_id : open_contexts_) {
            memx_close(context_id);
            if(ignore_server_) {
                memx_unlock(context_id);
            }
        }
        open_contexts_.clear();
        return false;
    }

    // Use the DeviceManager to check chip counts and set_power_mode for each opened device
    for(auto context_id : open_contexts_) {
        int device_id = context_id; // because we assigned context_id to device_id in memx_open
        if(device_manager_ != nullptr) {
            // get num_chips from the device_infos for this device id
            int device_chip_count = device_manager_->device_infos[device_id].chip_count;

            // make sure dfp num_chips is less than or equal to device_chip_count, and is
            // a valid number (1,2,3,4,8,12,16)
            if(dfp_->get_dfp_meta()->num_chips > device_chip_count) {
                spdlog::error("[DFPRunner] DFP num_chips: {} is greater than device id: {} chip_count: {}",
                              dfp_->get_dfp_meta()->num_chips, device_id, device_chip_count);
                // unlock any that were locked by us
                if(clients[0] != nullptr) {
                    for(auto id : converted_dev_ids_) {
                        clients[0]->local_unlock(id);
                        device_manager_->local_device_in_use[id] = false;
                    }
                    delete clients[0];
                    clients.clear();
                }
                // memx_close any open contexts
                for(auto context_id : open_contexts_) {
                    memx_close(context_id);
                    if(ignore_server_) {
                        memx_unlock(context_id);
                    }
                }
                open_contexts_.clear();
                return false;
            }
            if(dfp_->get_dfp_meta()->num_chips != 1 &&
               dfp_->get_dfp_meta()->num_chips != 2 &&
               dfp_->get_dfp_meta()->num_chips != 3 &&
               dfp_->get_dfp_meta()->num_chips != 4 &&
               dfp_->get_dfp_meta()->num_chips != 8 &&
               dfp_->get_dfp_meta()->num_chips != 12 &&
               dfp_->get_dfp_meta()->num_chips != 16) {
                spdlog::error("[DFPRunner] DFP num_chips: {} is not a valid number for device id: {}",
                              dfp_->get_dfp_meta()->num_chips, device_id);
                // unlock any that were locked by us
                if(clients[0] != nullptr) {
                    for(auto id : converted_dev_ids_) {
                        clients[0]->local_unlock(id);
                        device_manager_->local_device_in_use[id] = false;
                    }
                    delete clients[0];
                    clients.clear();
                }
                // memx_close any open contexts
                for(auto context_id : open_contexts_) {
                    memx_close(context_id);
                    if(ignore_server_) {
                        memx_unlock(context_id);
                    }
                }
                open_contexts_.clear();
                return false;
            }

            // Set freq to the upclocked freq if autoclocking is enabled, else the
            // default will be FREQ_USE_CONF
            device_manager_->set_power_mode(device_id, device_chip_count, autoclock_infos[device_id].freq);
            spdlog::debug("[DFPRunner] Successfully set power mode for device id: {} to {}",
                          device_id, mxFrequencyOptionToString(autoclock_infos[device_id].freq));
        }
    }

    // Download the DFP to each open device
    for(auto context_id : open_contexts_) {
        memx_status status = memx_download_model(context_id, (const char*) dfp_->src_dfp_bytes, 0 /*model_idx*/,
                             MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL_BUFFER);
        if(memx_status_error(status)) {
            spdlog::error("[DFPRunner] Error in memx_download for context id: {}", context_id);
            // unlock any that were locked by us
            if(clients[0] != nullptr) {
                for(auto id : converted_dev_ids_) {
                    clients[0]->local_unlock(id);
                    device_manager_->local_device_in_use[id] = true;
                }
                delete clients[0];
                clients.clear();
            }
            // memx_close any open contexts
            for(auto context_id : open_contexts_) {
                memx_close(context_id);
                if(ignore_server_) {
                    memx_unlock(context_id);
                }
            }
            open_contexts_.clear();
            return false;
        }
        else {
            spdlog::debug("[DFPRunner] Successfully downloaded DFP to context id: {}", context_id);
        }
    }

    // Set stream enable for each open context
    for(auto context_id : open_contexts_) {
        memx_status status = memx_set_stream_enable(context_id, 0 /*wait time?*/);
        if(memx_status_error(status)) {
            spdlog::error("[DFPRunner] Error in memx_set_stream_enable for context id: {}", context_id);
            // unlock any that were locked by us
            if(clients[0] != nullptr) {
                for(auto id : converted_dev_ids_) {
                    clients[0]->local_unlock(id);
                    device_manager_->local_device_in_use[id] = true;
                }
                delete clients[0];
                clients.clear();
            }
            // memx_close any open contexts
            for(auto context_id : open_contexts_) {
                memx_close(context_id);
                if(ignore_server_) {
                    memx_unlock(context_id);
                }
            }
            open_contexts_.clear();
            return false;
        }
        else {
            spdlog::debug("[DFPRunner] Successfully set stream enable for context id: {}", context_id);
        }
    }

    // get the number of models from the dfp object
    num_models = dfp_->get_dfp_meta()->num_models;

    // create the models vector
    models.resize(num_models);
    for(int i = 0; i < num_models; i++) {
        spdlog::debug("[DFPRunner] Creating model {}", i);
        models[i] = new MxModel(i, dfp_, use_model_shape_, true, open_contexts_, clients[0]);
    }

    // Done initializing local mode
    spdlog::debug("[DFPRunner] Done initializing local mode");
    return true;
}


bool DFPRunner::close_local()
{

    // are we already closed (models.size == 0 and open_contexts_.size() == 0)?
    if(open_contexts_.empty()) {
        spdlog::debug("[DFPRunner] Already closed local mode");

        // make sure clients is also empty
        if(!clients.empty()) {
            spdlog::warn("[DFPRunner] Clients vector is not empty, but models and open_contexts are empty. This should not happen.");
            for(auto client : clients) {
                delete client;
            }
            clients.clear();
        }

        return true;
    }

    // Stop all models
    for(unsigned int i = 0; i < models.size(); i++) {
        if(models[i] != nullptr) {
            // delete model
            delete models[i];
            models[i] = nullptr;
        }
    }

    // Set stream disable for each open context
    for(auto context_id : open_contexts_) {
        // NOTE:
        // We have to set `wait` to one to ensure the function waits for `ifmap` and `ofmap` to complete.
        // Setting `wait` to zero returns immediately, which may cause a hang in the next round of download_dfp
        // if `ifmap` or `ofmap` are still processing from the previous operation.
        int wait = 1;
        memx_status status = memx_set_stream_disable(context_id, wait);

        if(memx_status_error(status)) {
            spdlog::error("[DFPRunner] Error in memx_set_stream_disable for context id: {}", context_id);
            return false;
        }
        else {
            spdlog::debug("[DFPRunner] Successfully set stream disable for context id: {}", context_id);
        }
    }

    // Close all contexts
    for(auto context_id : open_contexts_) {
        memx_status status = memx_close(context_id);
        if(memx_status_error(status)) {
            spdlog::error("[DFPRunner] Error in memx_close for context id: {}", context_id);
            return false;
        }
        else {
            spdlog::debug("[DFPRunner] Successfully closed context id: {}", context_id);
        }
    }

    // set local_device_in_use to false for each device id
    for(auto device_id : device_ids_to_use_) {
        device_manager_->local_device_in_use[device_id] = false;
    }

    if(!ignore_server_) {
        // unlock any that were locked by us

        // if clients is empty, we tried to connect but failed
        if(clients.empty()) {
            spdlog::warn("[DFPRunner] No valid Client ptr (with ignore_server = false)");
            open_contexts_.clear();
            return true;
        }

        if(clients[0] != nullptr) {
            for(auto id : device_ids_to_use_) {
                if(clients[0]->local_unlock(id) == false) {
                    spdlog::warn("[DFPRunner] Error in client->local_unlock for device id: {}", id);
                }
                else {
                    spdlog::debug("[DFPRunner] Successfully unlocked device id: {}", id);
                }
            }

            // call the client's end_connection
            if(clients[0]->end_connection() == false) {
                spdlog::error("[DFPRunner] Error in client->end_connection for local mode");
                delete clients[0];
                clients.clear();
                return false;
            }
            else {
                spdlog::debug("[DFPRunner] Successfully ended connection for local mode");
            }

            delete clients[0];
            clients.clear();
        }
        else {
            spdlog::error("[DFPRunner] No valid Client ptr with ignore_server = false!");
            return false;
        }
    }
    else {

        // local unlock contexts with memx_unlock
        for(auto context_id : open_contexts_) {
            memx_status status = memx_unlock(context_id);
            if(memx_status_error(status)) {
                spdlog::error("[DFPRunner] Error in memx_unlock for context id: {}", context_id);
                return false;
            }
            else {
                spdlog::debug("[DFPRunner] Successfully unlocked context id: {}", context_id);
            }
        }

    }

    // clear the open_contexts vector
    open_contexts_.clear();

    // Done closing local mode
    spdlog::debug("[DFPRunner] Done closing local mode");
    return true;
}



//==============================================================================================
//==============================================================================================
// SHARED MODE
//==============================================================================================
//==============================================================================================

bool DFPRunner::init_shared()
{
    // Initialize shared mode
    spdlog::debug("[DFPRunner] Initializing shared mode");

    // get the number of models from the dfp object
    num_models = dfp_->get_dfp_meta()->num_models;
    models.resize(num_models, nullptr);

    // for each model, create a new Client and connect_dfp with the dfp_ and the relevant model_id
    for(int i = 0; i < num_models; i++) {
        Client* client_ = new Client();
        if(client_->init_connection(server_address_, base_port_) == false) {
            spdlog::error("[DFPRunner] Error in client->init_connection for shared mode, server_address: {}, base_port: {}",
                          server_address_, base_port_);
            delete client_;
            return false;
        }

        devman_discover(client_);

        // NOTE: Device ids conversion has to go after devman_discover
        converted_dev_ids_ = device_manager_->convert_device_ids(device_ids_to_use_);

        // connect_dfp based on the SchedulerOptions and the device ids to use
        // (get length of device_ids_to_use and send that as connect_dfp's num_devices arg)
        int32_t num_devices = converted_dev_ids_.size();
        int32_t* devices_to_use = new int32_t[num_devices];
        for(int i = 0; i < num_devices; i++) {
            devices_to_use[i] = converted_dev_ids_[i];
        }

        // connect_dfp with the dfp_ and the relevant model_id
        if(client_->connect_dfp(dfp_->dfp_byte_size, dfp_->src_dfp_bytes, i, sched_options_, client_options_, num_devices, devices_to_use) == false) {
            spdlog::error("[DFPRunner] Error in client->connect_dfp for shared mode");
            delete client_;
            delete [] devices_to_use;
            return false;
        }

        // add the client to the vector of clients
        // Note: model_id will match the index of the client in the vector
        clients.push_back(client_);

        // create the model for this client
        // (open_contexts is just empty for shared mode)
        open_contexts_.clear();
        models[i] = new MxModel(i, dfp_, use_model_shape_, false, open_contexts_, client_);
        
        delete[] devices_to_use;
    }

    return true;
}


bool DFPRunner::close_shared()
{
    // Close shared mode
    spdlog::debug("[DFPRunner] Closing shared mode");

    // stop & delete all MxModels
    for(unsigned int i = 0; i < models.size(); i++) {
        if(models[i] != nullptr) {
            // delete model
            delete models[i];
            models[i] = nullptr;
        }
    }

    bool ret = true;

    // delete clients
    for(auto client : clients) {
        if(client != nullptr) {
            // call the client's end_connection
            if(client->end_connection() == false) {
                spdlog::error("[DFPRunner] Error in client->end_connection for shared mode");
                delete client;
                client = nullptr;
                ret = false;
            }
            else {
                spdlog::debug("[DFPRunner] Successfully ended connection for shared mode");
                delete client;
                client = nullptr;
            }
        }
    }

    clients.clear();
    // Done closing shared mode
    spdlog::debug("[DFPRunner] Done closing shared mode");
    return ret;
}

MxModel* DFPRunner::get_model(int model_id)
{
    if(model_id < 0 || model_id >= num_models) {
        spdlog::error("[DFPRunner] Error in get_model: model_id should be between 0 and {}", num_models - 1);
        return nullptr;
    }

    return models[model_id];
}
