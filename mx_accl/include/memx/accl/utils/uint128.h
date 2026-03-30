// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_UINT128_H
#define MX_UINT128_H

#pragma once
#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER)

#include <intrin.h>  // for _umul128

struct uint128_t {
    uint64_t lo;
    uint64_t hi;

    // ctors
    constexpr uint128_t() noexcept : lo(0), hi(0) {}
    constexpr uint128_t(uint64_t _lo, uint64_t _hi) noexcept : lo(_lo), hi(_hi) {}
    // implicit from 64-bit
    constexpr uint128_t(uint64_t v) noexcept : lo(v), hi(0) {}

    // assignment from 64-bit
    constexpr uint128_t &operator=(uint64_t v) noexcept { lo = v; hi = 0; return *this; }

    // addition
    friend constexpr uint128_t operator+(const uint128_t &a, const uint128_t &b) noexcept
    {
        uint64_t l = a.lo + b.lo;
        uint64_t carry = (l < a.lo);
        return uint128_t(l, a.hi + b.hi + carry);
    }
    inline uint128_t &operator+=(const uint128_t &o) noexcept { return *this = *this + o; }

    // subtraction
    friend constexpr uint128_t operator-(const uint128_t &a, const uint128_t &b) noexcept
    {
        uint64_t l = a.lo - b.lo;
        uint64_t borrow = (a.lo < b.lo);
        return uint128_t(l, a.hi - b.hi - borrow);
    }
    inline uint128_t &operator-=(const uint128_t &o) noexcept { return *this = *this - o; }

    // multiplication (mod 2^128)
    friend inline uint128_t operator*(const uint128_t &a, const uint128_t &b) noexcept
    {
        uint64_t mid_hi;
        uint64_t low = _umul128(a.lo, b.lo, &mid_hi);
        uint64_t hi = mid_hi + a.lo * b.hi + a.hi * b.lo;
        return uint128_t(low, hi);
    }
    inline uint128_t &operator*=(const uint128_t &o) noexcept { return *this = *this * o; }

    // left shift
    friend constexpr uint128_t operator<<(const uint128_t &a, unsigned int s) noexcept
    {
        if      (s == 0) { return a; }
        else if (s < 64) { return uint128_t(a.lo << s, (a.hi << s) | (a.lo >> (64 - s))); }
        else if (s < 128) { return uint128_t(0, a.lo << (s - 64)); }
        else { return uint128_t(0, 0); }
    }
    constexpr uint128_t &operator<<=(unsigned int s) noexcept { return *this = *this << s; }

    // right shift
    friend constexpr uint128_t operator>>(const uint128_t &a, unsigned int s) noexcept
    {
        if      (s == 0) { return a; }
        else if (s < 64) { return uint128_t((a.lo >> s) | (a.hi << (64 - s)), a.hi >> s); }
        else if (s < 128) { return uint128_t(a.hi >> (s - 64), 0); }
        else { return uint128_t(0, 0); }
    }
    constexpr uint128_t &operator>>=(unsigned int s) noexcept { return *this = *this >> s; }

    // bitwise AND/OR/XOR/NOT
    friend constexpr uint128_t operator&(const uint128_t &a, const uint128_t &b) noexcept
    {
        return uint128_t(a.lo & b.lo, a.hi & b.hi);
    }
    constexpr uint128_t &operator&=(const uint128_t &o) noexcept { lo &= o.lo; hi &= o.hi; return *this; }

    friend constexpr uint128_t operator|(const uint128_t &a, const uint128_t &b) noexcept
    {
        return uint128_t(a.lo | b.lo, a.hi | b.hi);
    }
    constexpr uint128_t &operator|=(const uint128_t &o) noexcept { lo |= o.lo; hi |= o.hi; return *this; }

    friend constexpr uint128_t operator^(const uint128_t &a, const uint128_t &b) noexcept
    {
        return uint128_t(a.lo ^ b.lo, a.hi ^ b.hi);
    }
    constexpr uint128_t &operator^=(const uint128_t &o) noexcept { lo ^= o.lo; hi ^= o.hi; return *this; }

    friend constexpr uint128_t operator~(const uint128_t &a) noexcept
    {
        return uint128_t(~a.lo, ~a.hi);
    }

    // comparisons
    friend constexpr bool operator==(const uint128_t &a, const uint128_t &b) noexcept
    {
        return a.lo == b.lo && a.hi == b.hi;
    }
    friend constexpr bool operator!=(const uint128_t &a, const uint128_t &b) noexcept
    {
        return !(a == b);
    }
    friend constexpr bool operator<(const uint128_t &a, const uint128_t &b) noexcept
    {
        return (a.hi < b.hi) || (a.hi == b.hi && a.lo < b.lo);
    }
    friend constexpr bool operator<=(const uint128_t &a, const uint128_t &b) noexcept
    {
        return (a.hi < b.hi) || (a.hi == b.hi && a.lo <= b.lo);
    }
    friend constexpr bool operator>(const uint128_t &a, const uint128_t &b) noexcept
    {
        return b < a;
    }
    friend constexpr bool operator>=(const uint128_t &a, const uint128_t &b) noexcept
    {
        return b <= a;
    }

    // division and modulo via simple long-division
    friend inline uint128_t operator/(const uint128_t &n, const uint128_t &d) noexcept
    {
        uint128_t q(0), r(0);
        for(int i = 127; i >= 0; --i) {
            r <<= 1;
            r.lo |= (n >> i).lo & 1;
            if (r >= d) {
                r -= d;
                if (i < 64) { q.lo |= uint64_t(1) << i; }
                else { q.hi |= uint64_t(1) << (i - 64); }
            }
        }
        return q;
    }
    friend inline uint128_t operator%(const uint128_t &n, const uint128_t &d) noexcept
    {
        uint128_t q(0), r(0);
        for(int i = 127; i >= 0; --i) {
            r <<= 1;
            r.lo |= (n >> i).lo & 1;
            if (r >= d) {
                r -= d;
                if (i < 64) { q.lo |= uint64_t(1) << i; }
                else { q.hi |= uint64_t(1) << (i - 64); }
            }
        }
        return r;
    }
    template < typename T,
               typename = typename std::enable_if <
                   std::is_integral<T>::value &&
                   std::is_unsigned<T>::value &&
                   (sizeof(T) <= sizeof(uint64_t))
                    >::type
                   >
               constexpr operator T() const noexcept
    {
        return static_cast<T>(lo);
    }
};

#else

// GCC / Clang have built-in 128u
using uint128_t = unsigned __int128;

#endif


#endif // MX_UINT128_H
