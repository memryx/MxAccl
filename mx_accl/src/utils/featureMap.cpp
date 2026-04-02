// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/gbf.h>

#include <cstring>
#include <iostream>
#include <fstream>
#include <unordered_set>

using namespace MX::Types;
using namespace MX::Utils;

FeatureMap::FeatureMap(size_t size, MX_data_format format, uint16_t dim_h, uint16_t dim_w, uint16_t dim_z, uint32_t num_chan,
                       int fmap_convert_threads, bool use_model_shape, Dfp::PortInfo* port_info)
{

    // HPOC notes: 'size' includes hpoc_dim_c when hpoc is enabled, while num_chan is always the final dim_c size
    if(port_info != nullptr) {
        hpoc_en = port_info->hpoc_en;
        if(hpoc_en) {
            hpoc_dim_c = port_info->hpoc_dim_c;
            hpoc_list_length = port_info->hpoc_list_length;
            if(hpoc_list_length > 0) {
                hpoc_dummy_channels = new uint16_t[hpoc_list_length];
                std::memcpy(hpoc_dummy_channels, port_info->hpoc_dummy_channels, hpoc_list_length * sizeof(uint16_t));
            }
            else {
                hpoc_dummy_channels = nullptr;
            }
        }
        else {
            hpoc_dim_c = 0;
            hpoc_list_length = 0;
            hpoc_dummy_channels = nullptr;
        }
    }
    else {
        hpoc_en = false;
        hpoc_dim_c = 0;
        hpoc_list_length = 0;
        hpoc_dummy_channels = nullptr;
    }

    fm_type = FM_DFP; // default

    fmap_data = new uint32_t[size];
    fmap_data_internal = fmap_data;
    temp_float_buffer = new uint32_t[size];
    temp_float_buffer_internal = temp_float_buffer;
    featureMap_size = size;
    fmt = format;
    fmap_convert_threads_ = fmap_convert_threads;

    // defaults
    real_dim_c = 1;
    num_xyz_pixels = 1;
    num_gbf_per_pixel = 1;
    any_remainder_chs = false;
    gbf80_pixel_size = 1;
    gbf80_row_size = 1;
    flt32_row_size = 1;

    if(!(fmt == MX_FMT_GBF80 || fmt == MX_FMT_BF16 || fmt == MX_FMT_FP32 || fmt == MX_FMT_GBF80_ROW)) {
        throw runtime_error("featureMap was given an unknown format!");
    }

    // special case for pre/post FM_ fmaps
    if(dim_h == 0 && dim_w == 0 && dim_z == 0 && num_chan == 0 && port_info == nullptr && fmt == MX_FMT_FP32) {
        this->dim_h = 1;
        this->dim_w = 1;
        this->dim_z = 1;
        this->dim_c = size;
        fm_type = FM_PREPOST;
        use_model_shape_ = false;
        pinfo = nullptr;
        formatted_data = nullptr;
        calc_convert_size_and_new();
    }
    else {

        // no dims can be 0
        if(dim_h == 0 || dim_w == 0 || dim_z == 0 || num_chan == 0) {
            throw runtime_error("featureMap was given a dimension of 0");
        }

        use_model_shape_ = use_model_shape;
        if(use_model_shape_) {
            if(port_info == nullptr) {
                throw runtime_error("featureMap was given use_model_shape = true, but port_info is nullptr");
            }
            if(port_info->dim_h != dim_h || port_info->dim_w != dim_w || port_info->dim_z != dim_z || port_info->dim_c != num_chan) {
                throw runtime_error("featureMap was given use_model_shape = true, but port_info does not match the given dimensions");
            }

            pinfo = new Dfp::PortInfo();
            // only need to copy the batch, raw_shape/raw_dtype, and shape_shift_info members
            pinfo->batch = port_info->batch;
            pinfo->raw_shape = port_info->raw_shape;
            pinfo->raw_dtype = port_info->raw_dtype;
            pinfo->shape_shift_info = port_info->shape_shift_info;
            pinfo->permuted_indices = port_info->permuted_indices;

            // hpoc
            pinfo->hpoc_en = port_info->hpoc_en;
            if(pinfo->hpoc_en) {
                pinfo->hpoc_dim_c = port_info->hpoc_dim_c;
                pinfo->hpoc_list_length = port_info->hpoc_list_length;
                if(pinfo->hpoc_list_length > 0) {
                    pinfo->hpoc_dummy_channels = new uint16_t[pinfo->hpoc_list_length];
                    std::memcpy(pinfo->hpoc_dummy_channels, port_info->hpoc_dummy_channels, pinfo->hpoc_list_length * sizeof(uint16_t));
                }
                else {
                    pinfo->hpoc_dummy_channels = nullptr;
                }
            }
            else {
                pinfo->hpoc_dim_c = 0;
                pinfo->hpoc_list_length = 0;
                pinfo->hpoc_dummy_channels = nullptr;
            }

        }
        else {
            pinfo = nullptr;
        }

        this->dim_h = dim_h;
        this->dim_w = dim_w;
        this->dim_z = dim_z;
        dim_c = num_chan;
        formatted_data = nullptr;
        calc_convert_size_and_new();
    }

}

