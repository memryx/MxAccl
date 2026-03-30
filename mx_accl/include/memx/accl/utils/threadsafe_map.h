// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef THREADSAFE_MAP_H
#define THREADSAFE_MAP_H

#pragma once
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>

namespace MX
{
namespace Utils
{

template <typename K, typename V>
class ThreadSafeMap
{
  public:
    // Updates or inserts a value
    void update(const K &key, V value)
    {
        std::lock_guard<std::mutex> lock(m_);
        map_[key] = std::move(value);
    }

    // Accessor: Returns a reference. 
    // WARNING: Using this reference after the lock is released is thread-unsafe.
    V& operator[](const K &key)
    {
        std::lock_guard<std::mutex> lock(m_);
        // map_[key] automatically default-constructs V if it doesn't exist
        return map_[key];
    }

    // Const version for snapshotting (returns pointers to const values)
    std::vector<std::pair<K, V>> get_all_pairs() const
    {
        std::lock_guard<std::mutex> lock(m_);
        // We create a complete copy of the map's contents into a vector
        std::vector<std::pair<K, V>> out;
        out.reserve(map_.size());
        for (const auto &kv : map_) {
            out.emplace_back(kv.first, kv.second);
        }
        return out;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_);
        map_.clear();
    }

    size_t count(const K &key) const
    {
        std::lock_guard<std::mutex> lock(m_);
        return map_.count(key);
    }

  private:
    mutable std::mutex m_;
    std::unordered_map<K, V> map_;
};

template <typename K, typename V>
class ThreadSafePtrMap
{
  public:
    // Initialize a key with a new object if it doesn't exist
    void init(const K &key)
    {
        std::lock_guard<std::mutex> lock(m_);
        if (map_.count(key) == 0) {
            map_[key] = std::make_unique<V>();
        }
    }

    // operator[] write-style (returns raw pointer safely)
    V* operator[](const K &key)
    {
        std::lock_guard<std::mutex> lock(m_);
        if (map_.count(key) == 0) {
            map_[key] = std::make_unique<V>();
        }
        return map_[key].get();
    }

    // Return a vector of all <key,value> pairs (raw pointers)
    std::vector<std::pair<K, V*>> get_all_pairs() const
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::pair<K, V*>> out;
        out.reserve(map_.size());
        for (const auto &kv : map_) {
            out.emplace_back(kv.first, kv.second.get());
        }
        return out;
    }

    // Clear all values
    void clear()
    {
        std::lock_guard<std::mutex> lock(m_);
        map_.clear(); // unique_ptr automatically deletes the objects
    }

    int count(const K &key) const
    {
        std::lock_guard<std::mutex> lock(m_);
        return map_.count(key);
    }

  private:
    mutable std::mutex m_;
    std::unordered_map<K, std::unique_ptr<V>> map_;
};

} // namespace Utils
} // namespace MX

#endif // THREADSAFE_MAP_H