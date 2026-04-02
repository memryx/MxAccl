// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <iostream>
#include <cstdlib>

#include <memx/accl/utils/id_tracker.h>

namespace MX
{
namespace Utils
{

// IDTracker
//-------------------------------------------------------

IDTracker::IDTracker(bool disallow_0_deadbeef_, bool start_rand_with_0_,
                     bool sequential_order_, uint32_t max_sequential_val_)
    : disallow_0_deadbeef(disallow_0_deadbeef_),
      start_rand_with_0(start_rand_with_0_),
      sequential_order(sequential_order_),
      max_sequential_val(max_sequential_val_)
{
    if(sequential_order) {
        // populate the list
        for(uint32_t i = 0; i < max_sequential_val; i++) {
            free_id_list.push_back(i);
        }
    }
}

// generates new ID
uint32_t IDTracker::get_new()
{

    uint32_t ret = 0xFFFFFFFF;

    // sequential IDs mode (driver ctx)
    if(sequential_order) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(free_id_list.size() > 0) {
            // just get the next ID
            ret = free_id_list.front();
            free_id_list.pop_front();
        }
        // else return 0xFFFFFFFF, signaling 'error'
    }
    // random IDs mode (dfp_id/client_id)
    else {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(ids.empty()) {
            if(start_rand_with_0) {
                ret = 0;
            }
            else {
                ret = rand();
                if(disallow_0_deadbeef && (ret == 0 || ret == 0xDEADBEEF)) {
                    // scan until not a forbidden ID
                    while(ret == 0 || ret == 0xDEADBEEF) {
                        ret = rand();
                    }
                }
            }
        }
        else {
            ret = rand();
            if(disallow_0_deadbeef && (ret == 0 || ret == 0xDEADBEEF)) {
                // scan until we don't have duplicates nor forbidden IDs
                while(ret == 0 || ret == 0xDEADBEEF || ids.count(ret) > 0) {
                    ret = rand();
                }
            }
            else {
                // scan until we don't have duplicates
                while(ids.count(ret) > 0) {
                    ret = rand();
                }
            }
        }
        ids.insert(ret);
    }

    return ret;

}

// checks if an ID already exists
bool IDTracker::check(uint32_t id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(sequential_order) {
        // unsorted list -- have to O(n) linear search
        bool found = false;
        for(size_t i = 0; i < free_id_list.size(); i++) {
            if(free_id_list[i] == id) {
                found = true;
                break;
            }
        }
        return found;
    }
    else {
        return (ids.count(id) > 0);
    }
}

// retire this ID number like an athlete's jersey
void IDTracker::retire(uint32_t id)
{
    // sequential mode (driver ctx)
    if(sequential_order) {
        // make sure it's not already in the list!
        if(this->check(id)) {
            // already exists -- exit
        }
        else {
            // actually do the retirement
            std::lock_guard<std::mutex> lock(m_mutex);
            free_id_list.push_back(id);
        }
    }
    // random IDs mode (dfp_id/client_id)
    else {
        if(disallow_0_deadbeef && (id == 0 || id == 0xDEADBEEF)) {
            // don't do that!!
        }
        else {
            // if present, retire it -- else already retired
            std::lock_guard<std::mutex> lock(m_mutex);
            if(ids.count(id) > 0) {
                ids.erase(id);
            }
        }
    }
}

void IDTracker::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (sequential_order) {
        free_id_list.clear();
        for(uint32_t i = 0; i < max_sequential_val; i++) {
            free_id_list.push_back(i);
        }
    } else {
        ids.clear();
    }
}

// prints current clientlist data
void IDTracker::print()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(sequential_order) {
        std::cout << "IDTracker (seq. free vals): ";
        for (const auto &element : free_id_list) {
            std::cout << element << " ";
        }
    }
    else {
        std::cout << "IDTracker (tracked rand vals): ";
        for (const auto &element : ids) {
            std::cout << element << " ";
        }
    }
    std::cout << std::endl;
}

// Returns how many IDs have been created and not retired
size_t IDTracker::active_count()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (sequential_order) {
        return static_cast<size_t>(max_sequential_val) - free_id_list.size();
    } else {
        return ids.size();
    }
}

} // namespace Utils
} // namespace MX
