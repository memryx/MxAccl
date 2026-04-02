// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.


#ifndef DFP_H_
#define DFP_H_

///*
// * includes
// ******************************************************************************/

#pragma once

// dtypes
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>


// just need this for the MEMX_API_EXPORT macro
#include <memx/memx.h>

/**
 * @brief Namespace that includes all DFP-related types and classes
 */
namespace Dfp
{

/**
 * @brief A handy enum for FLOAT/UINT8
 */
typedef enum _format {
    FLOAT,
    UINT8
} PortDataFormat;

/**
 * @brief Object that contains all port's sizes (used for AcclUtils::Copy2D, etc.)
 */
class DataShapes
{
  public:
    /**
     * @brief Create an empty object
     */
    MEMX_API_EXPORT DataShapes();

    /**
     * @brief Create a new object with `num` shapes, set to the corresponding size in the `sizes` array
     *
     * @param num   Number of shapes (AKA: number of active ports for this DFP)
     * @param sizes Array of unsigned ints, of length `num`, that are the total size in # of values for that feature map port. For example, fmap of shape [5, 5, 1, 4] = 100 total size.
     */
    MEMX_API_EXPORT DataShapes(int num, unsigned int* sizes_);

    // copy constructor
    MEMX_API_EXPORT DataShapes(const DataShapes &t);

    // destructor
    MEMX_API_EXPORT ~DataShapes();

    /**
     * @brief (Re)sets the number of sizes in this object. This *will clear existing data*.
     *
     * @param num  The new length of the sizes array
     */
    MEMX_API_EXPORT void set_num_shapes(int num);

    /**
     * @brief Sets the total size for a given port/feature map.
     *
     * @param idx  The shape (port/feature map) to set the size for.
     * @param size Total size value to be set for this shape.
     */
    MEMX_API_EXPORT void set_size(int idx, unsigned int size);

    /**
     * @brief Current number of fmap/shapes.
     */
    int num_shapes;

    /**
     * @brief Array with the stored total sizes for each fmap/shape.
     */
    unsigned int* sizes;

    MEMX_API_EXPORT DataShapes &operator=(const DataShapes &other);
    MEMX_API_EXPORT unsigned int &operator[](std::size_t idx);
    MEMX_API_EXPORT unsigned int operator[](std::size_t idx) const;
};

/**
 * @brief Model input / output port configuration struct, same as used by the driver. You probably don't want to use this data directly unless you're using the driver API directly (as in, not SyncAccl/AsyncAccl).
 */
struct PortInfo {
    uint8_t port;        // port index
    uint8_t port_set;    // port set
    uint8_t mpu_id;      // MPU ID
    uint8_t model_index; // model index
    uint8_t active;      // 1: active, 0: inactive
    uint8_t format;      // input port = 0:GBF80, 1:RGB888, 2:RGB565, 3:YUV422, 4:BF16, 5:FP32, 6:YUY2; output port = 0:GBF80, 4:BF16, 5:FP32
    uint16_t dim_h;      // shape dimension x (height)
    uint16_t dim_w;      // shape dimension y (width)
    uint16_t dim_z;      // shape dimension z
    uint32_t dim_c;      // shape dimension c (channels (user, after HPOC))
    size_t total_size;

    // input port only:
    uint8_t range_convert_enabled; // FP->RGB using data ranges conversion
    float range_convert_shift;     // amount to shift before scale
    float range_convert_scale;     // amount to scale before integer cast

    // outport port only:
    uint8_t hpoc_en;               // HPOC enabled/disabled
    uint32_t hpoc_dim_c;           // expanded HPOC channels shape
    uint16_t hpoc_list_length;     // HPOC channel list length
    uint16_t* hpoc_dummy_channels; // list of dummy channels to remove
    char* layer_name;

    int batch; // 0 == "NONE", 1+ == batch size

    std::unordered_map<int, int> raw_shape; // 0 == "NONE"
    std::string                  raw_dtype;

    struct shape_shift_info_t {
        std::vector<int>                add;
        std::vector<int>                remove;
        std::vector<std::string>        folded_optype;
        std::vector< std::vector<int>> folded_opshape;
    } shape_shift_info;

    std::vector<unsigned int> permuted_indices;  // indices that can be use to directly apply folded_ops
    std::vector<unsigned int> compute_index_mapping(std::vector<uint32_t> shape, const PortInfo::shape_shift_info_t &shape_shift_info, bool inport);
};

/**
 * @brief Metadata about the dfp file. This is the same metadata displayed by the `dfp_inspect` Python tool.
 */
struct DfpMeta {
    std::string dfp_version_str; // DFP format version name + int
    int dfp_version;             // -1 for "legacy"

