// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <sstream>

#include "spdlog/spdlog.h"

#include <memx/accl/MxAccl.h>
#include <memx/accl/client.h>

using namespace MX::Runtime;
using namespace MX::Types;
using namespace MX::Utils;

static constexpr const char* CLASS_NAME = "MxAccl";

MxAccl::~MxAccl()
{
    // MxAcclBase destructor will delete all the rest
    this->stop_all();
}

//---------------------------------------------------------------------------------------------
// ALL THE START/STOP/WAIT FUNCTIONS
//---------------------------------------------------------------------------------------------

void MxAccl::start(int model_id)
{
    if(model_id == -1) {

        // check streams before starting all models
        int total_streams = 0;
        for (const auto &model: dfp_runner->models) {
            total_streams += model->get_num_streams();
        }
        if (total_streams == 0) {
            throw std::runtime_error("No stream connected across all models. Please connect at least one stream before start().");
        }

        this->start_all();
    }
    else {
        this->start_model(model_id);
    }
}

void MxAccl::stop(int model_id)
{
    if(model_id == -1) {
        this->stop_all();
    }
    else {
        this->stop_model(model_id);
    }
}

void MxAccl::wait(int model_id)
{
    if(model_id == -1) {
        this->wait_all();
    }
    else {
        this->wait_model(model_id);
    }
}

// Start/Stop/Wait helpers
//------------------------
void MxAccl::start_model(int model_id)
{
    get_model_or_throw(model_id, CLASS_NAME, __func__)->model_start();
}

void MxAccl::stop_model(int model_id)
{
    get_model_or_throw(model_id, CLASS_NAME, __func__)->model_stop();
}

void MxAccl::wait_model(int model_id)
{
    get_model_or_throw(model_id, CLASS_NAME, __func__)->model_wait();
}

void MxAccl::start_all()
{
    for (int i = 0; i < dfp_runner->num_models; i++) {
        this->start_model(i);
    }
}

void MxAccl::stop_all()
{
    for (int i = 0; i < dfp_runner->num_models; i++) {
        this->stop_model(i);
    }
}

void MxAccl::wait_all()
{
    for (int i = 0; i < dfp_runner->num_models; i++) {
        this->wait_model(i);
    }
}

//---------------------------------------------------------------------------------------------
// CONNECT_STREAM AND FRIENDS
//---------------------------------------------------------------------------------------------

void MxAccl::connect_stream(callback_t in_cb, callback_t out_cb, int stream_id, int model_id)
{
    get_model_or_throw(model_id, CLASS_NAME, __func__)->connect_stream(in_cb, out_cb, stream_id);
}

void MxAccl::set_num_workers(int input_num_workers, int output_num_workers, int model_id)
{
    get_model_or_throw(model_id, CLASS_NAME, __func__)->set_num_workers(input_num_workers, output_num_workers);
}

int MxAccl::get_num_streams(int model_id)
{
    return get_model_or_throw(model_id, CLASS_NAME, __func__)->get_num_streams();
}

bool MxAccl::all_tasks_done(int model_id) const
{
    // check specific model
    if (model_id != -1) {
        MxModel* model = get_model_or_throw(model_id, CLASS_NAME, __func__);
        return model->all_tasks_done();
    }

    // check all models
    for (int i = 0; i < dfp_runner->num_models; ++i) {
        MxModel* model = get_model_or_throw(i, CLASS_NAME, __func__);
        if (model->all_tasks_done() == false) {
            return false;
        }
    }
    return true;
}
