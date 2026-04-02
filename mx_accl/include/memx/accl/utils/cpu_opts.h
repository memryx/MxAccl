// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef CPU_OPTS_H
#define CPU_OPTS_H

#pragma once

namespace MX
{
namespace Utils
{

/**
 * @brief Sets the CPU affinity of the current thread and all children to the big cores.
 *
 * @param min_num_cores Minimum number of big cores needed on system in order to restrict affinity.
 */
void set_self_affinity_to_big_cores(uint32_t min_num_cores);

}
}

#endif // CPU_OPTS_H
