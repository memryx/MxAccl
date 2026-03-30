// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_UTILS_LOCKED_VAR_H
#define MX_UTILS_LOCKED_VAR_H

#pragma once
#include <condition_variable>
#include <mutex>
#include <shared_mutex>

namespace MX
{
namespace Utils
{

// LockedVar: a thread-safe wrapper
// around a variable of type T
//
// Use this class for counters and
// flags that only have ~1 reader
template<typename T>
class LockedVar
{

  public:
    LockedVar(T initial_value)
    {
        value = initial_value;
        killed = false;
    }

    // constructor with default value
    LockedVar() : value{}
    {
        killed = false;
    }

    ~LockedVar()
    {
        kill();
    }

    void kill()
    {
        {
            std::unique_lock<std::mutex> lock(m);
            killed = true;
            lock.unlock();
        }
        s_value_changed.notify_all();
    }

    T load() const
    {
        std::lock_guard<std::mutex> lock(m);
        return value;
    }

    void store(T new_value)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            value = new_value;
        }
        s_value_changed.notify_all();
    }

    bool wait_for_value(T expected_value) const
    {
        std::unique_lock<std::mutex> lock(m);
        s_value_changed.wait(lock, [this, expected_value] { return (value == expected_value || killed == true); });
        return !killed;
    }

    // copy constructor
    LockedVar(const LockedVar<T> &other)
    {
        std::lock_guard<std::mutex> lock(other.m);
        value = other.value;
        killed = other.killed;
    }

    // assignment operator to another LockedVar
    LockedVar<T> &operator=(const LockedVar<T> &other)
    {
        if(this != &other) {
            {
                std::scoped_lock lock(m, other.m);
                value = other.value;
                killed = other.killed;
            }
            s_value_changed.notify_all();
        }
        return *this;
    }

    // assignment operator to T
    LockedVar<T> &operator=(const T &new_value)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            value = new_value;
        }
        s_value_changed.notify_all();
        return *this;
    }

    // move constructor
    LockedVar(LockedVar<T> &&other) noexcept
    {
        std::lock_guard<std::mutex> lock(other.m);
        value = std::move(other.value);
        killed = other.killed;
    }

    // move assignment operator
    LockedVar<T> &operator=(LockedVar<T> &&other) noexcept
    {
        if(this != &other) {
            {
                std::scoped_lock lock(m, other.m);
                value = std::move(other.value);
                killed = other.killed;
            }
            s_value_changed.notify_all();
        }
        return *this;
    }

    // increment operator (operator++)
    // only enabled for integral types
    template<typename U = T>
    typename std::enable_if<std::is_integral<U>::value, LockedVar<T>&>::type
    operator++()
    {
        {
            std::lock_guard<std::mutex> lock(m);
            ++value;
        }
        s_value_changed.notify_all();
        return *this;
    }

    // postfix operator (operator++(int))
    // only enabled for integral types
    template<typename U = T>
    typename std::enable_if<std::is_integral<U>::value, LockedVar<T>&>::type
    operator++(int)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            value++;
        }
        s_value_changed.notify_all();
        return *this;
    }

    // postfix decrement operator (operator--(int))
    // only enabled for integral types
    template<typename U = T>
    typename std::enable_if<std::is_integral<U>::value, LockedVar<T>&>::type
    operator--(int)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            value--;
        }
        s_value_changed.notify_all();
        return *this;
    }

    // decrement operator (operator--)
    // only enabled for integral types
    template<typename U = T>
    typename std::enable_if<std::is_integral<U>::value, LockedVar<T>&>::type
    operator--()
    {
        {
            std::lock_guard<std::mutex> lock(m);
            --value;
        }
        s_value_changed.notify_all();
        return *this;
    }

    // addition assignment operator (operator+=)
    // only enabled for integral and floating-point types
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, LockedVar<T>&>::type
    operator+=(const T &rhs)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            value += rhs;
        }
        s_value_changed.notify_all();
        return *this;
    }

    // subtraction assignment operator (operator-=)
    // only enabled for integral and floating-point types
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, LockedVar<T>&>::type
    operator-=(const T &rhs)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            value -= rhs;
        }
        s_value_changed.notify_all();
        return *this;
    }

    // conversion operator to T
    operator T() const
    {
        std::lock_guard<std::mutex> lock(m);
        return value;
    }

    // equality operator
    bool operator==(const T &rhs) const
    {
        std::lock_guard<std::mutex> lock(m);
        return value == rhs;
    }

    // inequality operator
    bool operator!=(const T &rhs) const
    {
        std::lock_guard<std::mutex> lock(m);
        return value != rhs;
    }

    // less than operator
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator<(const T &rhs) const
    {
        std::lock_guard<std::mutex> lock(m);
        return value < rhs;
    }

    // greater than operator
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator>(const T &rhs) const
    {
        std::lock_guard<std::mutex> lock(m);
        return value > rhs;
    }

    // less than or equal operator
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator<=(const T &rhs) const
    {
        std::lock_guard<std::mutex> lock(m);
        return value <= rhs;
    }

    // greater than or equal operator
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator>=(const T &rhs) const
    {
        std::lock_guard<std::mutex> lock(m);
        return value >= rhs;
    }

    // swap function
    void swap(LockedVar<T> &other)
    {
        if(this != &other) {
            {
                std::scoped_lock lock(m, other.m);
                std::swap(value, other.value);
                std::swap(killed, other.killed);
            }
            s_value_changed.notify_all();
            other.s_value_changed.notify_all();
        }
    }

    // "wait for greater than" function:
    //  useful for counters waiting until >0
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    wait_for_greater_than(T threshold) const
    {
        std::unique_lock<std::mutex> lock(m);
        s_value_changed.wait(lock, [this, threshold] { return (value > threshold || killed == true); });
        return !killed;
    }

  private:
    T value;
    bool killed;
    mutable std::mutex m;
    mutable std::condition_variable s_value_changed;
};


