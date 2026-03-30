// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_SHA512_H
#define MX_SHA512_H

#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <type_traits>

#include "uint128.h"

namespace MX
{
namespace sha512
{

// Detect GCC/Clang rotate-right intrinsic for 64-bit, fallback to manual shift
#if defined(__has_builtin)
    #if __has_builtin(__builtin_rotateright64)
        #define SHA512_ROTR(x,n) __builtin_rotateright64((x),(n))
    #else
        #define SHA512_ROTR(x,n) (((x) >> (n)) | ((x) << (64 - (n))))
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define SHA512_ROTR(x,n) __builtin_rotateright64((x),(n))
#else
    #define SHA512_ROTR(x,n) (((x) >> (n)) | ((x) << (64 - (n))))
#endif

typedef std::array<uint8_t, 64> hash_t;

// SHA-512 constants (first 80 bits of the fractional parts of the cube roots of the first 80 primes)
static constexpr std::array<uint64_t, 80> K = {{
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
        0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
        0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
        0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
        0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
        0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
        0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
        0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
        0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
        0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
        0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
        0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
        0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
        0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
    }
};

// SHA-512 functions
inline uint64_t Ch(uint64_t x, uint64_t y, uint64_t z) noexcept
{
    return (x & y) ^ (~x & z);
}
inline uint64_t Maj(uint64_t x, uint64_t y, uint64_t z) noexcept
{
    return (x & y) ^ (x & z) ^ (y & z);
}
inline uint64_t bsig0(uint64_t x) noexcept
{
    return SHA512_ROTR(x, 28) ^ SHA512_ROTR(x, 34) ^ SHA512_ROTR(x, 39);
}
inline uint64_t bsig1(uint64_t x) noexcept
{
    return SHA512_ROTR(x, 14) ^ SHA512_ROTR(x, 18) ^ SHA512_ROTR(x, 41);
}
inline uint64_t lsig0(uint64_t x) noexcept
{
    return SHA512_ROTR(x, 1)  ^ SHA512_ROTR(x, 8)  ^ (x >> 7);
}
inline uint64_t lsig1(uint64_t x) noexcept
{
    return SHA512_ROTR(x, 19) ^ SHA512_ROTR(x, 61) ^ (x >> 6);
}

// Process one 1024-bit block
inline void transform_block(const uint8_t block[128], std::array<uint64_t, 8> &S) noexcept
{
    uint64_t W[80];
    // prepare message schedule
    #pragma omp simd
    for(int t = 0; t < 16; ++t) {
        W[t] = (uint64_t(block[t * 8    ]) << 56)
               | (uint64_t(block[t * 8 + 1]) << 48)
               | (uint64_t(block[t * 8 + 2]) << 40)
               | (uint64_t(block[t * 8 + 3]) << 32)
               | (uint64_t(block[t * 8 + 4]) << 24)
               | (uint64_t(block[t * 8 + 5]) << 16)
               | (uint64_t(block[t * 8 + 6]) <<  8)
               | (uint64_t(block[t * 8 + 7])      );
    }

    for(int t = 16; t < 80; ++t) {
        W[t] = lsig1(W[t - 2]) + W[t - 7] + lsig0(W[t - 15]) + W[t - 16];
    }

    // working vars
    uint64_t a = S[0], b = S[1], c = S[2], d = S[3];
    uint64_t e = S[4], f = S[5], g = S[6], h = S[7];

    // 80 rounds
    for(int t = 0; t < 80; ++t) {
        uint64_t T1 = h + bsig1(e) + Ch(e, f, g) + K[t] + W[t];
        uint64_t T2 = bsig0(a) + Maj(a, b, c);
        h = g;  g = f;  f = e;
        e = d + T1;
        d = c;  c = b;  b = a;
        a = T1 + T2;
    }

    // update state
    S[0] += a; S[1] += b; S[2] += c; S[3] += d;
    S[4] += e; S[5] += f; S[6] += g; S[7] += h;
}

// One-shot SHA-512
inline hash_t compute_hash(const uint8_t* data, size_t len)
{
    // initial hash values (first 64 bits of the fractional parts of the square roots of the first 8 primes)
    std::array<uint64_t, 8> S = {{
            0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
            0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
            0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
            0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
        }
    };

    // pad message
    uint128_t bit_len = uint128_t(len) * 8;
    std::vector<uint8_t> M(data, data + len);
    M.push_back(0x80);
    while ((M.size() % 128) != 112) { M.push_back(0x00); }
    // append 128-bit big-endian length
    for(int i = 15; i >= 0; --i) {
        M.push_back(uint8_t((bit_len >> (i * 8)) & 0xFF));
    }

    // process blocks
    for(size_t off = 0; off < M.size(); off += 128) {
        transform_block(&M[off], S);
    }

    // output digest
    hash_t digest;
    for(int i = 0; i < 8; ++i) {
        #pragma omp simd
        for(int b = 0; b < 8; ++b) {
            digest[i * 8 + b] = uint8_t((S[i] >> ((7 - b) * 8)) & 0xFF);
        }
    }
    return digest;
}

// Overload for contiguous containers
template<typename C>
inline auto compute(const C &buf)
-> std::enable_if_t<std::is_convertible<typename C::value_type, uint8_t>::value,
std::array<uint8_t, 64>>
{
    return compute_hash(
               reinterpret_cast<const uint8_t*>(buf.data()),
               buf.size()
           );
}
inline hash_t compute(const uint8_t* data, size_t len)
{
    return compute_hash(data, len);
}

// --- Base64 encoding (unchanged) ---
inline std::string to_base64(const hash_t data)
{
    static constexpr char B64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    std::string out;
    size_t len = data.size();
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for(; i + 2 < len; i += 3) {
        uint32_t v = (uint32_t(data[i]) << 16)
                     | (uint32_t(data[i + 1]) << 8)
                     |  uint32_t(data[i + 2]);
        out.push_back(B64[(v >> 18) & 0x3F]);
        out.push_back(B64[(v >> 12) & 0x3F]);
        out.push_back(B64[(v >>  6) & 0x3F]);
        out.push_back(B64[ v        & 0x3F]);
    }
    if(i < len) {
        uint32_t v = uint32_t(data[i]) << 16;
        out.push_back(B64[(v >> 18) & 0x3F]);
        if(i + 1 < len) {
            v |= uint32_t(data[i + 1]) << 8;
            out.push_back(B64[(v >> 12) & 0x3F]);
            out.push_back(B64[(v >>  6) & 0x3F]);
            out.push_back('=');
        }
        else {
            out.push_back(B64[(v >> 12) & 0x3F]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

} // namespace sha512
} // namespace MX

#endif