FeatureMap::FeatureMap(float* in_data, size_t size, MX_data_format format,  uint16_t dim_h, uint16_t dim_w, uint16_t dim_z,
                       uint32_t num_chan, int fmap_convert_threads, bool use_model_shape, Dfp::PortInfo* port_info)
{
    // HPOC notes: 'size' includes hpoc_dim_c when hpoc is enabled, while num_chan is always the final dim_c size
    if(port_info != nullptr) {
        hpoc_en = port_info->hpoc_en;
        if(hpoc_en) {
            hpoc_dim_c = port_info->hpoc_dim_c;
            hpoc_list_length = port_info->hpoc_list_length;
            if(hpoc_list_length > 0) {
                hpoc_dummy_channels = new uint16_t[hpoc_list_length];
                std::memcpy(hpoc_dummy_channels, port_info->hpoc_dummy_channels, hpoc_list_length * sizeof(uint16_t));
            }
            else {
                hpoc_dummy_channels = nullptr;
            }
        }
        else {
            hpoc_dim_c = 0;
            hpoc_list_length = 0;
            hpoc_dummy_channels = nullptr;
        }
    }
    else {
        hpoc_en = false;
        hpoc_dim_c = 0;
        hpoc_list_length = 0;
        hpoc_dummy_channels = nullptr;
    }

    fm_type = FM_DFP; // default

    fmap_data = new uint32_t[size];
    fmap_data_internal = fmap_data;
    temp_float_buffer = new uint32_t[size];
    temp_float_buffer_internal = temp_float_buffer;
    featureMap_size = size;
    std::memcpy(fmap_data, in_data, featureMap_size * sizeof(float));
    fmt = format;
    fmap_convert_threads_ = fmap_convert_threads;
    if(!(fmt == MX_FMT_GBF80 || fmt == MX_FMT_BF16 || fmt == MX_FMT_FP32 || fmt == MX_FMT_GBF80_ROW)) {
        throw runtime_error("featureMap was given an unknown format!");
    }

    // no dims can be 0
    if(dim_h == 0 || dim_w == 0 || dim_z == 0 || num_chan == 0) {
        throw runtime_error("featureMap was given a dimension of 0");
    }

    // defaults
    real_dim_c = 1;
    num_xyz_pixels = 1;
    num_gbf_per_pixel = 1;
    any_remainder_chs = false;
    gbf80_pixel_size = 1;
    gbf80_row_size = 1;
    flt32_row_size = 1;

    use_model_shape_ = use_model_shape;
    if(use_model_shape_) {
        if(port_info == nullptr) {
            throw runtime_error("featureMap was given use_model_shape = true, but port_info is nullptr");
        }
        if(port_info->dim_h != dim_h || port_info->dim_w != dim_w || port_info->dim_z != dim_z || port_info->dim_c != num_chan) {
            throw runtime_error("featureMap was given use_model_shape = true, but port_info does not match the given dimensions");
        }

        pinfo = new Dfp::PortInfo();
        // only need to copy the batch, raw_shape/raw_dtype, and shape_shift_info members
        pinfo->batch = port_info->batch;
        pinfo->raw_shape = port_info->raw_shape;
        pinfo->raw_dtype = port_info->raw_dtype;
        pinfo->shape_shift_info = port_info->shape_shift_info;
        pinfo->permuted_indices = port_info->permuted_indices;

        // hpoc
        pinfo->hpoc_en = port_info->hpoc_en;
        if(pinfo->hpoc_en) {
            pinfo->hpoc_dim_c = port_info->hpoc_dim_c;
            pinfo->hpoc_list_length = port_info->hpoc_list_length;
            if(pinfo->hpoc_list_length > 0) {
                pinfo->hpoc_dummy_channels = new uint16_t[pinfo->hpoc_list_length];
                std::memcpy(pinfo->hpoc_dummy_channels, port_info->hpoc_dummy_channels, pinfo->hpoc_list_length * sizeof(uint16_t));
            }
            else {
                pinfo->hpoc_dummy_channels = nullptr;
            }
        }
        else {
            pinfo->hpoc_dim_c = 0;
            pinfo->hpoc_list_length = 0;
            pinfo->hpoc_dummy_channels = nullptr;
        }

    }
    else {
        pinfo = nullptr;
    }

    this->dim_h = dim_h;
    this->dim_w = dim_w;
    this->dim_z = dim_z;
    this->dim_c = num_chan;
    formatted_data = nullptr;
    calc_convert_size_and_new();
    convert_data(fmap_data);
}

