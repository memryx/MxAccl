// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_FEATUREMAP_H
#define MX_FEATUREMAP_H

#pragma once
#include <vector>
#include <stdint.h>
#include <stdexcept>
#include <mutex>
#include <condition_variable>
#include <unordered_set>

#include <memx/accl/dfp.h>
#include <memx/accl/utils/macros.h>
#include <memx/accl/utils/errors.h>
#include <memx/accl/utils/mxTypes.h>
#include <memx/accl/utils/locked_var.h>

using namespace std;
using namespace MX::Utils;

namespace MX
{
namespace Types
{
enum MEMX_API_EXPORT MX_data_format : uint8_t {
    MX_FMT_GBF80  = 0,
    MX_FMT_RGB565 = 1,
    MX_FMT_RGB888 = 2,
    MX_FMT_YUV422 = 3,
    MX_FMT_BF16   = 4,
    MX_FMT_FP32   = 5,
    MX_FMT_GBF80_ROW = 6
};

enum MEMX_API_EXPORT FeatureMap_Type { FM_DFP, FM_PRE, FM_POST, FM_PREPOST };

/**
 * @class FeatureMap
 * @brief Encapsulates data buffers used by MxAccl for input and output feature maps.
 *
 * The FeatureMap class represents a data container used internally by MxAccl to manage
 * tensor data across the accelerator interface. It provides methods to safely send
 * (`set_data`) and retrieve (`get_data`) data while abstracting low-level memory management.
 */
class FeatureMap
{
  public:
    /**
     * @brief Constructs a FeatureMap by allocating a data block of the given size and
     *        setting its dimensions and format.
     *
     * @param size                  Size of the feature map buffer (in bytes or elements, depending on format).
     * @param format                Data format (e.g., FP32, INT8).
     * @param dim_h                 Height dimension.
     * @param dim_w                 Width dimension.
     * @param dim_z                 Depth (Z) dimension.
     * @param num_chan              Number of channels.
     * @param fmap_convert_threads  Number of threads used for format conversion or transposition.
     * @param use_model_shape       Whether to use the model-defined shape instead of explicit dimensions.
     * @param port_info             Optional port metadata for alignment with model I/O configuration.
     */
    MEMX_API_EXPORT FeatureMap(size_t size, MX_data_format format = MX_FMT_FP32, uint16_t dim_h = 0, uint16_t dim_w = 0,
                               uint16_t dim_z = 0, uint32_t num_chan = 0, int fmap_convert_threads = 1, bool use_model_shape = false,
                               Dfp::PortInfo* port_info = nullptr);

    /**
     * @brief Constructs a FeatureMap and initializes it with the provided input data.
     *
     * A new data block is allocated and populated with the contents of `input`.
     * Dimensions and format are configured as specified.
     *
     * @param input                 Pointer to input data to copy into the feature map.
     * @param size                  Size of the feature map buffer.
     * @param format                Data format.
     * @param dim_h                 Height dimension.
     * @param dim_w                 Width dimension.
     * @param dim_z                 Depth (Z) dimension.
     * @param num_chan              Number of channels.
     * @param fmap_convert_threads  Number of threads used for format conversion or transposition.
     * @param use_model_shape       Whether to use the model-defined shape.
     * @param port_info             Optional port metadata for model alignment.
     */
    MEMX_API_EXPORT FeatureMap(float* input, size_t size, MX_data_format format = MX_FMT_FP32, uint16_t dim_h = 0, uint16_t dim_w = 0,
                               uint16_t dim_z = 0, uint32_t num_chan = 0, int fmap_convert_threads = 1, bool use_model_shape = false,
                               Dfp::PortInfo* port_info = nullptr);

    /**
     * @brief Copy constructor.
     *
     * Creates a deep copy of the provided FeatureMap instance, duplicating all
     * internal metadata and buffer content.
     *
     * @param rhs FeatureMap instance to copy from.
     */
    MEMX_API_EXPORT FeatureMap(const FeatureMap &rhs);

