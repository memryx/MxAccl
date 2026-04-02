// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#pragma once
#include <filesystem>
#include <string>

// for MEMX_API_EXPORT macro
#include <memx/memx.h>

namespace MX
{
namespace Utils
{
/**
 * Returns absolute path of home directory if env variable `MX_API_HOME`
 * is set else returns empty path
 */
MEMX_API_EXPORT std::filesystem::path mx_get_home_dir();

/**
 * Returns absolute path of accl directory
 */
MEMX_API_EXPORT std::filesystem::path mx_get_accl_dir();

} // namespace Utils
} // namespace MX

#endif
