// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <iostream>
#include <vector>
#include <thread>

#include <cstring>

#ifdef __linux__
    #include <sched.h>
    #include <unistd.h>
    #include "cpuinfo.h"
    #include "spdlog/spdlog.h"
#endif

namespace MX
{
namespace Utils
{

// these are Linux only functions
#ifdef __linux__

bool is_big_core(const cpuinfo_core* core)
{
    // If no frequency info is available, check for common ARM little cores
    if (core->frequency == 0) {
        if(core->uarch == cpuinfo_uarch_cortex_a53 || core->uarch == cpuinfo_uarch_cortex_a55) {
            return false;
        }
        else {
            // Assume any other core is a big core if frequency is not available
            return true;
        }
    }

    // Compute a threshold: max frequency among all cores
    static uint32_t max_freq = 0;
    if (max_freq == 0) {
        uint32_t core_count = cpuinfo_get_cores_count();
        for (uint32_t i = 0; i < core_count; i++) {
            const cpuinfo_core* c = cpuinfo_get_core(i);
            if (c->frequency > max_freq) {
                max_freq = c->frequency;
            }
        }
    }

    // Consider a core "big" if it's >= 95% of the max frequency
    return (core->frequency >= 0.95 * max_freq);
}

std::vector<uint32_t> get_big_core_processors()
{
    std::vector<uint32_t> big_processors;
    uint32_t processor_count = cpuinfo_get_processors_count();

    for (uint32_t i = 0; i < processor_count; i++) {
        const cpuinfo_processor* proc = cpuinfo_get_processor(i);
        const cpuinfo_core* core = proc->core;

        if (is_big_core(core)) {
            big_processors.push_back(proc->linux_id);
        }
    }
    return big_processors;
}

void set_affinity_to_big_cores(const std::vector<uint32_t> &big_cores, uint32_t min_num_cores)
{
    if (big_cores.empty()) {
        spdlog::debug("No big cores found, skipping setting affinity.");
        return;
    }

    // skip if the number of big cores == the processor count
    uint32_t processor_count = cpuinfo_get_processors_count();
    if (big_cores.size() == processor_count) {
        spdlog::debug("All cores are the same, skipping setting affinity.");
        return;
    }

    // skip if the number of big cores is less than the minimum required
    if (big_cores.size() < min_num_cores) {
        spdlog::warn("Not enough big cores available ({}), skipping setting affinity.", big_cores.size());
        return;
    }

    cpu_set_t set;
    CPU_ZERO(&set);

    for (uint32_t cpu_id : big_cores) {
        CPU_SET(cpu_id, &set);
    }

    pid_t pid = getpid();
    if (sched_setaffinity(pid, sizeof(set), &set) != 0) {
        spdlog::warn("Failed to set process affinity to big core: {}", strerror(errno));
    }
    else {
        // print the list of big cores we've assigned to
        for (uint32_t cpu_id : big_cores) {
            spdlog::debug("CPU assignment includes big core {}", cpu_id);
        }
    }
}


void set_self_affinity_to_big_cores(uint32_t min_num_cores)
{
    if(cpuinfo_initialize()) {
        std::vector<uint32_t> big_cores = get_big_core_processors();
        set_affinity_to_big_cores(big_cores, min_num_cores);
    }
}

#else

void set_self_affinity_to_big_cores(uint32_t min_num_cores)
{
    // do nothing on non-Linux systems
}

#endif // __linux__

} // namespace Utils
} // namespace MX