    /**
     * @brief Retrieves the data from the accelerator's output buffer into the given destination.
     *
     * This method copies data from the internal feature map buffer into the user-provided
     * memory pointed to by `out_data`.
     *
     * @param out_data Pointer to the destination buffer for the output data.
     * @return MX_Status indicating success or failure.
     */
    MEMX_API_EXPORT MX::Utils::MX_status get_data(float* out_data) const;

    /**
     * @brief Sets the input data to be sent to the accelerator.
     *
     * Copies data from the user-provided `in_data` pointer into the feature map's internal buffer.
     *
     * @param in_data Pointer to the source input data.
     * @return MX_Status indicating success or failure.
     */
    MEMX_API_EXPORT MX::Utils::MX_status set_data(float* in_data) const;

    MEMX_API_EXPORT void set_data_len(float* in_data, size_t data_len = 0) const;

    MEMX_API_EXPORT void get_data_len(float* out_data, size_t data_len = 0) const;

    //Returns the pointer of formatted data of featureMap
    MEMX_API_EXPORT uint8_t* get_formatted_data();

    MEMX_API_EXPORT ~FeatureMap();

    //vinput -->vinput reordered with permuted_indices, thus efficiently applying folded ops
    MEMX_API_EXPORT void apply_folded_ops(const void* vinput, void* voutput) const;

    //copy assignment operator
    MEMX_API_EXPORT FeatureMap &operator=(const FeatureMap &rhs);

    MEMX_API_EXPORT size_t get_formatted_size();
    MEMX_API_EXPORT std::vector<int64_t> shape() const;
    MEMX_API_EXPORT void* get_data_ptr();
    MEMX_API_EXPORT int get_num_fmap_threads() const;

    FeatureMap_Type fm_type = FM_DFP;


    bool use_model_shape_;

    // non-exposed functions for use with pre/post within MxModel
    void set_data_force_foldedops(float* in_data, bool do_foldedops) const;
    void get_data_force_foldedops(float* out_data, bool do_foldedops) const;

    // set to random data [0.0,1.0]
    void set_random_data() const;

    void convert_data(void* vdata) const; // converts *data -> *formatted_data
    void unconvert_data(void* vdata) const; // converts *formatted_data -> *data
    
  private:
    mutable uint32_t* fmap_data; // data in user-facing format (float)
    mutable uint32_t* temp_float_buffer;
    uint32_t* fmap_data_internal;
    uint32_t* temp_float_buffer_internal;
    size_t featureMap_size; // size, in terms of user-facing format
    MX_data_format fmt; // data format
    uint8_t* formatted_data;
    void calc_convert_size_and_new(); // calculates size for and allocates formatted bytes


    /* dimension variables used for GBF calculations, shape and transofrms*/
    uint16_t dim_h;      // shape dimension x (height)
    uint16_t dim_w;      // shape dimension y (width)
    uint16_t dim_z;      // shape dimension z
    uint32_t dim_c;      // number of channels -- used for GBF calculations, shape and transforms
    uint32_t hpoc_dim_c;
    bool     hpoc_en;
    uint16_t hpoc_list_length; // HPOC channel list length
    uint16_t* hpoc_dummy_channels; // list of dummy channels to remove

    int fmap_convert_threads_;

    Dfp::PortInfo* pinfo;

    // stored constants for format en/decoding
    //-------------------------------------------------
    size_t formatted_featuremap_size; // how many bytes the converted data is
    uint32_t real_dim_c; // real number of channels, before HPOC scrunching
    uint32_t num_xyz_pixels; // number of pixels in the featureMap, i.e. dim_h * dim_w * dim_z
    uint32_t num_gbf_words;
    uint32_t num_gbf_per_pixel;
    bool     any_remainder_chs;
    uint32_t gbf80_pixel_size;
    uint32_t gbf80_row_size;
    uint32_t gbf80_row_size_rowpad;
    uint32_t flt32_row_size;

};
} // namespace Types
} // namespace MX

#endif
