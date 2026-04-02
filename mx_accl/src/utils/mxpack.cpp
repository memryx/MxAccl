// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <memx/accl/utils/mxpack.h>


void* mxpack_get_keyval(mxpack_dict_t* d, const char* k)
{

    void* val = NULL;

    // O(n) but that should be okay since
    // we don't expect toooo many keys...
    for(uint32_t i = 0; i < d->num_keys; i++) {
        if(strncmp(d->data[i].key, k, 64) == 0) {
            val = d->data[i].value;
            break;
        }
    }

    return val;
}


void* mxpack_get_list_item_ptr(mxpack_list_t* l, uint32_t idx)
{
    if(idx >= l->num_elem) { return NULL; }

    switch(l->dtype) {
        case MXPACK_DICT:
            return &(((mxpack_dict_t*)(l->data))[idx]);
            break;
        case MXPACK_NUMPY:
        case MXPACK_LIST:
            return &(((mxpack_list_t*)(l->data))[idx]);
            break;
        case MXPACK_UTF8:
        case MXPACK_BIN:
            return &(((mxpack_binary_t*)(l->data))[idx]);
            break;
        case MXPACK_ASCII:
            return &(((mxpack_ascii_t*)(l->data))[idx]);
            break;
        case MXPACK_BOOL:
            return &(((bool*)(l->data))[idx]);
            break;
        case MXPACK_UINT8:
            return &(((uint8_t*)(l->data))[idx]);
            break;
        case MXPACK_UINT16:
            return &(((uint16_t*)(l->data))[idx]);
            break;
        case MXPACK_UINT32:
            return &(((uint32_t*)(l->data))[idx]);
            break;
        case MXPACK_UINT64:
            return &(((uint64_t*)(l->data))[idx]);
            break;
        case MXPACK_INT8:
            return &(((int8_t*)(l->data))[idx]);
            break;
        case MXPACK_INT16:
            return &(((int16_t*)(l->data))[idx]);
            break;
        case MXPACK_INT32:
            return &(((int32_t*)(l->data))[idx]);
            break;
        case MXPACK_INT64:
            return &(((int64_t*)(l->data))[idx]);
            break;
        case MXPACK_FP32:
            return &(((float*)(l->data))[idx]);
            break;
        default:
            return NULL;
    }
}




