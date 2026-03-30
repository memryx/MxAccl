// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_ID_TRACKER_H
#define MX_ID_TRACKER_H

#pragma once
#include <mutex>
#include <unordered_set>
#include <deque>
#include <cstdint>

#include <memx/memx.h>

namespace MX
{
namespace Utils
{

// generic used to generate and track unique IDs
class IDTracker
{
  protected:
    // common
    std::mutex                   m_mutex;

    // random IDs mode (dfp_id)
    //--------------------------------------
    std::unordered_set<uint32_t> ids;

    // config
    const bool disallow_0_deadbeef; // for Client ID tracking
    const bool start_rand_with_0;


    // sequential IDs mode (driver ctx)
    //--------------------------------------
    std::deque<uint32_t> free_id_list;

    // config
    const bool sequential_order;
    const uint32_t max_sequential_val;

  public:
    IDTracker(bool disallow_0_deadbeef_,
              bool start_rand_with_0_,
              bool sequential_order_,
              uint32_t max_sequential_val_);

    // returns an ID
    // NOTE: in sequential order mode, this will
    //       return 0xFFFFFFFF if we're out of
    //       available ID numbers
    uint32_t get_new();

    // is the ID present / free?
    bool     check(uint32_t id);

    // return this ID to the pool
    void     retire(uint32_t id);

    // print contents
    void     print();

    // Returns the number of IDs currently in use (created and not retired).
    size_t active_count();

    // Empties the ID tracker, retiring all active IDs. 
    // Mainly used for testing to reset state between tests.
    void clear();
};


// specialized for Client IDs
class ClientIDTracker : public IDTracker
{
  public:
    ClientIDTracker() : IDTracker(true, false, false, 0) {};
};

// specialized for DFP IDs
class DfpIDTracker : public IDTracker
{
  public:
    DfpIDTracker() : IDTracker(false, true, false, 0) {};
};

// specialized for Driver Context IDs
class DriverIDTracker : public IDTracker
{
  public:
    DriverIDTracker(uint32_t max = MEMX_MODEL_MAX_NUMBER) : IDTracker(false, false, true, max) {};
};

}
}

#endif