    int subversion; // DFP format version subversion (0 for v6, 1 for v6.1, etc.)

    std::string compile_time; // compilation timestamp

    std::string compiler_version; // compiler version string

    float mxa_gen;            // MXA generation as float (3, 3.1)
    std::string mxa_gen_name; // MXA generation as string (Cascade, Cascade+)

    int num_chips; // number of MXAs this dfp was compiled for

    bool use_multigroup_lb; // if true, double-deploy the 2-chip DFP and do load balancing

    int num_inports;  // number of input ports (generation dependent)
    int num_outports; // number of output ports (generation dependent)

    int num_used_inports;  // number of USED input ports (as set by this dfp)
    int num_used_outports; // number of USED output ports (as set by this dfp)

    int num_models; // number of models in this dfp
    std::vector<std::vector<uint8_t>> model_inports;
    std::vector<std::vector<uint8_t>> model_outports;
};

typedef struct _MemxDfpPortConfig {
    uint8_t port; // port index
    uint8_t port_set; // port set
    uint8_t mpu_id; // MPU ID
    uint8_t model_index; // model index
    uint8_t active; // 1: active, 0: inactive
    uint8_t format; // input port = 0:GBF80, 1:RGB888, 2:RGB565, 3:YUV422, 4:YUY2, 5:BF16?; output port = 0:GBF80, 1:BF16?
    uint16_t dim_x; // shape dimension x (height)
    uint16_t dim_y; // shape dimension y (width)
    uint16_t dim_z; // shape dimension z
    uint32_t dim_c; // shape dimension c (channels (user, after HPOC))

    // input port only:
    uint8_t range_convert_enabled; // FP->RGB using data ranges conversion
    float range_convert_shift; // amount to shift before scale
    float range_convert_scale; // amount to scale before integer cast

    // outport port only:
    uint8_t  hpoc_en; // HPOC enabled/disabled
    uint32_t hpoc_dim_c; // expanded HPOC channels shape
    uint16_t hpoc_list_length; // HPOC channel list length
    uint16_t* hpoc_dummy_channels; // list of dummy channels to remove

} MemxDfpPortConfig;

typedef struct _dfpcontext {
    uint32_t            identifier_data;
    uint32_t            dfp_attr;
    uint32_t            input_mode_flag;
    uint32_t            input_port_number;
    uint32_t            output_port_number;
    uint32_t            weight_size;
    uint32_t            config_size;
    MemxDfpPortConfig* pInputConfigList;
    MemxDfpPortConfig* pOuputConfigList;
    uint8_t* pWeightBaseAdr;
    uint8_t* pRgCfgBaseAdr;
} DfpContext, * pDfpContext;
/**
 * @brief Wrapper around a dfp file, with a variety of methods for obtaining port/feature map shapes and overall DFP metadata.
 */
class DfpObject
{

  public:
    /**
     * @brief Constructor from raw bytes
     *
     * @param f  Pointer to data
     * @param byte_size  Size of data in bytes
     */
    MEMX_API_EXPORT DfpObject(uint8_t* b, size_t byte_size);

    /**
     * @brief Constructor from a file (name as C++ std::string)
     *
     * @param f  Path to .dfp file
     */
    MEMX_API_EXPORT DfpObject(std::string f);

    // destructor
    MEMX_API_EXPORT ~DfpObject();

    /**
     * @brief Get input port info and put the shape/format into the supplied pointers
     *
     * @param port  The port to get info for.
     * @param dh    Pointer to unsigned 16-bit int where fmap HEIGHT is written.
     * @param dw    Pointer to unsigned 16-bit int where fmap WIDTH is written.
     * @param dz    Pointer to unsigned 16-bit int where fmap Z is written.
     * @param dc    Pointer to unsigned 32-bit int where fmap CHANNELS is written.
     *
     * @returns 0 on success, -1 on error
     */
    MEMX_API_EXPORT int get_input_shape_fmt(int port, uint16_t* dh, uint16_t* dw, uint16_t* dz, uint32_t* dc, PortDataFormat* pdf);

    /**
     * @brief Get ALL input ports' info and put the shapes/formats into the supplied array pointers
     *
     * @param dhs    Unsigned 16-bit int array where fmap HEIGHTs are written.
     * @param dws    Unsigned 16-bit int array where fmap WIDTHs are written.
     * @param dzs    Unsigned 16-bit int array where fmap Zs are written.
     * @param dcs    Unsigned 32-bit int array where fmap CHANNELSs are written.
     *
     * @returns 0 on success, -1 on error
     */
    MEMX_API_EXPORT int get_all_input_shapes_fmts(uint16_t* dhs, uint16_t* dws, uint16_t* dzs, uint32_t* dcs, PortDataFormat* pdfs);