size_t mxpack_process_list(mxpack_list_t* l, const uint8_t* b)
{

    size_t i = 0;

    // special case for empty lists
    if(l->num_elem == 0) {
        l->data = NULL;
        return 0;
    }

    // what we do next depends on the dtype
    switch(l->dtype) {
        case MXPACK_BOOL:
        case MXPACK_UINT8:
        case MXPACK_INT8:  // ignore signs cuz I'm lazy
            l->data = (uint8_t*) malloc(l->num_elem * sizeof(uint8_t));
            memcpy(l->data, b + i, l->num_elem * sizeof(uint8_t));
            i += l->num_elem * sizeof(uint8_t);
            break;
        case MXPACK_UINT16:
        case MXPACK_INT16:
            l->data = (uint16_t*) malloc(l->num_elem * sizeof(uint16_t));
            memcpy(l->data, b + i, l->num_elem * sizeof(uint16_t));
            i += l->num_elem * sizeof(uint16_t);
            break;
        case MXPACK_FP32:
        case MXPACK_UINT32:
        case MXPACK_INT32:
            l->data = (uint32_t*) malloc(l->num_elem * sizeof(uint32_t));
            memcpy(l->data, b + i, l->num_elem * sizeof(uint32_t));
            i += l->num_elem * sizeof(uint32_t);
            break;
        case MXPACK_UINT64:
        case MXPACK_INT64:
            l->data = (uint64_t*) malloc(l->num_elem * sizeof(uint64_t));
            memcpy(l->data, b + i, l->num_elem * sizeof(uint64_t));
            i += l->num_elem * sizeof(uint64_t);
            break;
        case MXPACK_BIN:
        case MXPACK_UTF8: {
            // we now have an !array! of mxpack_binary_t structs
            mxpack_binary_t* d = (mxpack_binary_t*) malloc(l->num_elem * sizeof(mxpack_binary_t));

            // each is processed at a time
            for(uint32_t n = 0; n < l->num_elem; n++) {
                memcpy(&(d[n].length), b + i, sizeof(uint64_t));
                i += sizeof(uint64_t);

                d[n].data = (uint8_t*) malloc(d[n].length);
                memcpy(d[n].data, b + i, d[n].length);
                i += (size_t) d[n].length;
            }
            l->data = d;
            break;
        }
        case MXPACK_ASCII: {
            // same deal as BIN/UTF8
            mxpack_ascii_t* d = (mxpack_ascii_t*) malloc(l->num_elem * sizeof(mxpack_ascii_t));

            for(uint32_t n = 0; n < l->num_elem; n++) {
                memcpy(&(d[n].length), b + i, sizeof(uint32_t));
                i += sizeof(uint32_t);

                d[n].s = (char*) malloc(d[n].length);
                memcpy(d[n].s, b + i, d[n].length);
                i += (size_t) d[n].length;
            }

            l->data = d;
            break;
        }
        case MXPACK_DICT: {
            // list of dicts{}
            mxpack_dict_t* d = (mxpack_dict_t*) malloc(l->num_elem * sizeof(mxpack_dict_t));

            for(uint32_t n = 0; n < l->num_elem; n++) {
                size_t amt = mxpack_process_dict(&(d[n]), b + i);
                if(amt == 0) {
                    printf("list of dict parsing FAIL\n");
                    return 0;
                }
                else {
                    i += amt;
                }
            }

            // finally set the data pointer
            l->data = d;
            break;
        }
        case MXPACK_NUMPY:
        case MXPACK_LIST: {
            // nested lists? hoohboy
            mxpack_list_t* d = (mxpack_list_t*) malloc(l->num_elem * sizeof(mxpack_list_t));

            for(uint32_t n = 0; n < l->num_elem; n++) {
                // list dtype
                memcpy(&(d[n].dtype), b + i, 1);
                i += 1;

                // list num_elem
                memcpy(&(d[n].num_elem), b + i, sizeof(uint32_t));
                i += sizeof(uint32_t);

                size_t amt = mxpack_process_list(&(d[n]), b + i);
                if(amt == 0) {
                    printf("nested list parsing FAIL\n");
                    return 0;
                }
                else {
                    i += amt;
                }
            }

            l->data = d;
            break;
        }
        default:
            printf("UNKNOWN DTYPE\n");
            return 0;
    }

    return i;
}


size_t mxpack_decode_dict_entry(mxpack_dict_entry_t* e, const uint8_t* b)
{

    size_t i = 0;
    memcpy(e->key, b + i, 64);
    i += 64;

    // force null-terminate
    e->key[63] = '\0';

    // value's dtype
    memcpy(&(e->dtype), b + i, 1);
    i += 1;

    switch(e->dtype) {
        case MXPACK_BOOL:
        case MXPACK_UINT8:
        case MXPACK_INT8:  // ignore signs cuz I'm lazy
            e->value = (uint8_t*) malloc(1 * sizeof(uint8_t));
            memcpy(e->value, b + i, 1 * sizeof(uint8_t));
            i += sizeof(uint8_t);
            break;
        case MXPACK_UINT16:
        case MXPACK_INT16:
            e->value = (uint16_t*) malloc(1 * sizeof(uint16_t));
            memcpy(e->value, b + i, 1 * sizeof(uint16_t));
            i += sizeof(uint16_t);
            break;
        case MXPACK_UINT32:
        case MXPACK_INT32:
        case MXPACK_FP32:
            e->value = (uint32_t*) malloc(1 * sizeof(uint32_t));
            memcpy(e->value, b + i, 1 * sizeof(uint32_t));
            i += sizeof(uint32_t);
            break;
        case MXPACK_UINT64:
        case MXPACK_INT64:
            e->value = (uint64_t*) malloc(1 * sizeof(uint64_t));
            memcpy(e->value, b + i, 1 * sizeof(uint64_t));
            i += sizeof(uint64_t);
            break;
        case MXPACK_BIN:
        case MXPACK_UTF8: {
            mxpack_binary_t* d = (mxpack_binary_t*) malloc(1 * sizeof(mxpack_binary_t));
            memcpy(&(d->length), b + i, 1 * sizeof(uint64_t));
            i += sizeof(uint64_t);
            d->data = (uint8_t*) malloc(d->length);
            memcpy(d->data, b + i, d->length);
            i += (size_t) d->length;
            e->value = d;
            break;
        }
        case MXPACK_ASCII: {
            mxpack_ascii_t* d = (mxpack_ascii_t*) malloc(1 * sizeof(mxpack_ascii_t));
            memcpy(&(d->length), b + i, 1 * sizeof(uint32_t));
            i += sizeof(uint32_t);
            d->s = (char*) malloc(d->length);
            memcpy(d->s, b + i, d->length);
            i += (size_t) d->length;
            e->value = d;
            break;
        }
        case MXPACK_DICT: {
            e->value = (mxpack_dict_t*) malloc(1 * sizeof(mxpack_dict_t));

            size_t amt = mxpack_process_dict((mxpack_dict_t*)e->value, b + i);
            if(amt == 0) {
                printf("nested mxpack_parse FAIL\n");
                return 0;
            }
            else {
                i += amt;
            }
            break;
        }
        case MXPACK_NUMPY:
        case MXPACK_LIST: {
            // create the list obj
            mxpack_list_t* d = (mxpack_list_t*) malloc(sizeof(mxpack_list_t));

            // get the list member dtype
            memcpy(&(d->dtype), b + i, 1);
            i += 1;

            // get the number of list elements
            memcpy(&(d->num_elem), b + i, sizeof(uint32_t));
            i += sizeof(uint32_t);

            // special case for empty lists
            if(d->num_elem == 0) {
                d->data = NULL;
                e->value = d;
                break;
            }
            else {
                // process the list
                size_t amt = mxpack_process_list(d, b + i);
                if(amt == 0) {
                    printf("dict's parsing of list FAIL\n");
                    return 0;
                }
                else {
                    i += amt;
                }
                e->value = d;
                break;
            }
        }
        default:
            printf("dict parsing got UNKNOWN DTYPE 0x%02X\n", e->dtype);
            return 0;
    }

    return i; // so we know how far to scoot
}


