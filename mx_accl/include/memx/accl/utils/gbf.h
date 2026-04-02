// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_GBF_H
#define MX_GBF_H

#pragma once
#include <cstdint>
#include <cstring>

#include <memx/accl/utils/macros.h>

// arch optimization checks
#ifdef __linux__
    #if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
        #if defined(__BMI__) && defined(__BMI2__) && defined(__LZCNT__) && defined(__AVX2__)
            #ifndef USE_X86_OPT
                #define USE_X86_OPT
                #include <x86intrin.h>
            #endif
        #endif
    #elif defined(__aarch64__) || defined(_M_ARM64)
        #ifndef USE_ARM64_OPT
            #define USE_ARM64_OPT
            #include <arm_neon.h>
        #endif
    #endif
#endif



namespace MX
{
namespace Types
{
typedef struct _MemxGbfGbf80Map {
    unsigned int man_0 : 8;
    unsigned int sign_0 : 1;
    unsigned int man_1 : 8;
    unsigned int sign_1 : 1;
    unsigned int man_2 : 8;
    unsigned int sign_2 : 1;
    unsigned int man_3_0 : 5; // because of C compiler will align to 4 bytes
    unsigned int man_3_1 : 3; // if we declare 9 bits here, it will be placed to bit[32:32+9]
    unsigned int sign_3 : 1;
    unsigned int man_4 : 8;
    unsigned int sign_4 : 1;
    unsigned int man_5 : 8;
    unsigned int sign_5 : 1;
    unsigned int man_6 : 8;
    unsigned int sign_6 : 1;
    unsigned int man_7_0 : 1;
    unsigned int man_7_1 : 7;
    unsigned int sign_7 : 1;
    unsigned int exp : 8;
    // to be noticed, this structure will actually be aligned to 12 bytes
    // but do not append dummy padding manually or will cause memory violation
} MemxGbfGbf80Map;

typedef struct _MemxGbfFloat32Map {
    unsigned int zero : 16; // always zeros field
    unsigned int man : 7;
    unsigned int exp : 8;
    unsigned int sign : 1;
} MemxGbfFloat32Map;


inline
void gbf_encode(uint32_t* __restrict flt32_buffer, uint8_t* __restrict gbf80_buffer, int length)
{
    MemxGbfGbf80Map* gbf80_map;
    MemxGbfFloat32Map* flt32_map;
    uint8_t* gbf80;
    uint32_t* flt32;
    int gbf80_offset = 0;
    int flt32_offset = 0;

    unsigned char exp;
    unsigned char man;
#define _MX_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define _SET_MANTISSA_SHIFT_WITH_ROUNDING(_exp_shift_)                                                                       \
    do                                                                                                                       \
    {                                                                                                                        \
        if ((_exp_shift_) == 0)                                                                                              \
        {                                                                                                                    \
            man = (flt32_map->man == 0x7f) ? (unsigned char)(0x80 | flt32_map->man)                                          \
                  : (unsigned char)(0x80 | flt32_map->man) + ((flt32_map->zero >> 15) & 0x1);       \
        }                                                                                                                    \
        else                                                                                                                 \
        {                                                                                                                    \
            man = (unsigned char)((0x80 | flt32_map->man) >> (_exp_shift_)) + ((flt32_map->man >> ((_exp_shift_)-1)) & 0x1); \
        }                                                                                                                    \
    } while (0)

    while ((flt32_offset < length)) {
        gbf80 = gbf80_buffer + gbf80_offset;
        flt32 = flt32_buffer + flt32_offset;

        // performs float32 to float16 rounding, based on IEEE floating point design
        // no need to handle exponent and mantissa separately
        for (int i = 0; i < 8; ++i) {
            if (flt32_offset + i < length) {
                *(uint32_t*)(flt32 + i) += 0x00008000;
                *(uint32_t*)(flt32 + i) &= 0xffff0000;
            }
        }

        // gets maximum exponent among 8 floating points
        exp = 0;
        for (int i = 0; i < 8; ++i) {
            if (flt32_offset + i < length) {
                flt32_map = (MemxGbfFloat32Map*)(flt32 + i);
                exp = _MX_MAX(exp, (unsigned char)flt32_map->exp);
            }
        }

        // combines 8 floating points to gbf80
        gbf80_map = (MemxGbfGbf80Map*)gbf80;
        gbf80_map->exp = exp;
        if (flt32_offset + 0 < length) {
            flt32_map = (MemxGbfFloat32Map*)(flt32 + 0);
            _SET_MANTISSA_SHIFT_WITH_ROUNDING(exp - flt32_map->exp);
            gbf80_map->man_0 = man & 0xff;
            gbf80_map->sign_0 = (man & 0xff) ? flt32_map->sign : 0;
        }
        else {
            gbf80_map->man_0 = 0;
            gbf80_map->sign_0 = 0;
        }
        if (flt32_offset + 1 < length) {
            flt32_map = (MemxGbfFloat32Map*)(flt32 + 1);
            _SET_MANTISSA_SHIFT_WITH_ROUNDING(exp - flt32_map->exp);
            gbf80_map->man_1 = man & 0xff;
            gbf80_map->sign_1 = (man & 0xff) ? flt32_map->sign : 0;
        }
        else {
            gbf80_map->man_1 = 0;
            gbf80_map->sign_1 = 0;
        }
        if (flt32_offset + 2 < length) {
            flt32_map = (MemxGbfFloat32Map*)(flt32 + 2);
            _SET_MANTISSA_SHIFT_WITH_ROUNDING(exp - flt32_map->exp);
            gbf80_map->man_2 = man & 0xff;
            gbf80_map->sign_2 = (man & 0xff) ? flt32_map->sign : 0;
        }
        else {
            gbf80_map->man_2 = 0;
            gbf80_map->sign_2 = 0;
        }
        if (flt32_offset + 3 < length) {
            flt32_map = (MemxGbfFloat32Map*)(flt32 + 3);
            _SET_MANTISSA_SHIFT_WITH_ROUNDING(exp - flt32_map->exp);
            gbf80_map->man_3_0 = man & 0x1f;
            gbf80_map->man_3_1 = (man >> 5) & 0x7;
            gbf80_map->sign_3 = (man & 0xff) ? flt32_map->sign : 0;
        }
        else {
            gbf80_map->man_3_0 = 0;
            gbf80_map->man_3_1 = 0;
            gbf80_map->sign_3 = 0;
        }
        if (flt32_offset + 4 < length) {
            flt32_map = (MemxGbfFloat32Map*)(flt32 + 4);
            _SET_MANTISSA_SHIFT_WITH_ROUNDING(exp - flt32_map->exp);
            gbf80_map->man_4 = man & 0xff;
            gbf80_map->sign_4 = (man & 0xff) ? flt32_map->sign : 0;
        }
        else {
            gbf80_map->man_4 = 0;
            gbf80_map->sign_4 = 0;
        }
        if (flt32_offset + 5 < length) {
            flt32_map = (MemxGbfFloat32Map*)(flt32 + 5);
            _SET_MANTISSA_SHIFT_WITH_ROUNDING(exp - flt32_map->exp);
            gbf80_map->man_5 = man & 0xff;
            gbf80_map->sign_5 = (man & 0xff) ? flt32_map->sign : 0;
        }
        else {
            gbf80_map->man_5 = 0;
            gbf80_map->sign_5 = 0;
        }
        if (flt32_offset + 6 < length) {
            flt32_map = (MemxGbfFloat32Map*)(flt32 + 6);
            _SET_MANTISSA_SHIFT_WITH_ROUNDING(exp - flt32_map->exp);
            gbf80_map->man_6 = man & 0xff;
            gbf80_map->sign_6 = (man & 0xff) ? flt32_map->sign : 0;
        }
        else {
            gbf80_map->man_6 = 0;
            gbf80_map->sign_6 = 0;
        }
        if (flt32_offset + 7 < length) {
            flt32_map = (MemxGbfFloat32Map*)(flt32 + 7);
            _SET_MANTISSA_SHIFT_WITH_ROUNDING(exp - flt32_map->exp);
            gbf80_map->man_7_0 = man & 0x1;
            gbf80_map->man_7_1 = (man >> 1) & 0x7f;
            gbf80_map->sign_7 = (man & 0xff) ? flt32_map->sign : 0;
        }
        else {
            gbf80_map->man_7_0 = 0;
            gbf80_map->man_7_1 = 0;
            gbf80_map->sign_7 = 0;
        }

        gbf80_offset += 10;
        flt32_offset += 8;
    }

} // gbf_encode;


//===========================================================================
// LZCNT
static const uint8_t lzcnt_lut[256] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#define lzcnt8(x) lzcnt_lut[(x)]


//===========================================================================
// SATURATED SUBTRACT
inline uint8_t saturated_subtract8(uint8_t a, uint8_t b)
{
    uint8_t res = a - b;
    res &= -(res <= a);
    return res;
}


//===========================================================================
// PREFETCH
#ifdef USE_X86_OPT
    #define prefetch_ptr(x) _mm_prefetch((x), _MM_HINT_T0)
#else
    #if (__GNUC__ >= 10)
        #if __has_builtin(__builtin_prefetch)
            #define prefetch_ptr(x) __builtin_prefetch((x))
        #else
            #define prefetch_ptr(x) {}
        #endif
    #else
        #define prefetch_ptr(x) {}
    #endif
#endif


//===========================================================================
// X86 AVX2 constants
#ifdef USE_X86_OPT
#endif


//===========================================================================
// GBF80 DECODERS
inline void gbf_decode(uint8_t* __restrict gbf80_buffer, uint32_t* __restrict flt32_buffer, unsigned int length)
{

    // ARM64 NEON optimized version
#ifdef USE_ARM64_OPT
    unsigned int off_g = 0;
    unsigned int off_f = 0;

    // Process in 8x8-bit lanes with ARM NEON
    while (off_f + 8 <= length) {

        uint64_t lo = 0;
        uint16_t hi = 0;
        uint32_t* out8 = flt32_buffer + off_f;
        memcpy(&lo, gbf80_buffer + off_g, 8);
        memcpy(&hi, gbf80_buffer + off_g + 8, 2);

        uint8_t exp = (uint8_t)(hi >> 8);

        // extract and set signs
        uint32_t s0 = (uint32_t)(((lo >>  8) & 1u) << 31);
        uint32_t s1 = (uint32_t)(((lo >> 17) & 1u) << 31);
        uint32_t s2 = (uint32_t)(((lo >> 26) & 1u) << 31);
        uint32_t s3 = (uint32_t)(((lo >> 35) & 1u) << 31);
        uint32_t s4 = (uint32_t)(((lo >> 44) & 1u) << 31);
        uint32_t s5 = (uint32_t)(((lo >> 53) & 1u) << 31);
        uint32_t s6 = (uint32_t)(((lo >> 62) & 1u) << 31);
        uint32_t s7 = (uint32_t)(((hi >>  7) & 1u) << 31);

        // set mantissas
        uint8_t t_arr[8];
        t_arr[0] = (uint8_t)((lo >>  0) & 0xFF);
        t_arr[1] = (uint8_t)((lo >>  9) & 0xFF);
        t_arr[2] = (uint8_t)((lo >> 18) & 0xFF);
        t_arr[3] = (uint8_t)((lo >> 27) & 0xFF);
        t_arr[4] = (uint8_t)((lo >> 36) & 0xFF);
        t_arr[5] = (uint8_t)((lo >> 45) & 0xFF);
        t_arr[6] = (uint8_t)((lo >> 54) & 0xFF);
        t_arr[7] = (uint8_t)((((hi & 0x007F) << 1) | ((lo >> 63) & 1u)) & 0xFF);

        // Load into NEON 8x8-bit vector
        uint8x8_t t = vld1_u8(t_arr); // 1.m in 8 lanes
        uint8x8_t expv = vdup_n_u8(exp);

        // d = lzcnt
        uint8x8_t d = vclz_u8(t);

        // e = sat_sub(exp, d)
        uint8x8_t e = vqsub_u8(expv, d);

        // zero e if t==0
        uint8x8_t d_is_8 = vceq_u8(d, vdup_n_u8(8));
        e = vbic_u8(e, d_is_8);

        // m = (t << d) & 0x7F
        int8x8_t  d_s = vreinterpret_s8_u8(d);
        uint8x8_t m8  = vshl_u8(t, d_s);
        m8 = vand_u8(m8, vdup_n_u8(0x7F));

        // widen to 32-bit and deposit bits
        // out = sign<<31 | e<<23 | m<<16
        uint16x8_t e16 = vmovl_u8(e);
        uint16x8_t m16 = vmovl_u8(m8);

        uint32x4_t e_lo32 = vmovl_u16(vget_low_u16(e16));
        uint32x4_t e_hi32 = vmovl_u16(vget_high_u16(e16));
        uint32x4_t m_lo32 = vmovl_u16(vget_low_u16(m16));
        uint32x4_t m_hi32 = vmovl_u16(vget_high_u16(m16));

        // shift into position
        e_lo32 = vshlq_n_u32(e_lo32, 23);
        e_hi32 = vshlq_n_u32(e_hi32, 23);
        m_lo32 = vshlq_n_u32(m_lo32, 16);
        m_hi32 = vshlq_n_u32(m_hi32, 16);

        // store everything but signs so far
        uint32x4_t out_lo = vorrq_u32(e_lo32, m_lo32);
        uint32x4_t out_hi = vorrq_u32(e_hi32, m_hi32);
        vst1q_u32(out8 + 0, out_lo);
        vst1q_u32(out8 + 4, out_hi);

        // OR in sign bits
        out8[0] |= s0;
        out8[1] |= s1;
        out8[2] |= s2;
        out8[3] |= s3;
        out8[4] |= s4;
        out8[5] |= s5;
        out8[6] |= s6;
        out8[7] |= s7;

        // bump the offsets
        off_f += 8;
        off_g += 10;

        // trigger prefetch
        __builtin_prefetch(gbf80_buffer + off_g, 0, 3); // prefetch for read
        __builtin_prefetch(flt32_buffer + off_f, 1, 3); // prefetch for write
    }

    // tail cases for num elems %8!=0
    if (off_f < length) {
        uint32_t tmp[8] = {0};
        // WARNING: we RELY on the invalid channels having
        //          all zeros when coming from the chip!
        gbf_decode(gbf80_buffer + off_g, tmp, 8);
        for (unsigned r = 0; off_f + r < length; ++r) {
            flt32_buffer[off_f + r] = tmp[r];
        }
    }

#else
#ifdef USE_X86_OPT
    // masks to extract 7 mantissa bytes and 7 sign bits from the low 64 bits
    // mantissas at [9k .. 9k+7], signs at bit (9k+8), k=0..6
    static const uint64_t MANT_MASK =
        (0xFFull << 0) | (0xFFull << 9) | (0xFFull << 18) |
        (0xFFull << 27) | (0xFFull << 36) | (0xFFull << 45) |
        (0xFFull << 54);

    static const uint64_t SIGN_MASK =
        (1ull << 8) | (1ull << 17) | (1ull << 26) |
        (1ull << 35) | (1ull << 44) | (1ull << 53) |
        (1ull << 62);

    // nibble lz table for 4-bit leading zeros: idx 0..15
    const __m128i LZ4 = _mm_setr_epi8(
                            4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0
                        );
    const __m128i V4   = _mm_set1_epi8(4);
    const __m128i V8   = _mm_set1_epi8(8);
    const __m128i Z128 = _mm_setzero_si128();

    // pow2 table for (1 << d) as 16-bit (lo/hi bytes shufflable with PSHUFB)
    // indices 0..8 are used; 9..15 are zero
    const __m128i POW2_LO = _mm_setr_epi8(
                                1, 2, 4, 8, 16, 32, 64, (char)128, 0, 0, 0, 0, 0, 0, 0, 0
                            );
    const __m128i POW2_HI = _mm_setr_epi8(
                                0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0
                            );
    const __m128i MASK7_16 = _mm_set1_epi16(0x007F);

    size_t off_g = 0;
    size_t off_f = 0;

    // main loop: 8 outputs per 10-byte packet
    while (off_f + 8 <= length) {
        const uint8_t* in = gbf80_buffer + off_g;

        // load the 80-bit packet as lo64 + hi16
        uint64_t lo; memcpy(&lo, in,   8);
        uint16_t hi; memcpy(&hi, in + 8, 2);

        // shared exponent
        const uint8_t exp = (uint8_t)(hi >> 8);

        // ---- extract mantissa bytes ----
        // 7 consecutive bytes (t0..t6) packed by PEXT in order
        const uint64_t mant56 = _pext_u64(lo, MANT_MASK);

        // t7 spans lo bit63 (LSB) and in[8][6:0] (MSBs)
        const uint8_t t7 = (uint8_t)(( (in[8] & 0x7Fu) << 1 ) | ((lo >> 63) & 1u));

        // assemble 8 mantissa bytes into an XMM
        __m128i TM = _mm_cvtsi64_si128((long long)mant56); // t0..t6 in bytes 0..6
        TM = _mm_insert_epi8(TM, t7, 7);

        // ---- extract sign bits ----
        const unsigned s7bits = (unsigned)_pext_u64(lo, SIGN_MASK); // s0..s6 in bits 0..6
        uint8_t sbytes_arr[8];
        sbytes_arr[0] = (uint8_t)((s7bits >> 0) & 1);
        sbytes_arr[1] = (uint8_t)((s7bits >> 1) & 1);
        sbytes_arr[2] = (uint8_t)((s7bits >> 2) & 1);
        sbytes_arr[3] = (uint8_t)((s7bits >> 3) & 1);
        sbytes_arr[4] = (uint8_t)((s7bits >> 4) & 1);
        sbytes_arr[5] = (uint8_t)((s7bits >> 5) & 1);
        sbytes_arr[6] = (uint8_t)((s7bits >> 6) & 1);
        sbytes_arr[7] = (uint8_t)((hi >> 7) & 1);
        __m128i SB = _mm_loadl_epi64((const __m128i*)sbytes_arr); // 8 sign bytes (0/1)

        // ---- compute d = lzcnt8(t) ----
        // hi/lo nibbles
        __m128i hi_nib = _mm_and_si128(_mm_srli_epi16(TM, 4), _mm_set1_epi8(0x0F));
        __m128i lo_nib = _mm_and_si128(TM,                     _mm_set1_epi8(0x0F));

        // lz4 lookups
        __m128i lz_hi = _mm_shuffle_epi8(LZ4, hi_nib);
        __m128i lz_lo = _mm_shuffle_epi8(LZ4, lo_nib);

        // if hi_nib != 0 -> d = lz_hi else 4 + lz_lo
        __m128i mask_hi_nz = _mm_cmpgt_epi8(hi_nib, Z128);
        __m128i d_from_lo  = _mm_add_epi8(V4, lz_lo);
        __m128i D          = _mm_blendv_epi8(d_from_lo, lz_hi, mask_hi_nz);

        // if t==0 -> d = 8
        __m128i mask_t_zero = _mm_cmpeq_epi8(TM, Z128);
        D = _mm_blendv_epi8(D, V8, mask_t_zero); // D in 0..8

        // ---- exponent: e = sat_sub(exp, d); zero if d==8 ----
        __m128i EXP8 = _mm_set1_epi8((char)exp);
        __m128i E8   = _mm_subs_epu8(EXP8, D);
        __m128i mask_d_eq8 = _mm_cmpeq_epi8(D, V8);
        E8 = _mm_andnot_si128(mask_d_eq8, E8); // zero out when d==8

        // ---- mantissa: m = ((t << d) & 0x7F) ----
        // factor = (1<<d) as 16-bit via PSHUFB tables
        __m128i fac_lo = _mm_shuffle_epi8(POW2_LO, D);
        __m128i fac_hi = _mm_shuffle_epi8(POW2_HI, D);
        __m128i FAC16  = _mm_unpacklo_epi8(fac_lo, fac_hi); // 8x u16

        // widen t to u16, multiply, mask to 7 bits
        __m128i T16 = _mm_cvtepu8_epi16(TM);
        __m128i M16 = _mm_mullo_epi16(T16, FAC16);
        M16 = _mm_and_si128(M16, MASK7_16);

        // ---- widen to 32-bit lanes ----
        __m256i M32  = _mm256_cvtepu16_epi32(M16);                // m
        M32          = _mm256_slli_epi32(M32, 16);                // m << 16

        __m256i E32  = _mm256_cvtepu8_epi32(E8);                  // e
        E32          = _mm256_slli_epi32(E32, 23);                // e << 23

        __m256i S32  = _mm256_cvtepu8_epi32(SB);                  // sign (0/1)
        S32          = _mm256_slli_epi32(S32, 31);                // sign << 31

        __m256i OUT  = _mm256_or_si256(S32, _mm256_or_si256(E32, M32));

        // store 8 results
        _mm256_storeu_si256((__m256i*)(flt32_buffer + off_f), OUT);

        // advance
        off_f += 8;
        off_g += 10;

        // prefetch next
        _mm_prefetch((const char*)(gbf80_buffer + off_g), _MM_HINT_T0);
        _mm_prefetch((const char*)(flt32_buffer + off_f), _MM_HINT_T0);
    }


    // tail (0..7)
    if (off_f < length) {
        uint32_t tmp[8] = {0};
        // WARNING: we RELY on the invalid channels having
        //          all zeros when coming from the chip!
        gbf_decode(gbf80_buffer + off_g, tmp, 8);
        for (unsigned r = 0; off_f + r < length; ++r) {
            flt32_buffer[off_f + r] = tmp[r];
        }
    }

#else
    // RISC-V, and other architectures

    size_t off_f = 0, off_g = 0;

    while (off_f + 8 <= length) {
        const uint8_t* in = gbf80_buffer + off_g;

        uint64_t lo = 0;
        uint16_t hi = 0;
        memcpy(&lo, in, 8);
        memcpy(&hi, in + 8, 2);

        uint8_t exp = (uint8_t)(hi >> 8);

        // signs
        uint32_t out0 = (uint32_t)(((lo >>  8) & 1u) << 31);
        uint32_t out1 = (uint32_t)(((lo >> 17) & 1u) << 31);
        uint32_t out2 = (uint32_t)(((lo >> 26) & 1u) << 31);
        uint32_t out3 = (uint32_t)(((lo >> 35) & 1u) << 31);
        uint32_t out4 = (uint32_t)(((lo >> 44) & 1u) << 31);
        uint32_t out5 = (uint32_t)(((lo >> 53) & 1u) << 31);
        uint32_t out6 = (uint32_t)(((lo >> 62) & 1u) << 31);
        uint32_t out7 = (uint32_t)(((hi >>  7) & 1u) << 31);

        // mantissas (8b 1.m)
        uint8_t t0 = (uint8_t)((lo >>  0) & 0xFF);
        uint8_t t1 = (uint8_t)((lo >>  9) & 0xFF);
        uint8_t t2 = (uint8_t)((lo >> 18) & 0xFF);
        uint8_t t3 = (uint8_t)((lo >> 27) & 0xFF);
        uint8_t t4 = (uint8_t)((lo >> 36) & 0xFF);
        uint8_t t5 = (uint8_t)((lo >> 45) & 0xFF);
        uint8_t t6 = (uint8_t)((lo >> 54) & 0xFF);
        uint8_t t7 = (uint8_t)((((hi & 0x007F) << 1) | ((lo >> 63) & 1u)) & 0xFF);

        // d = leading zeros in 8 bits (t==0 -> 8)
        uint8_t d0 = lzcnt8(t0);
        uint8_t d1 = lzcnt8(t1);
        uint8_t d2 = lzcnt8(t2);
        uint8_t d3 = lzcnt8(t3);
        uint8_t d4 = lzcnt8(t4);
        uint8_t d5 = lzcnt8(t5);
        uint8_t d6 = lzcnt8(t6);
        uint8_t d7 = lzcnt8(t7);

        // e = sat_sub(exp, d) ; if d==8 (t==0) => exp becomes 0
        uint32_t e0 = saturated_subtract8(exp, d0) & -(uint32_t)(d0 < 8);
        uint32_t e1 = saturated_subtract8(exp, d1) & -(uint32_t)(d1 < 8);
        uint32_t e2 = saturated_subtract8(exp, d2) & -(uint32_t)(d2 < 8);
        uint32_t e3 = saturated_subtract8(exp, d3) & -(uint32_t)(d3 < 8);
        uint32_t e4 = saturated_subtract8(exp, d4) & -(uint32_t)(d4 < 8);
        uint32_t e5 = saturated_subtract8(exp, d5) & -(uint32_t)(d5 < 8);
        uint32_t e6 = saturated_subtract8(exp, d6) & -(uint32_t)(d6 < 8);
        uint32_t e7 = saturated_subtract8(exp, d7) & -(uint32_t)(d7 < 8);

        // shift/clear hidden 1
        uint32_t m0 = ((uint32_t)(t0 << d0)) & 0x7F;
        uint32_t m1 = ((uint32_t)(t1 << d1)) & 0x7F;
        uint32_t m2 = ((uint32_t)(t2 << d2)) & 0x7F;
        uint32_t m3 = ((uint32_t)(t3 << d3)) & 0x7F;
        uint32_t m4 = ((uint32_t)(t4 << d4)) & 0x7F;
        uint32_t m5 = ((uint32_t)(t5 << d5)) & 0x7F;
        uint32_t m6 = ((uint32_t)(t6 << d6)) & 0x7F;
        uint32_t m7 = ((uint32_t)(t7 << d7)) & 0x7F;

        // set the final float vals
        flt32_buffer[off_f + 0] = out0 | (e0 << 23) | (m0 << 16);
        flt32_buffer[off_f + 1] = out1 | (e1 << 23) | (m1 << 16);
        flt32_buffer[off_f + 2] = out2 | (e2 << 23) | (m2 << 16);
        flt32_buffer[off_f + 3] = out3 | (e3 << 23) | (m3 << 16);
        flt32_buffer[off_f + 4] = out4 | (e4 << 23) | (m4 << 16);
        flt32_buffer[off_f + 5] = out5 | (e5 << 23) | (m5 << 16);
        flt32_buffer[off_f + 6] = out6 | (e6 << 23) | (m6 << 16);
        flt32_buffer[off_f + 7] = out7 | (e7 << 23) | (m7 << 16);

        // bump the offsets
        off_f += 8;
        off_g += 10;

        // trigger prefetch
        prefetch_ptr((const char*)(gbf80_buffer + off_g));
        prefetch_ptr((const char*)(flt32_buffer + off_f));
    }

    // tail cases for num elems %8!=0
    if (off_f < length) {
        uint32_t tmp[8] = {0};
        // WARNING: we RELY on the invalid channels having
        //          all zeros when coming from the chip!
        gbf_decode(gbf80_buffer + off_g, tmp, 8);
        for (unsigned r = 0; off_f + r < length; ++r) {
            flt32_buffer[off_f + r] = tmp[r];
        }
    }

#endif // USE_X86_OPT
#endif // USE_ARM64_OPT

}

inline void bf16_encode(uint32_t* src, uint8_t* dst, int tensor_size)
{

    const unsigned int n = tensor_size;
    uint16_t* dst16 = (uint16_t*)dst;

#ifdef USE_X86_OPT

    const __m256i add = _mm256_set1_epi32(0x00008000u);
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m256i a0 = _mm256_loadu_si256((const __m256i*)(src + i));
        __m256i a1 = _mm256_loadu_si256((const __m256i*)(src + i + 8));
        a0 = _mm256_add_epi32(a0, add);
        a1 = _mm256_add_epi32(a1, add);
        a0 = _mm256_srli_epi32(a0, 16);
        a1 = _mm256_srli_epi32(a1, 16);
        // pack 2x 8 i32 -> 16 u16
        __m256i packed = _mm256_packus_epi32(a0, a1);
        packed = _mm256_permute4x64_epi64(packed, 0xD8);
        _mm256_storeu_si256((__m256i*)(dst16 + i), packed);
    }
    // scalar tail (<=15)
    for (; i < n; ++i) {
        uint32_t v = src[i] + 0x00008000u;
        dst16[i]   = (uint16_t)(v >> 16);
    }
    return;

#else
#ifdef USE_ARM64_OPT