FeatureMap::FeatureMap(const FeatureMap &rhs)
{
    hpoc_en = rhs.hpoc_en;
    hpoc_dim_c = rhs.hpoc_dim_c;
    hpoc_list_length = rhs.hpoc_list_length;
    hpoc_dummy_channels = nullptr;
    if(rhs.hpoc_list_length > 0) {
        hpoc_dummy_channels = new uint16_t[rhs.hpoc_list_length];
        std::memcpy(hpoc_dummy_channels, rhs.hpoc_dummy_channels, rhs.hpoc_list_length * sizeof(uint16_t));
    }

    real_dim_c = rhs.real_dim_c;
    num_xyz_pixels = rhs.num_xyz_pixels;
    num_gbf_per_pixel = rhs.num_gbf_per_pixel;
    any_remainder_chs = rhs.any_remainder_chs;
    gbf80_pixel_size = rhs.gbf80_pixel_size;
    gbf80_row_size = rhs.gbf80_row_size;
    flt32_row_size = rhs.flt32_row_size;

    featureMap_size = rhs.featureMap_size;
    fmt = rhs.fmt;
    use_model_shape_ = rhs.use_model_shape_;
    fm_type = rhs.fm_type;
    if(use_model_shape_) {
        if(rhs.pinfo == nullptr) {
            throw runtime_error("featureMap was given use_model_shape = true, but rhs.pinfo is nullptr");
        }
        pinfo = new Dfp::PortInfo();
        pinfo->batch = rhs.pinfo->batch;
        pinfo->raw_shape = rhs.pinfo->raw_shape;
        pinfo->raw_dtype = rhs.pinfo->raw_dtype;
        pinfo->shape_shift_info = rhs.pinfo->shape_shift_info;
        pinfo->permuted_indices = rhs.pinfo->permuted_indices;

    }
    else {
        pinfo = nullptr;
    }
    dim_h = rhs.dim_h;
    dim_w = rhs.dim_w;
    dim_z = rhs.dim_z;
    dim_c = rhs.dim_c;
    fmap_convert_threads_ = rhs.fmap_convert_threads_;
    formatted_featuremap_size = rhs.formatted_featuremap_size;
    fmap_data = new uint32_t[featureMap_size];
    fmap_data_internal = fmap_data;
    temp_float_buffer = new uint32_t[featureMap_size];
    temp_float_buffer_internal = temp_float_buffer;
    std::memcpy(fmap_data, rhs.fmap_data, featureMap_size * sizeof(float));
    if(fmt == MX_FMT_RGB888 || fmt == MX_FMT_FP32) {
        formatted_data = (uint8_t*) fmap_data;
    }
    else {
        formatted_data = new uint8_t[formatted_featuremap_size];
        std::memcpy(formatted_data, rhs.formatted_data, formatted_featuremap_size * sizeof(uint8_t));
    }
}