size_t mxpack_process_dict(mxpack_dict_t* d, const uint8_t* b)
{

    size_t i = 0;

    // get num_keys
    memcpy(&(d->num_keys), b + i, sizeof(uint32_t));
    i += sizeof(uint32_t);

    // alloc that many dict entries
    d->data = (mxpack_dict_entry_t*) malloc(d->num_keys * sizeof(mxpack_dict_entry_t));

    for(uint32_t n = 0; n < d->num_keys; n++) {

        size_t amt = mxpack_decode_dict_entry(&(d->data[n]), b + i);
        if(amt == 0) {
            printf("parse_dict failed on parse dict_entry FAIL\n");
            return 0;
        }
        else {
            i += amt;
        }

    }

    return i;
}


//------------------------------------------------------------------------------

void mxpack_free_dict_entry(mxpack_dict_entry_t* e)
{

    if(e == NULL) { return; }

    switch(e->dtype) {
        case MXPACK_BOOL:
        case MXPACK_UINT8:
        case MXPACK_UINT16:
        case MXPACK_UINT32:
        case MXPACK_UINT64:
        case MXPACK_INT8:
        case MXPACK_INT16:
        case MXPACK_INT32:
        case MXPACK_INT64:
        case MXPACK_FP32:
            free(e->value);
            e->value = NULL;
            break;
        case MXPACK_DICT:
            mxpack_free_dict((mxpack_dict_t*)e->value);
            free(e->value);
            e->value = NULL;
            break;
        case MXPACK_NUMPY:
        case MXPACK_LIST:
            mxpack_free_list((mxpack_list_t*)e->value);
            free(e->value);
            e->value = NULL;
            break;
        case MXPACK_BIN:
        case MXPACK_UTF8:
            free( ((mxpack_binary_t*)e->value)->data );
            ((mxpack_binary_t*)e->value)->data = NULL;
            free( e->value );
            e->value = NULL;
            break;
        case MXPACK_ASCII:
            free( ((mxpack_ascii_t*)e->value)->s );
            ((mxpack_ascii_t*)e->value)->s = NULL;
            free( e->value );
            e->value = NULL;
            break;
        default:
            printf("mxpack_free_dict_entry: UNKNOWN DTYPE\n");
    }

}


void mxpack_free_dict(mxpack_dict_t* d)
{

    if(d == NULL) { return; }

    for(uint32_t i = 0; i < d->num_keys; i++) {
        mxpack_free_dict_entry( &(d->data[i]) );
    }
    free(d->data);
    d->data = NULL;
}


