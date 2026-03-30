// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef RENTAL_STORE_H
#define RENTAL_STORE_H

#pragma once
#include <vector>

#include <memx/accl/utils/macros.h>
#include <memx/accl/utils/blocky_queue.h>


namespace MX
{
namespace Utils
{


template<typename T>
class RentalStore
{
  public:

    // constructor with fixed capacity
    // also initializes all items in the store using the
    // T constructor with args from Args list
    template<typename... Args>
    explicit RentalStore(unsigned int capacity, Args &&... args)
    {
        store.resize(capacity, T(std::forward<Args>(args)...) );
        for(unsigned int i = 0; i < capacity; i++) {
            freelist.push(&store[i]);
        }
    }

    // gets the next available item from the freelist, or nullptr on error
    T* checkout_item()
    {
        T* item = freelist.pop();
        if(UNLIKELY(item == nullptr)) {
            // ERROR
            return nullptr;
        }
        return item;
    }

    // gets the next available item from the freelist, or nullptr on timeout/error
    T* checkout_item_timeout(unsigned int timeout_ms)
    {
        T* item = nullptr;
        bool success = freelist.pop_timeout(item, timeout_ms);
        if(UNLIKELY(!success)) {
            // timeout or error
            return nullptr;
        }
        return item;
    }

    // user returns an item to the freelist
    void return_item(T* item)
    {
        freelist.push(item);
    }


  private:
    std::vector<T> store;
    BlockyQueue<T*> freelist;

};





} // namespace Utils
} // namespace MX

#endif // RENTAL_STORE_H
