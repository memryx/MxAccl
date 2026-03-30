// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MXPACK_H
#define MXPACK_H

#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>


// typedefs
//----------------------------------------------------------------

#define MXPACK_DICT   0x01
#define MXPACK_LIST   0x02
#define MXPACK_NUMPY  0xA2
#define MXPACK_BIN    0x03
#define MXPACK_UINT8  0x10
#define MXPACK_INT8   0x11
#define MXPACK_UINT16 0x20
#define MXPACK_INT16  0x21
#define MXPACK_UINT32 0x30
#define MXPACK_INT32  0x31
#define MXPACK_UINT64 0x40
#define MXPACK_INT64  0x41
#define MXPACK_FP32   0x50
#define MXPACK_ASCII  0x60
#define MXPACK_UTF8   0x61
#define MXPACK_BOOL   0x70


typedef struct {
    char    key[64];
    uint8_t dtype;
    void*    value;
} mxpack_dict_entry_t;


typedef struct {
    uint32_t             num_keys;
    mxpack_dict_entry_t*  data;
} mxpack_dict_t;


typedef struct {
    uint8_t        dtype;
    uint32_t       num_elem;
    void*           data;
} mxpack_list_t;


typedef struct {
    uint64_t     length;
    uint8_t*      data;
} mxpack_binary_t;  // UTF8 uses this too


typedef struct {
    uint32_t  length;
    char*      s;
} mxpack_ascii_t;



// functions
//----------------------------------------------------------------


// gets the dictionary entry at key k
// returns NULL if not found
void* mxpack_get_keyval(mxpack_dict_t* d, const char* k);
void* mxpack_get_list_item_ptr(mxpack_list_t* l, uint32_t idx);

// decode a single key/value pair
// returns the number of bytes we moved up b, 0 is error
size_t mxpack_decode_dict_entry(mxpack_dict_entry_t* e, const uint8_t* b);

// parses the binary data array
// returns the number of bytes we moved up b, 0 is error
size_t mxpack_process_dict(mxpack_dict_t* d, const uint8_t* b);

// decode a list
// returns the number of bytes we moved up b, 0 is error
size_t mxpack_process_list(mxpack_list_t* l, const uint8_t* b);

// free() the given data container's contents AND the container itself
void mxpack_free_dict_entry(mxpack_dict_entry_t* e);
void mxpack_free_dict(mxpack_dict_t* d);
void mxpack_free_list(mxpack_list_t* l);

// save the data to a binary string
// returns the binary size on success, 0 on fail
uint64_t mxpack_save_dict(mxpack_dict_t* d, uint8_t* b);
uint64_t mxpack_save_list(mxpack_list_t* l, uint8_t* b);

// print all data
void mxpack_print_dict(const mxpack_dict_t* d, int indent_level);
void mxpack_print_list(const mxpack_list_t* l, int indent_level);

uint8_t* dfpv6_extract_hw_dfp_bytes(const uint8_t* b, uint64_t &length);

#endif