void mxpack_free_list(mxpack_list_t* l)
{

    if(l == NULL) { return; }

    // empty list
    if(l->data == NULL) { return; }

    switch(l->dtype) {
        case MXPACK_BOOL:
        case MXPACK_UINT8:
        case MXPACK_UINT16:
        case MXPACK_UINT32:
        case MXPACK_UINT64:
        case MXPACK_INT8:
        case MXPACK_INT16:
        case MXPACK_INT32:
        case MXPACK_INT64:
        case MXPACK_FP32:
            free(l->data);
            l->data = NULL;
            break;
        case MXPACK_DICT:
            for(uint32_t i = 0; i < l->num_elem; i++) {
                mxpack_free_dict( &(((mxpack_dict_t*)l->data)[i]) );
            }
            free(l->data);
            l->data = NULL;
            break;
        case MXPACK_NUMPY:
        case MXPACK_LIST:
            for(uint32_t i = 0; i < l->num_elem; i++) {
                mxpack_free_list( &(((mxpack_list_t*)l->data)[i]) );
            }
            free(l->data);
            l->data = NULL;
            break;
        case MXPACK_BIN:
        case MXPACK_UTF8:
            for(uint32_t i = 0; i < l->num_elem; i++) {
                free( (((mxpack_binary_t*)l->data)[i]).data );
                (((mxpack_binary_t*)l->data)[i]).data = NULL;
            }
            free(l->data);
            l->data = NULL;
            break;
        case MXPACK_ASCII:
            for(uint32_t i = 0; i < l->num_elem; i++) {
                free( (((mxpack_ascii_t*)l->data)[i]).s );
                (((mxpack_ascii_t*)l->data)[i]).s = NULL;
            }
            free(l->data);
            l->data = NULL;
            break;
        default:
            printf("mxpack_free_list: UNKNOWN DTYPE\n");
    }
}



//------------------------------------------------------------------------------


void inprintf(int num, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    for(int i = 0; i < num; i++) { printf(" "); }
    vprintf(format, args);
    fflush(stdout);
    va_end(args);
}

const char* mxpack_type2str(uint8_t t)
{
    switch(t) {
        case MXPACK_BOOL:
            return "bool";
        case MXPACK_UINT8:
            return "uint8";
        case MXPACK_UINT16:
            return "uint16";
        case MXPACK_UINT32:
            return "uint32";
        case MXPACK_UINT64:
            return "uint64";
        case MXPACK_INT8:
            return "int8";
        case MXPACK_INT16:
            return "int16";
        case MXPACK_INT32:
            return "int32";
        case MXPACK_INT64:
            return "int64";
        case MXPACK_FP32:
            return "float";
        case MXPACK_ASCII:
            return "string";
        case MXPACK_UTF8:
            return "UTF-8";
        case MXPACK_BIN:
            return "bytes";
        case MXPACK_LIST:
            return "list";
        case MXPACK_NUMPY:
            return "ndarray";
        case MXPACK_DICT:
            return "dict";
        default:
            return "!UNKNOWN!";
    }
}