FeatureMap &FeatureMap::operator=(const FeatureMap &rhs)
{
    if(this == &rhs) {
        return *this;
    }

    hpoc_en = rhs.hpoc_en;
    hpoc_dim_c = rhs.hpoc_dim_c;
    hpoc_list_length = rhs.hpoc_list_length;
    hpoc_dummy_channels = nullptr;
    if(rhs.hpoc_list_length > 0) {
        hpoc_dummy_channels = new uint16_t[rhs.hpoc_list_length];
        std::memcpy(hpoc_dummy_channels, rhs.hpoc_dummy_channels, rhs.hpoc_list_length * sizeof(uint16_t));
    }

    real_dim_c = rhs.real_dim_c;
    num_xyz_pixels = rhs.num_xyz_pixels;
    num_gbf_per_pixel = rhs.num_gbf_per_pixel;
    any_remainder_chs = rhs.any_remainder_chs;
    gbf80_pixel_size = rhs.gbf80_pixel_size;
    gbf80_row_size = rhs.gbf80_row_size;
    flt32_row_size = rhs.flt32_row_size;

    featureMap_size = rhs.featureMap_size;
    fmt = rhs.fmt;
    use_model_shape_ = rhs.use_model_shape_;
    fm_type = rhs.fm_type;
    if(use_model_shape_) {
        if(rhs.pinfo == nullptr) {
            throw runtime_error("featureMap was given use_model_shape = true, but rhs.pinfo is nullptr");
        }
        pinfo = new Dfp::PortInfo();
        pinfo->batch = rhs.pinfo->batch;
        pinfo->raw_shape = rhs.pinfo->raw_shape;
        pinfo->raw_dtype = rhs.pinfo->raw_dtype;
        pinfo->shape_shift_info = rhs.pinfo->shape_shift_info;
        pinfo->permuted_indices = rhs.pinfo->permuted_indices;
    }
    else {
        pinfo = nullptr;
    }
    this->dim_h = rhs.dim_h;
    this->dim_w = rhs.dim_w;
    this->dim_z = rhs.dim_z;
    dim_c = rhs.dim_c;
    fmap_convert_threads_ = rhs.fmap_convert_threads_;
    formatted_featuremap_size = rhs.formatted_featuremap_size;
    if(fmap_data_internal != nullptr) {
        if(formatted_data == (uint8_t*) fmap_data_internal) {
            formatted_data = nullptr;
        }
        delete[] fmap_data_internal;
        fmap_data = nullptr;
        fmap_data_internal = nullptr;
    }
    fmap_data = new uint32_t[featureMap_size];
    fmap_data_internal = fmap_data;
    if(temp_float_buffer_internal != nullptr) {
        delete[] temp_float_buffer_internal;
        temp_float_buffer = nullptr;
        temp_float_buffer_internal = nullptr;
    }
    temp_float_buffer = new uint32_t[featureMap_size];
    temp_float_buffer_internal = temp_float_buffer;
    std::memcpy(fmap_data, rhs.fmap_data, featureMap_size * sizeof(float));
    std::memcpy(temp_float_buffer, rhs.temp_float_buffer, featureMap_size * sizeof(float));
    if(formatted_data != nullptr && formatted_data != (uint8_t*) fmap_data_internal) {
        delete[] formatted_data;
        formatted_data = nullptr;
    }
    if(fmt == MX_FMT_RGB888 || fmt == MX_FMT_FP32) {
        formatted_data = (uint8_t*) fmap_data_internal;
    }
    else {
        formatted_data = new uint8_t[formatted_featuremap_size];
        std::memcpy(formatted_data, rhs.formatted_data, formatted_featuremap_size * sizeof(uint8_t));
    }

    // note: does NOT copy over ready/wait flags
    return *this;
}