    /**
     * @brief Get output port info and put the shape/format into the supplied pointers
     *
     * @param port  The port to get info for.
     * @param dh    Pointer to unsigned 16-bit int where fmap HEIGHT is written.
     * @param dw    Pointer to unsigned 16-bit int where fmap WIDTH is written.
     * @param dz    Pointer to unsigned 16-bit int where fmap Z is written.
     * @param dc    Pointer to unsigned 32-bit int where fmap CHANNELS is written.
     *
     * @returns 0 on success, -1 on error
     */
    MEMX_API_EXPORT int get_output_shape(int port, uint16_t* dh, uint16_t* dw, uint16_t* dz, uint32_t* dc);

    /**
     * @brief Get ALL output ports' info and put the shapes/formats into the supplied array pointers
     *
     * @param dhs    Unsigned 16-bit int array where fmap HEIGHTs are written.
     * @param dws    Unsigned 16-bit int array where fmap WIDTHs are written.
     * @param dzs    Unsigned 16-bit int array where fmap Zs are written.
     * @param dcs    Unsigned 32-bit int array where fmap CHANNELSs are written.
     *
     * @returns 0 on success, -1 on error
     */
    MEMX_API_EXPORT int get_all_output_shapes(uint16_t* dhs, uint16_t* dws, uint16_t* dzs, uint32_t* dcs);

    /**
     * @brief Gets a pointer to the @ref PortInfo data for the given input port.
     *
     * @param port  Port to get info for
     *
     * @returns Pointer to the PortInfo on success, NULL on error.
     */
    MEMX_API_EXPORT PortInfo* input_port(int port);

    /**
     * @brief Gets a pointer to the @ref PortInfo data for the given output port.
     *
     * @param port  Port to get info for
     *
     * @returns Pointer to the PortInfo on success, NULL on error.
     */
    MEMX_API_EXPORT PortInfo* output_port(int port);

    /**
     * @brief Copies ALL input port @ref PortInfo structs to the given PortInfo array.
     *
     * @param dstv  Array of PortInfo structs into which data shall be copied.
     *
     * @returns 0 on success, -1 on error.
     */
    MEMX_API_EXPORT int get_all_input_port_info(PortInfo* dstv);

    /**
     * @brief Copies ALL output port @ref PortInfo structs to the given PortInfo array.
     *
     * @param dstv  Array of PortInfo structs into which data shall be copied.
     *
     * @returns 0 on success, -1 on error.
     */
    MEMX_API_EXPORT int get_all_output_port_info(PortInfo* dstv);

    /**
     * @brief Get a @ref DataShapes object that has all input port total sizes.
     *
     * @returns A DataShapes object.
     */
    MEMX_API_EXPORT DataShapes all_indata_shapes();

    /**
     * @brief Get a @ref DataShapes object that has all output port total sizes.
     *
     * @returns A DataShapes object.
     */
    MEMX_API_EXPORT DataShapes all_outdata_shapes();

    /**
     * @brief Returns a @ref DfpMeta object ptr with all the metadata.
     *
     * @returns DfpMeta ptr for this DFP file.
     */
    MEMX_API_EXPORT DfpMeta* get_dfp_meta();

    /**
     * @brief Returns the path to the opened .dfp file. This is useful for giving the .c_str() to driver API calls if needed.
     *
     * @returns std::string to file path.
     */
    MEMX_API_EXPORT std::string path();

    /**
     * @brief `true` if this is a valid, successfully opened DFP. `false` otherwise.
     */
    bool valid;

    pDfpContext get_cache();
    void set_device_ids(const std::vector<int> &device_ids_to_use);

    // orrrrrr the dfp ptr
    uint8_t* src_dfp_bytes;

    uint64_t hardware_size;

    size_t dfp_byte_size;

    std::vector<int> device_ids_to_use;

  private:
    // actually parses the dfp
    // supports v6 and v5 only
    int __load_dfp_file(const char* f);
    int __load_dfp_bytes(uint8_t* b);

    bool is_from_file;

    // arrays of input/output PortInfos
    PortInfo* iports;
    PortInfo* oports;

    uint8_t* wtmemBuf;
    uint8_t* rgcfigBuf;
    uint32_t wtmemSize;
    uint32_t rgcfigSize;
    // parsed dfp metadata
    DfpMeta meta;
    DfpContext dfpCacheEntry;

    // the file path
    std::string src_file_path;

}; // DfpObject

}; // namespace Dfp

#endif // DFP_H