// SharedLockedVar: LockedVar, but
// using shared_mutex for write-once, read-many scenarios
//
// Use this for "running" flags and the like that may
// have many reader threads accessing it
template<typename T>
class SharedLockedVar
{

  public:
    SharedLockedVar(T initial_value)
    {
        value = initial_value;
    }

    // constructor with default value
    SharedLockedVar() : value{} {}

    ~SharedLockedVar() {}
    
    T load() const
    {
        std::shared_lock<std::shared_mutex> lock(m);
        return value;
    }

    void store(T new_value)
    {
        std::unique_lock<std::shared_mutex> lock(m);
        value = new_value;
        lock.unlock();
    }

    // copy constructor
    SharedLockedVar(const SharedLockedVar<T> &other)
    {
        std::unique_lock<std::shared_mutex> lock(other.m);
        value = other.value;
    }

    // assignment operator to another SharedLockedVar
    SharedLockedVar<T> &operator=(const SharedLockedVar<T> &other)
    {
        if(this != &other) {
            {
                std::scoped_lock<std::shared_mutex> lock(m, other.m);
                value = other.value;
            }
        }
        return *this;
    }

    // assignment operator to T
    SharedLockedVar<T> &operator=(const T &new_value)
    {
        std::unique_lock<std::shared_mutex> lock(m);
        value = new_value;
        lock.unlock();
        return *this;
    }

    // move constructor
    SharedLockedVar(SharedLockedVar<T> &&other) noexcept
    {
        std::unique_lock<std::shared_mutex> lock(other.m);
        value = std::move(other.value);
    }

    // move assignment operator
    SharedLockedVar<T> &operator=(SharedLockedVar<T> &&other) noexcept
    {
        if(this != &other) {
            {
                std::scoped_lock<std::shared_mutex> lock(m, other.m);
                value = std::move(other.value);
            }
        }
        return *this;
    }

    // increment operator (operator++)
    // only enabled for integral types
    template<typename U = T>
    typename std::enable_if<std::is_integral<U>::value, SharedLockedVar<T>&>::type
    operator++()
    {
        std::unique_lock<std::shared_mutex> lock(m);
        ++value;
        lock.unlock();
        return *this;
    }

    // postfix operator (operator++(int))
    // only enabled for integral types
    template<typename U = T>
    typename std::enable_if<std::is_integral<U>::value, SharedLockedVar<T>&>::type
    operator++(int)
    {
        std::unique_lock<std::shared_mutex> lock(m);
        value++;
        lock.unlock();
        return *this;
    }

    // postfix decrement operator (operator--(int))
    // only enabled for integral types
    template<typename U = T>
    typename std::enable_if<std::is_integral<U>::value, SharedLockedVar<T>&>::type
    operator--(int)
    {
        std::unique_lock<std::shared_mutex> lock(m);
        value--;
        lock.unlock();
        return *this;
    }

    // decrement operator (operator--)
    // only enabled for integral types
    template<typename U = T>
    typename std::enable_if<std::is_integral<U>::value, SharedLockedVar<T>&>::type
    operator--()
    {
        std::unique_lock<std::shared_mutex> lock(m);
        --value;
        lock.unlock();
        return *this;
    }

    // addition assignment operator (operator+=)
    // only enabled for integral and floating-point types
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, SharedLockedVar<T>&>::type
    operator+=(const T &rhs)
    {
        std::unique_lock<std::shared_mutex> lock(m);
        value += rhs;
        lock.unlock();
        return *this;
    }

    // subtraction assignment operator (operator-=)
    // only enabled for integral and floating-point types
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, SharedLockedVar<T>&>::type
    operator-=(const T &rhs)
    {
        std::unique_lock<std::shared_mutex> lock(m);
        value -= rhs;
        lock.unlock();
        return *this;
    }

    // conversion operator to T
    operator T() const
    {
        std::shared_lock<std::shared_mutex> lock(m);
        return value;
    }

    // equality operator
    bool operator==(const T &rhs) const
    {
        std::shared_lock<std::shared_mutex> lock(m);
        return value == rhs;
    }

    // inequality operator
    bool operator!=(const T &rhs) const
    {
        std::shared_lock<std::shared_mutex> lock(m);
        return value != rhs;
    }

    // less than operator
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator<(const T &rhs) const
    {
        std::shared_lock<std::shared_mutex> lock(m);
        return value < rhs;
    }

    // greater than operator
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator>(const T &rhs) const
    {
        std::shared_lock<std::shared_mutex> lock(m);
        return value > rhs;
    }

    // less than or equal operator
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator<=(const T &rhs) const
    {
        std::shared_lock<std::shared_mutex> lock(m);
        return value <= rhs;
    }

    // greater than or equal operator
    template<typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator>=(const T &rhs) const
    {
        std::shared_lock<std::shared_mutex> lock(m);
        return value >= rhs;
    }

    // swap function
    void swap(SharedLockedVar<T> &other)
    {
        if(this != &other) {
            {
                std::scoped_lock<std::shared_mutex> lock(m, other.m);
                std::swap(value, other.value);
            }
        }
    }

  private:
    T value;
    mutable std::shared_mutex m;
};


} // namespace Utils
} // namespace MX

#endif