void FeatureMap::calc_convert_size_and_new()
{

    // use hpoc_dim_c if hpoc is enabled, otherwise use dim_c
    real_dim_c = hpoc_en ? hpoc_dim_c : dim_c;

    switch(fmt) {
        case MX_FMT_RGB888:
            // don't actually do anything
            formatted_featuremap_size = featureMap_size;
            //formatted_data = (uint8_t*) fmap_data_internal;
            formatted_data = new uint8_t[formatted_featuremap_size];
            break;
        case MX_FMT_FP32:
            // plain old *4
            formatted_featuremap_size = featureMap_size * 4;
            // the cast from float to uint8 accounts for the *4 size
            //formatted_data = (uint8_t*) fmap_data_internal;
            formatted_data = new uint8_t[formatted_featuremap_size];
            break;
        case MX_FMT_BF16:
            // extra padding item for odd-sized fmaps
            formatted_featuremap_size = featureMap_size * 2;
            if ( featureMap_size % 2 ) {
                formatted_featuremap_size += 2;
            }
            // have to actually allocate this one
            formatted_data = new uint8_t[formatted_featuremap_size];
            break;
        case MX_FMT_GBF80: {
            // need to get fancy for this one...
            num_xyz_pixels = (featureMap_size / real_dim_c);
            any_remainder_chs = ((real_dim_c % 8) != 0);
            num_gbf_per_pixel = (real_dim_c / 8) + (any_remainder_chs ? 1 : 0);
            formatted_featuremap_size = num_xyz_pixels * num_gbf_per_pixel * 10;
            // for hpoc later
            gbf80_pixel_size = num_gbf_per_pixel * 10;
            gbf80_row_size = dim_w * dim_z * gbf80_pixel_size;
            flt32_row_size = dim_w  * dim_z * dim_c; // always dim_c (final shape), not real_dim_c from hardware
            // padding to 4 bytes-alignment
            formatted_featuremap_size = (formatted_featuremap_size + 3) & ~0x3L;
            // have to actually allocate this one
            formatted_data = new uint8_t[formatted_featuremap_size];
            break;
        }
        case MX_FMT_GBF80_ROW: {
            // need to get fancy for this one...
            any_remainder_chs = ((real_dim_c % 8) != 0);
            num_gbf_per_pixel = (real_dim_c / 8) + (any_remainder_chs ? 1 : 0);
            gbf80_pixel_size = num_gbf_per_pixel * 10;
            gbf80_row_size = dim_w * dim_z * gbf80_pixel_size;
            gbf80_row_size_rowpad = (gbf80_row_size + 3) & ~0x3;
            flt32_row_size = dim_w  * dim_z * dim_c;

            // padding to 4 bytes-alignment
            formatted_featuremap_size = dim_h * ((dim_w * dim_z * num_gbf_per_pixel * 10 + 3) & ~0x3L);

            // have to actually allocate this one
            formatted_data = new uint8_t[formatted_featuremap_size];
            break;
        }
        default:
            throw std::invalid_argument("Invalid featureMap data format");
            break;
    }
}

void FeatureMap::convert_data(void* vdata) const
{
    uint32_t* sdata = (uint32_t*) vdata;
    if (fmt == MX_FMT_BF16) {
        bf16_encode(sdata, formatted_data, featureMap_size);
    }
    else if (fmt == MX_FMT_GBF80) {
        #pragma omp for schedule(static)  // ignored if not parallel
        for(size_t i = 0; i < num_xyz_pixels; i++) {
            uint8_t* gbf_base = &(formatted_data[ i * (num_gbf_per_pixel * 10) ]);
            uint32_t*   flt_base = &(sdata[ i * dim_c ]);

            gbf_encode(flt_base, gbf_base, dim_c);
        }
    }
    else if (fmt == MX_FMT_GBF80_ROW) {
        size_t gbf80_row_offset = 0;
        size_t flt32_row_offset = 0;

        for (uint16_t height = 0; height < dim_h; height++) {
            size_t gbf80_pixel_offset = 0;
            size_t flt32_pixel_offset = 0;
            for (uint16_t width = 0; width < dim_w; width++) {
                for (uint16_t z = 0; z < dim_z; z++) {
                    uint32_t* flt32_buffer = (sdata + flt32_row_offset + flt32_pixel_offset);
                    uint8_t* gbf80_buffer = (uint8_t*)(formatted_data + gbf80_row_offset + gbf80_pixel_offset);
                    gbf_encode(flt32_buffer, gbf80_buffer, dim_c);

                    gbf80_pixel_offset += gbf80_pixel_size;
                    flt32_pixel_offset += dim_c;
                }
            }
            gbf80_row_offset += gbf80_row_size;
            flt32_row_offset += flt32_row_size;
        }
    }
    else {
        // for FP32, just copy the data as is
        std::memcpy(formatted_data, sdata, featureMap_size * sizeof(float));
    }
}

