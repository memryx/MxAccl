// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef ERRORS_H
#define ERRORS_H

#pragma once

namespace MX
{
namespace Utils
{
enum MEMX_API_EXPORT MX_status {
    MX_STATUS_OK = 0,
    MX_STATUS_TIMEOUT = 1,
    MX_STATUS_INVALID_DFP,
    MX_STATUS_INVALID_DATA_FMT,
    MX_STATUS_INVALID_CHIP_GEN,
    MX_STATUS_ERR_OPEN_DEV,
    MX_STATUS_ERR_GET_CHIPNUM,
    MX_STATUS_ERR_DFP_MISMATCH_WITH_HARDWARE,
    MX_STATUS_ERR_DOWNLOAD_MODEL,
    MX_STATUS_ERR_ENABLE_STREAM,
    MX_STATUS_ERR_STREAM_IFMAP,
    MX_STATUS_ERR_STREAM_OFMAP,
    MX_STATUS_END,

    MX_STATUS_ERR_INTERNAL = 1000, // error code >= 1000
};
} // namespace MX
} // namespace Utils

#endif
