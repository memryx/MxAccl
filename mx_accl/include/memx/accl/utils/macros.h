// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_MACROS_H
#define MX_MACROS_H

#pragma once

// Branch predictor hints: speeds up critical paths
#ifdef __GNUC__
    #define LIKELY(condition)   __builtin_expect(static_cast<bool>(condition), true)
    #define UNLIKELY(condition) __builtin_expect(static_cast<bool>(condition), false)
#else
    #define LIKELY(condition)     (condition)
    #define UNLIKELY(condition)   (condition)
#endif

// Thread Sanitizer annotation macros
#ifdef __SANITIZE_THREAD__
    #include <sanitizer/tsan_interface.h>
    #define TSAN_ACQUIRE(a)  __tsan_acquire(a)
    #define TSAN_RELEASE(a)  __tsan_release(a)
#else
    #define TSAN_ACQUIRE(a)
    #define TSAN_RELEASE(a)
#endif

// Pipeline pressure thresholds
#define MEMX_PRESSURE_LOW_THRESH 20.0f
#define MEMX_PRESSURE_MEDIUM_THRESH 65.0f
#define MEMX_PRESSURE_HIGH_THRESH 92.0f

#endif // MX_MACROS_H