void FeatureMap::unconvert_data(void* vdata) const
{
    uint32_t* ddata = (uint32_t*) vdata;
    if (fmt == MX_FMT_BF16) {
        bf16_decode(formatted_data, ddata, featureMap_size);
    }
    else if (fmt == MX_FMT_GBF80) {

        if(hpoc_en) {
            uint32_t gbf80_row_offset = 0;
            uint32_t flt32_row_offset = 0;

            // loop each row
            for (uint32_t h_idx = 0; h_idx < dim_h; h_idx++) {
                uint32_t gbf80_pixel_offset = 0;
                uint32_t flt32_pixel_offset = 0;
                // visist all GBF pixel in each row
                for (uint32_t w_idx = 0; w_idx < dim_w; w_idx++) {
                    for (uint32_t z_idx = 0; z_idx < dim_z; z_idx++) {
                        uint32_t check_dummy_ch_idx = 0;
                        // decode for each buf of GBF pixel
                        for (uint32_t gbf_ch_idx = 0, gbf_buf_offset = 0, flt32_buf_offset = 0; gbf_ch_idx < real_dim_c; gbf_ch_idx += 8, gbf_buf_offset += 10) {
                            uint32_t decode_float_buf[8] = {0};
                            uint8_t* gbf80_buffer = (uint8_t*)(formatted_data + gbf80_row_offset + gbf80_pixel_offset + gbf_buf_offset);
                            gbf_decode(gbf80_buffer, decode_float_buf, 8);

                            for (uint32_t ch_offset = 0; ch_offset < 8; ++ch_offset) {
                                uint32_t curr_ch_idx = gbf_ch_idx + ch_offset;
                                // skip dummy channel
                                if ((check_dummy_ch_idx < real_dim_c) &&
                                        (curr_ch_idx == hpoc_dummy_channels[check_dummy_ch_idx])) {
                                    check_dummy_ch_idx++;
                                    continue;
                                }
                                else {
                                    // update target channel data of FP32 pixel
                                    if (gbf_ch_idx + ch_offset < real_dim_c) {
                                        uint32_t* flt32_buffer = (ddata + flt32_row_offset + flt32_pixel_offset + flt32_buf_offset);
                                        *flt32_buffer = decode_float_buf[ch_offset];
                                        flt32_buf_offset++;
                                    }
                                }
                            }/* ch_offset */
                        }/* gbf_ch_idx */
                        gbf80_pixel_offset += gbf80_pixel_size;
                        flt32_pixel_offset += dim_c;
                    }/* z */
                }/* w */
                gbf80_row_offset += gbf80_row_size;
                flt32_row_offset += flt32_row_size;
            }/* h */
        }
        else {
            #pragma omp for schedule(static)  // ignored if not parallel
            for(size_t i = 0; i < num_xyz_pixels; i++) {
                uint8_t*    gbf_base = &(formatted_data[ i * (num_gbf_per_pixel * 10) ]);
                uint32_t*   flt_base = (uint32_t*) & (ddata[ i * real_dim_c ]);

                gbf_decode(gbf_base, flt_base, real_dim_c);
            }
        }
    }
    else if (fmt == MX_FMT_GBF80_ROW) {
        size_t gbf80_row_offset = 0;
        size_t flt32_row_offset = 0;

        if(hpoc_en) {
            // loop each row
            for (uint32_t h_idx = 0; h_idx < dim_h; h_idx++) {
                uint32_t gbf80_pixel_offset = 0;
                uint32_t flt32_pixel_offset = 0;
                // visist all GBF pixel in each row
                for (uint32_t w_idx = 0; w_idx < dim_w; w_idx++) {
                    for (uint32_t z_idx = 0; z_idx < dim_z; z_idx++) {
                        uint32_t check_dummy_ch_idx = 0;
                        // decode for each buf of GBF pixel
                        for (uint32_t gbf_ch_idx = 0, gbf_buf_offset = 0, flt32_buf_offset = 0; gbf_ch_idx < real_dim_c; gbf_ch_idx += 8, gbf_buf_offset += 10) {
                            uint32_t decode_float_buf[8] = {0};
                            uint8_t* gbf80_buffer = (uint8_t*)(formatted_data + gbf80_row_offset + gbf80_pixel_offset + gbf_buf_offset);
                            gbf_decode(gbf80_buffer, decode_float_buf, 8);

                            for (uint32_t ch_offset = 0; ch_offset < 8; ++ch_offset) {
                                uint32_t curr_ch_idx = gbf_ch_idx + ch_offset;
                                // skip dummy channel
                                if ((check_dummy_ch_idx < real_dim_c) &&
                                        (curr_ch_idx == hpoc_dummy_channels[check_dummy_ch_idx])) {
                                    check_dummy_ch_idx++;
                                    continue;
                                }
                                else {
                                    // update target channel data of FP32 pixel
                                    if (gbf_ch_idx + ch_offset < real_dim_c) {
                                        uint32_t* flt32_buffer = (ddata + flt32_row_offset + flt32_pixel_offset + flt32_buf_offset);
                                        *flt32_buffer = decode_float_buf[ch_offset];
                                        flt32_buf_offset++;
                                    }
                                }
                            }/* ch_offset */
                        }/* gbf_ch_idx */
                        gbf80_pixel_offset += gbf80_pixel_size;
                        flt32_pixel_offset += dim_c;
                    }/* z */
                }/* w */
                gbf80_row_offset += gbf80_row_size_rowpad;
                flt32_row_offset += flt32_row_size;
            }/* h */

        }
        else {
            for (uint16_t height = 0; height < dim_h; height++) {
                size_t gbf80_pixel_offset = 0;
                size_t flt32_pixel_offset = 0;
                for (uint16_t width = 0; width < dim_w; width++) {
                    for (uint16_t z = 0; z < dim_z; z++) {
                        uint8_t* gbf80_buffer = (uint8_t*)(formatted_data + gbf80_row_offset + gbf80_pixel_offset);
                        uint32_t* flt32_buffer = (ddata + flt32_row_offset + flt32_pixel_offset);
                        gbf_decode(gbf80_buffer, flt32_buffer, real_dim_c);

                        gbf80_pixel_offset += gbf80_pixel_size;
                        flt32_pixel_offset += real_dim_c;
                    }
                }
                gbf80_row_offset += gbf80_row_size_rowpad;
                flt32_row_offset += flt32_row_size;
            }
        }
    }
    else {
        // for FP32, just copy the data as is
        std::memcpy(ddata, formatted_data, featureMap_size * sizeof(float));
    }

}