void mxpack_print_dict(const mxpack_dict_t* d, int il)
{

    inprintf(il, "{\n");
    il++;

    for(uint32_t i = 0; i < d->num_keys; i++) {

        switch(d->data[i].dtype) {

            case MXPACK_BOOL:
                inprintf(il, "\"%s\" (bool): %s\n", d->data[i].key, (*((uint8_t*) d->data[i].value) == 0) ? "false" : "true" );
                break;
            case MXPACK_UINT8:
                inprintf(il, "\"%s\" (uint8): %u\n", d->data[i].key, *((uint8_t*) d->data[i].value));
                break;
            case MXPACK_UINT16:
                inprintf(il, "\"%s\" (uint16): %u\n", d->data[i].key, *((uint16_t*) d->data[i].value));
                break;
            case MXPACK_UINT32:
                inprintf(il, "\"%s\" (uint32): %u\n", d->data[i].key, *((uint32_t*) d->data[i].value));
                break;
            case MXPACK_UINT64:
                inprintf(il, "\"%s\" (uint64): %lu\n", d->data[i].key, *((uint64_t*) d->data[i].value));
                break;
            case MXPACK_INT8:
                inprintf(il, "\"%s\" (int8): %d\n", d->data[i].key, *((int8_t*) d->data[i].value));
                break;
            case MXPACK_INT16:
                inprintf(il, "\"%s\" (int16): %d\n", d->data[i].key, *((int16_t*) d->data[i].value));
                break;
            case MXPACK_INT32:
                inprintf(il, "\"%s\" (int32): %d\n", d->data[i].key, *((int32_t*) d->data[i].value));
                break;
            case MXPACK_INT64:
                inprintf(il, "\"%s\" (int64): %ld\n", d->data[i].key, *((int64_t*) d->data[i].value));
                break;
            case MXPACK_FP32:
                inprintf(il, "\"%s\" (float): %g\n", d->data[i].key, *((float*) d->data[i].value));
                break;
            case MXPACK_ASCII:
                inprintf(il, "\"%s\" (string): \"%s\"\n", d->data[i].key, ((mxpack_ascii_t*)(d->data[i].value))->s);
                break;
            case MXPACK_BIN:
                inprintf(il, "\"%s\" (bytes): < %lu Bytes >\n", d->data[i].key, ((mxpack_binary_t*)(d->data[i].value))->length);
                break;
            case MXPACK_UTF8:
                inprintf(il, "\"%s\" (UTF-8): < %lu Byte long UTF-8 string >\n", d->data[i].key, ((mxpack_binary_t*)(d->data[i].value))->length);
                break;
            case MXPACK_DICT:
                inprintf(il, "\"%s\" (dict):\n", d->data[i].key);
                mxpack_print_dict((mxpack_dict_t*) d->data[i].value, il + 2);
                break;
            case MXPACK_LIST:
                inprintf(il, "\"%s\" (list of %s):\n", d->data[i].key, mxpack_type2str(((mxpack_list_t*)(d->data[i].value))->dtype));
                mxpack_print_list((mxpack_list_t*) d->data[i].value, il + 2);
                break;
            case MXPACK_NUMPY:
                inprintf(il, "\"%s\" (ndarray of %s):\n", d->data[i].key, mxpack_type2str(((mxpack_list_t*)(d->data[i].value))->dtype));
                mxpack_print_list((mxpack_list_t*) d->data[i].value, il + 2);
                break;
            default:
                inprintf(il, "\"%s\": ERROR! UNKNOWN DTYPE!!\n", d->data[i].key);
        }

    }

    il--;
    inprintf(il, "}\n");

}