    const uint32x4_t add = vdupq_n_u32(0x00008000u);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint32x4_t a0 = vld1q_u32(src + i);
        uint32x4_t a1 = vld1q_u32(src + i + 4);
        a0 = vaddq_u32(a0, add);
        a1 = vaddq_u32(a1, add);
        a0 = vshrq_n_u32(a0, 16);
        a1 = vshrq_n_u32(a1, 16);
        uint16x4_t n0 = vmovn_u32(a0);
        uint16x4_t n1 = vmovn_u32(a1);
        uint16x8_t p  = vcombine_u16(n0, n1);
        vst1q_u16(dst16 + i, p);
    }
    for (; i < n; ++i) {
        uint32_t v = src[i] + 0x00008000u;
        dst16[i]   = (uint16_t)(v >> 16);
    }
    return;

#else

    // fallback: see if OpenMP-SIMD can work any magic
    #pragma omp simd
    for (int i = 0; i < tensor_size; ++i) {
        uint32_t v = src[i] + 0x00008000u;
        dst16[i] = (uint16_t)(v >> 16);
    }
    return;

#endif
#endif

}

inline void bf16_decode(uint8_t* __restrict src, uint32_t* __restrict dst, int tensor_size)
{

    uint16_t const* __restrict src16 = (const uint16_t*)src;

    #pragma omp simd
    for (int i = 0; i < tensor_size; ++i) {
        dst[i] = (uint32_t)(src16[i]) << 16;
    }
}


} // Types
} // MX


#endif // MX_GBF_H
