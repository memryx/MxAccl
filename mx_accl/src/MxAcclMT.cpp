// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <sstream>

#include "spdlog/spdlog.h"

#include <memx/accl/MxAcclMT.h>

using namespace MX::Runtime;
using namespace MX::Types;
using namespace MX::Utils;

static constexpr const char* CLASS_NAME = "MxAcclMT";

MxAcclMT::~MxAcclMT()
{
    // base class's destructor will take care of the rest
}

bool MxAcclMT::send_input(std::vector<float*> in_data, int model_id, int stream_id, int32_t timeout)
{
    MxModel* model = get_model_or_throw(model_id, CLASS_NAME, __func__);

    // call model_manual_send
    return model->model_manual_send(in_data, stream_id, timeout);
}

bool MxAcclMT::receive_output(std::vector<float*> &out_data, int model_id, int stream_id, int32_t timeout)
{
    MxModel* model = get_model_or_throw(model_id, CLASS_NAME, __func__);

    // call model_manual_receive
    return model->model_manual_receive(out_data, stream_id, timeout);
}

bool MxAcclMT::run(std::vector<float*> in_data, std::vector<float*> &out_data, int model_id, int stream_id, int32_t timeout)
{
    MxModel* model = get_model_or_throw(model_id, CLASS_NAME, __func__);

    // call model_manual_run
    return model->manual_run(in_data, out_data, stream_id, timeout);
}