//---------------------------------------------------------------------------------------------------------------------

void FeatureMap::apply_folded_ops(const void* vinput, void* voutput) const
{
    const uint32_t* __restrict input = (const uint32_t*) vinput;
    uint32_t* __restrict output = (uint32_t*) voutput;

    #pragma omp simd
    for(unsigned int i = 0; i < pinfo->permuted_indices.size(); ++i) {
        output[i] = input[pinfo->permuted_indices[i]];
    }
}

void* FeatureMap::get_data_ptr()
{
    return fmap_data;
}

MX_status FeatureMap::get_data(float* out_data) const
{
    if(fm_type != FM_DFP) {
        get_data_len(out_data);
        return MX_STATUS_OK;
    }

    #pragma omp parallel if(fmap_convert_threads_ > 1) num_threads(fmap_convert_threads_)
    {

        if(use_model_shape_) {
            unconvert_data(fmap_data);
            apply_folded_ops(fmap_data, out_data);
        }
        else {
            unconvert_data(out_data);
        }
    }
    return MX_STATUS_OK;
}

void FeatureMap::get_data_force_foldedops(float* out_data, bool do_foldedops) const
{
    #pragma omp parallel if(fmap_convert_threads_ > 1) num_threads(fmap_convert_threads_)
    {
        if(do_foldedops)
        {
            unconvert_data(fmap_data);
            apply_folded_ops(fmap_data, out_data);
        }
        else
        {
            unconvert_data(out_data);
        }
    }
}

