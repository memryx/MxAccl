// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#pragma once
#include <string>
#include <stdint.h>
#include <iostream>
#include <unordered_map>
#include <filesystem>

#include <memx/memx.h>

#include <memx/accl/dfp.h>
#include <memx/accl/client.h>
#include <memx/accl/messages.h>
#include <memx/accl/MxModel.h>
#include <memx/accl/utils/path.h>
#include <memx/accl/utils/mxTypes.h>
#include <memx/accl/utils/locked_var.h>

using namespace MX::RPC;
using namespace MX::Utils;

namespace MX
{
namespace Runtime
{


class DeviceManager
{
  public:
    MEMX_API_EXPORT DeviceManager();
    MEMX_API_EXPORT ~DeviceManager();

    MEMX_API_EXPORT void print_all_devices();
    MEMX_API_EXPORT void print_devices_info();

    float              get_power(int device_id);
    float              get_pressure(int device_id);
    float              get_max_temperature(int device_id);
    std::vector<float> get_chip_temperatures(int device_id);

    // configures groups for device
    bool configure_groups(int device_id, int device_chip_count, int pdfp_num_chips);

    bool set_power_mode(int device_id, int num_chips = 4,
                        MX::Types::MxFrequencyOption fop = MX::Types::MxFrequencyOption::FREQ_USE_CONF);

    std::vector<device_info_t> device_infos;

    int all_devices_count;

    bool discover_devices_direct();
    bool discover_devices_remote(Client* client);
    
    std::vector<bool> local_device_in_use;
    
    std::vector<int> convert_device_ids(const std::vector<int>& device_ids);

  private:

    void read_power_mode();

    std::mutex m_discover;

    // Frequency members
    uint16_t c4_freq;
    uint16_t c2_freq;
    uint16_t c4_volt;
    uint16_t c2_volt;

};// DeviceManager

} // namespace Runtime
} // namespace MX

#endif