void mxpack_print_list(const mxpack_list_t* l, int il)
{

    inprintf(il, "[\n");
    il++;

    switch(l->dtype) {
        case MXPACK_BOOL:
        case MXPACK_UINT8:
        case MXPACK_UINT16:
        case MXPACK_UINT32:
        case MXPACK_UINT64:
        case MXPACK_INT8:
        case MXPACK_INT16:
        case MXPACK_INT32:
        case MXPACK_INT64:
        case MXPACK_FP32:
            // values we will print in one long line
            inprintf(il, "");
            break;
        default:
            // the others will get their
            // own newline at each element
            break;
    }

    if(l->num_elem == 0) {
        il--;
        inprintf(il, "]\n");
        return;
    }

    for(uint32_t i = 0; i < (l->num_elem - 1); i++) {
        switch(l->dtype) {
            case MXPACK_BOOL:
                printf("%s, ", (((uint8_t*) l->data)[i] == 0) ? "false" : "true" );
                break;
            case MXPACK_UINT8:
                printf("%u, ", ((uint8_t*) l->data)[i]);
                break;
            case MXPACK_UINT16:
                printf("%u, ", ((uint16_t*) l->data)[i]);
                break;
            case MXPACK_UINT32:
                printf("%u, ", ((uint32_t*) l->data)[i]);
                break;
            case MXPACK_UINT64:
                printf("%lu, ", ((uint64_t*) l->data)[i]);
                break;
            case MXPACK_INT8:
                printf("%d, ", ((int8_t*) l->data)[i]);
                break;
            case MXPACK_INT16:
                printf("%d, ", ((int16_t*) l->data)[i]);
                break;
            case MXPACK_INT32:
                printf("%d, ", ((int32_t*) l->data)[i]);
                break;
            case MXPACK_INT64:
                printf("%ld, ", ((int64_t*) l->data)[i]);
                break;
            case MXPACK_FP32:
                printf("%g, ", ((float*) l->data)[i]);
                break;
            case MXPACK_ASCII:
                inprintf(il, "\"%s\",\n", ((mxpack_ascii_t*)(l->data))[i].s);
                break;
            case MXPACK_BIN:
                inprintf(il, "< %lu Bytes >,\n", ((mxpack_binary_t*)(l->data))[i].length);
                break;
            case MXPACK_UTF8:
                inprintf(il, "< %lu Byte long UTF-8 string >,\n", ((mxpack_binary_t*)(l->data))[i].length);
                break;
            case MXPACK_DICT:
                // yeah yeah, we miss a "," this way... deal with it
                mxpack_print_dict( &(((mxpack_dict_t*)(l->data))[i]), il + 2 );
                break;
            case MXPACK_NUMPY:
            case MXPACK_LIST:
                mxpack_print_list( &(((mxpack_list_t*)(l->data))[i]), il + 2 );
                break;
            default:
                inprintf(il, "!INVALID! ");
                break;
        }
    }

    // final element is ~special~ and doesn't get a comma
    int i = l->num_elem - 1;
    switch(l->dtype) {
        case MXPACK_BOOL:
            printf("%s\n", (((uint8_t*) l->data)[i] == 0) ? "false" : "true" );
            break;
        case MXPACK_UINT8:
            printf("%u\n", ((uint8_t*) l->data)[i]);
            break;
        case MXPACK_UINT16:
            printf("%u\n", ((uint16_t*) l->data)[i]);
            break;
        case MXPACK_UINT32:
            printf("%u\n", ((uint32_t*) l->data)[i]);
            break;
        case MXPACK_UINT64:
            printf("%lu\n", ((uint64_t*) l->data)[i]);
            break;
        case MXPACK_INT8:
            printf("%d\n", ((int8_t*) l->data)[i]);
            break;
        case MXPACK_INT16:
            printf("%d\n", ((int16_t*) l->data)[i]);
            break;
        case MXPACK_INT32:
            printf("%d\n", ((int32_t*) l->data)[i]);
            break;
        case MXPACK_INT64:
            printf("%ld\n", ((int64_t*) l->data)[i]);
            break;
        case MXPACK_FP32:
            printf("%g\n", ((float*) l->data)[i]);
            break;
        case MXPACK_ASCII:
            inprintf(il, "\"%s\"\n", ((mxpack_ascii_t*)(l->data))[i].s);
            break;
        case MXPACK_BIN:
            inprintf(il, "< %lu Bytes >\n", ((mxpack_binary_t*)(l->data))[i].length);
            break;
        case MXPACK_UTF8:
            inprintf(il, "< %lu Byte long UTF-8 string >\n", ((mxpack_binary_t*)(l->data))[i].length);
            break;
        case MXPACK_DICT:
            // yeah yeah, we miss a "," this way... deal with it
            mxpack_print_dict( &(((mxpack_dict_t*)(l->data))[i]), il + 2 );
            break;
        case MXPACK_NUMPY:
        case MXPACK_LIST:
            mxpack_print_list( &(((mxpack_list_t*)(l->data))[i]), il + 2 );
            break;
        default:
            inprintf(il, "!INVALID!\n");
            break;
    }


    il--;
    inprintf(il, "]\n");
}

uint8_t* dfpv6_extract_hw_dfp_bytes(const uint8_t* b, uint64_t &length)
{

    if(b == NULL) { return 0; }

    // make sure it's an mxpack dict
    if(b[0] != 0x01) { return 0; }

    mxpack_dict_t d;
    size_t num_pbytes = 0;
    num_pbytes = mxpack_process_dict(&d, b + 1);
    if(num_pbytes == 0) { return 0; }

    // extract hw dfp
    mxpack_binary_t* sdfp = (mxpack_binary_t*) mxpack_get_keyval(&d, "hw_dfp");
    if(sdfp == NULL) {
        mxpack_free_dict(&d);
        return 0;
    }

    // copy it so we can free the mxpack dict latewr
    uint8_t* hwdfp = (uint8_t*) malloc(sdfp->length);
    memcpy(hwdfp, sdfp->data, sdfp->length);
    length = sdfp->length;
    // free up and return ptr
    mxpack_free_dict(&d);

    return hwdfp;
}