MX_status FeatureMap::set_data(float* in_data) const
{
    if(fm_type != FM_DFP) {
        set_data_len(in_data);
        return MX_STATUS_OK;
    }

    #pragma omp parallel if(fmap_convert_threads_ > 1) num_threads(fmap_convert_threads_)
    {
        if(use_model_shape_) {
            apply_folded_ops(in_data, fmap_data);
            convert_data(fmap_data);
        }
        else {
            convert_data(in_data);
        }

    }
    return MX_STATUS_OK;
}


void FeatureMap::set_data_force_foldedops(float* in_data, bool do_foldedops) const
{
    #pragma omp parallel if(fmap_convert_threads_ > 1) num_threads(fmap_convert_threads_)
    {
        if(do_foldedops)
        {
            apply_folded_ops(in_data, fmap_data);
            convert_data(fmap_data);
        }
        else
        {
            convert_data(in_data);
        }
    }
}

void FeatureMap::set_data_len(float* in_data, size_t data_len) const
{
    if(data_len == 0) {
        std::memcpy(fmap_data, in_data, featureMap_size * sizeof(float));
    }
    else {
        std::memcpy(fmap_data, in_data, data_len * sizeof(float));
    }
}

void FeatureMap::set_random_data() const
{
    for(size_t i = 0; i < featureMap_size; i++) {
        float rand_val = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        std::memcpy(fmap_data + i, &rand_val, sizeof(float));
    }
    convert_data(fmap_data);
}

void FeatureMap::get_data_len(float* out_data, size_t data_len) const
{
    if(data_len == 0) {
        std::memcpy(out_data, fmap_data, featureMap_size * sizeof(float));
    }
    else {
        std::memcpy(out_data, fmap_data, data_len * sizeof(float));
    }
}

FeatureMap::~FeatureMap()
{
    if (fmap_data_internal != nullptr) {
        delete[] fmap_data_internal;
        fmap_data_internal = nullptr;
        fmap_data = nullptr;
    }
    if(temp_float_buffer_internal != nullptr) {
        delete[] temp_float_buffer_internal;
        temp_float_buffer = nullptr;
        temp_float_buffer_internal = nullptr;
    }
    if (formatted_data != nullptr) {
        delete[] formatted_data;
        formatted_data = nullptr;
    }

    if (hpoc_dummy_channels != nullptr) {
        delete[] hpoc_dummy_channels;
        hpoc_dummy_channels = nullptr;
    }

    if (pinfo != nullptr) {
        if (pinfo->hpoc_en) {
            if(pinfo->hpoc_dummy_channels != nullptr) {
                delete[] pinfo->hpoc_dummy_channels;
                pinfo->hpoc_dummy_channels = nullptr;
            }
        }
        delete pinfo;
        pinfo = nullptr;
    }
}

uint8_t* FeatureMap::get_formatted_data()
{
    return formatted_data;
}

size_t FeatureMap::get_formatted_size()
{
    return formatted_featuremap_size;
}

//TODO: this needs to be changed..
std::vector<int64_t> FeatureMap::shape() const
{
    // TODO: see if this needs to be changed
    MX::Types::ShapeVector shape_vec(dim_h, dim_w, dim_z, dim_c);
    if(use_model_shape_) {
        return shape_vec.chfirst_shape();
    }
    else {
        return shape_vec.chlast_shape();
    }

}

int FeatureMap::get_num_fmap_threads() const
{
    return fmap_convert_threads_;
}
