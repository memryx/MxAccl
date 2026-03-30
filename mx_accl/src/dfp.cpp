// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include <cassert>
#include <iostream>
#include "spdlog/spdlog.h"

#include <memx/accl/dfp.h>
#include <memx/accl/utils/mxpack.h>

using namespace Dfp;

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-//
// DataShapes                                                    //
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-//

DataShapes::DataShapes()
{
    num_shapes = 0;
    sizes = nullptr;
}

DataShapes::DataShapes(int num, unsigned int* sizes_)
{
    num_shapes = num;
    sizes = new unsigned int[num];
    memcpy(sizes, sizes_, num * sizeof(unsigned int));
}

DataShapes::~DataShapes()
{
    if(sizes != nullptr) {
        delete [] sizes;
        sizes = nullptr;
    }
}

DataShapes::DataShapes(const DataShapes &t)
{
    num_shapes = t.num_shapes;
    sizes = new unsigned int[num_shapes];
    memcpy(sizes, t.sizes, num_shapes * sizeof(unsigned int));
}

void DataShapes::set_num_shapes(int num)
{
    if(sizes != nullptr) {
        delete [] sizes;
    }
    num_shapes = num;
    sizes = new unsigned int[num_shapes];
    memset(sizes, 0, num_shapes * sizeof(float));
}

void DataShapes::set_size(int idx, unsigned int size)
{
    if(idx < 0 || idx >= num_shapes) {
        return;
    }
    sizes[idx] = size;
}


DataShapes &DataShapes::operator=(const DataShapes &other)
{
    if(this != &other) {
        if(other.num_shapes > 0 && other.sizes != nullptr) {
            if(sizes != nullptr) {
                delete [] sizes;
            }
            num_shapes = other.num_shapes;
            sizes = new unsigned int[num_shapes];
            memcpy(sizes, other.sizes, num_shapes * sizeof(float));
        }
    }
    return *this;
}

unsigned int &DataShapes::operator[](std::size_t idx)
{
    if(idx < (size_t)num_shapes) {
        return sizes[idx];
    }
    else {
        return sizes[0];
    }
}

unsigned int DataShapes::operator[](std::size_t idx) const
{
    if(idx < (size_t)num_shapes) {
        return sizes[idx];
    }
    else {
        return 0;
    }
}


//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-//
// DfpObject                                                       //
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-//

// CTORs
//--------------------------------------------------

// ctor using bytes
DfpObject::DfpObject(uint8_t* b, size_t byte_size)
{
    src_dfp_bytes = nullptr;
    iports = nullptr;
    oports = nullptr;
    is_from_file = false;
    dfp_byte_size = byte_size;
    if(__load_dfp_bytes(b) != 0) {
        valid = false;
        if(iports != nullptr) {
            for(int i = 0; i < meta.num_inports; i++) {
                if(iports[i].layer_name != nullptr) {
                    delete [] iports[i].layer_name;
                    iports[i].layer_name = nullptr;
                }
            }
            delete [] iports;
            iports = nullptr;
        }
        if(oports != nullptr) {
            for(int i = 0; i < meta.num_outports; i++) {
                if(oports[i].hpoc_dummy_channels != nullptr) {
                    free(oports[i].hpoc_dummy_channels);
                    oports[i].hpoc_dummy_channels = nullptr;
                }
                if(oports[i].layer_name != nullptr) {
                    delete [] oports[i].layer_name;
                    oports[i].layer_name = nullptr;
                }
            }
            delete [] oports;
            oports = nullptr;
        }
        spdlog::error("[DfpObject] Failed to load DFP from bytes");
        throw(std::runtime_error("Failed to load DFP from bytes"));
    }
    else {
        valid = true;
    };
}

// ctor using string
DfpObject::DfpObject(std::string f)
{
    src_dfp_bytes = nullptr;
    iports = nullptr;
    oports = nullptr;
    is_from_file = true;
    if(__load_dfp_file(f.c_str()) != 0) {
        valid = false;
        meta.num_inports = 0;
        meta.num_outports = 0;
        if(iports != nullptr) {
            for(int i = 0; i < meta.num_inports; i++) {
                if(iports[i].layer_name != nullptr) {
                    delete [] iports[i].layer_name;
                    iports[i].layer_name = nullptr;
                }
            }
            delete [] iports;
            iports = nullptr;
        }
        if(oports != nullptr) {
            for(int i = 0; i < meta.num_outports; i++) {
                if(oports[i].hpoc_dummy_channels != nullptr) {
                    free(oports[i].hpoc_dummy_channels);
                    oports[i].hpoc_dummy_channels = nullptr;
                }
                if(oports[i].layer_name != nullptr) {
                    delete [] oports[i].layer_name;
                    oports[i].layer_name = nullptr;
                }
            }
            delete [] oports;
            oports = nullptr;
        }
        spdlog::error("[DfpObject] Failed to load DFP file: {}", f);
        throw(std::runtime_error("Failed to load DFP file: " + f));
    }
    else {
        valid = true;
    };
}



// DTOR
//--------------------------------------------------
DfpObject::~DfpObject()
{
    if(iports != nullptr) {
        for(int i = 0; i < meta.num_inports; i++) {
            if(iports[i].layer_name != nullptr) {
                delete [] iports[i].layer_name;
                iports[i].layer_name = nullptr;
            }
        }
        delete [] iports;
        iports = nullptr;
    }
    if(oports != nullptr) {
        for(int i = 0; i < meta.num_outports; i++) {
            if(oports[i].hpoc_dummy_channels != nullptr) {
                free(oports[i].hpoc_dummy_channels);
                oports[i].hpoc_dummy_channels = nullptr;
            }
            if(oports[i].layer_name != nullptr) {
                delete [] oports[i].layer_name;
                oports[i].layer_name = nullptr;
            }
        }
        delete [] oports;
        oports = nullptr;
    }
    if (dfpCacheEntry.pWeightBaseAdr) {
        free(dfpCacheEntry.pWeightBaseAdr);
        dfpCacheEntry.pWeightBaseAdr = nullptr;
        dfpCacheEntry.weight_size = 0;
    }
    if (dfpCacheEntry.pRgCfgBaseAdr) {
        free(dfpCacheEntry.pRgCfgBaseAdr);
        dfpCacheEntry.pRgCfgBaseAdr = nullptr;
        dfpCacheEntry.config_size = 0;
    }
    if (dfpCacheEntry.pInputConfigList != nullptr) {
        free(dfpCacheEntry.pInputConfigList);
        dfpCacheEntry.pInputConfigList = nullptr;
    }
    if (dfpCacheEntry.pOuputConfigList != nullptr) {
        for (int i = 0; i < meta.num_outports; i++) {
            if (dfpCacheEntry.pOuputConfigList[i].hpoc_dummy_channels != nullptr) {
                free(dfpCacheEntry.pOuputConfigList[i].hpoc_dummy_channels);
                dfpCacheEntry.pOuputConfigList[i].hpoc_dummy_channels = nullptr;
            }
        }
        free(dfpCacheEntry.pOuputConfigList);
        dfpCacheEntry.pOuputConfigList = nullptr;
    }

    if(is_from_file) {
        if(src_dfp_bytes != nullptr) {
            delete [] src_dfp_bytes;
            src_dfp_bytes = nullptr;
        }
    }
}



// various 'get' functions
//--------------------------------------------------

DfpMeta* DfpObject::get_dfp_meta()
{
    return &meta;
}

pDfpContext DfpObject::get_cache()
{
    return &dfpCacheEntry;
}


// input shapes
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int DfpObject::get_input_shape_fmt(int port, uint16_t* dh, uint16_t* dw, uint16_t* dz, uint32_t* dc, PortDataFormat* pdf)
{

    if(iports == nullptr) { return -1; }

    if(port < 0 || port >= meta.num_inports) {
        printf("Invalid port %d given to get_input_shape\n", port);
        return -1;
    }


    *dh = iports[port].dim_h;
    *dw = iports[port].dim_w;
    *dz = iports[port].dim_z;
    *dc = iports[port].dim_c;

    if(iports[port].format == 0 || iports[port].format == 5 || iports[port].format == 6) {
        *pdf = FLOAT;
    }
    else {
        *pdf = UINT8;
    }


    return 0;
}


int DfpObject::get_all_input_shapes_fmts(uint16_t* dhs, uint16_t* dws, uint16_t* dzs, uint32_t* dcs, PortDataFormat* pdfs)
{

    if(iports == nullptr) { return -1; }

    for(int i = 0; i < meta.num_inports; i++) {
        dhs[i] = iports[i].dim_h;
        dws[i] = iports[i].dim_w;
        dzs[i] = iports[i].dim_z;
        dcs[i] = iports[i].dim_c;
        if(iports[i].format == 0 || iports[i].format == 5 || iports[i].format == 6) {
            pdfs[i] = FLOAT;
        }
        else {
            pdfs[i] = UINT8;
        }
    }

    return 0;
}

DataShapes DfpObject::all_indata_shapes()
{

    DataShapes dat;
    dat.set_num_shapes(meta.num_used_inports);
    for(int i = 0; i < meta.num_used_inports; i++) {
        dat.set_size(i, iports[i].total_size);
    }

    return dat;
}


DataShapes DfpObject::all_outdata_shapes()
{

    DataShapes dat;
    dat.set_num_shapes(meta.num_used_outports);
    for(int i = 0; i < meta.num_used_outports; i++) {
        dat.set_size(i, oports[i].total_size);
    }

    return dat;
}


// output shapes
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int DfpObject::get_output_shape(int port, uint16_t* dh, uint16_t* dw, uint16_t* dz, uint32_t* dc)
{

    if(oports == nullptr) { return -1; }

    if(port < 0 || port >= meta.num_outports) {
        printf("Invalid port %d given to get_output_shape\n", port);
        return -1;
    }

    *dh = oports[port].dim_h;
    *dw = oports[port].dim_w;
    *dz = oports[port].dim_z;
    *dc = oports[port].dim_c;

    return 0;
}


int DfpObject::get_all_output_shapes(uint16_t* dhs, uint16_t* dws, uint16_t* dzs, uint32_t* dcs)
{

    if(oports == nullptr) { return -1; }

    for(int i = 0; i < meta.num_outports; i++) {
        dhs[i] = oports[i].dim_h;
        dws[i] = oports[i].dim_w;
        dzs[i] = oports[i].dim_z;
        dcs[i] = oports[i].dim_c;
    }

    return 0;
}


// complete port info
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
PortInfo* DfpObject::input_port(int port)
{

    if(iports == nullptr) { return nullptr; }

    if(port < 0 || port >= meta.num_inports) {
        printf("Invalid port %d given to get input_port info\n", port);
        return nullptr;
    }

    return &(iports[port]);
}

PortInfo* DfpObject::output_port(int port)
{

    if(oports == nullptr) { return nullptr; }

    if(port < 0 || port >= meta.num_outports) {
        printf("Invalid port %d given to get_output_port_info\n", port);
        return nullptr;
    }

    // FYI: this calls the default copy constructors for strings, vectors, etc.
    return &(oports[port]);
}

int DfpObject::get_all_input_port_info(PortInfo* dstv)
{
    if(iports == nullptr) { return -1; }
    for(int i = 0; i < meta.num_inports; i++) {
        dstv[i] = iports[i];
    }
    return 0;
}

int DfpObject::get_all_output_port_info(PortInfo* dstv)
{
    if(oports == nullptr) { return -1; }
    for(int i = 0; i < meta.num_outports; i++) {
        dstv[i] = oports[i];
    }
    return 0;
}

std::string DfpObject::path()
{
    return src_file_path;
}

// helper to parse list of ints from mxpack_list_t*
//--------------------------------------------------
std::vector<int> parse_int_list(mxpack_list_t* list)
{
    std::vector<int> result;
    if (!list || !list->data) { return result; }

    switch (list->dtype) {
        case MXPACK_UINT8:
            for (uint32_t i = 0; i < list->num_elem; ++i) {
                result.push_back(static_cast<uint8_t*>(list->data)[i]);
            }
            break;
        case MXPACK_UINT16:
            for (uint32_t i = 0; i < list->num_elem; ++i) {
                result.push_back(static_cast<uint16_t*>(list->data)[i]);
            }
            break;
        case MXPACK_UINT32:
            for (uint32_t i = 0; i < list->num_elem; ++i) {
                result.push_back(static_cast<uint32_t*>(list->data)[i]);
            }
            break;
        default:
            std::cerr << "Unsupported dtype in parse_int_list\n";
            break;
    }

    return result;
}


// the big cahuna
//--------------------------------------------------
int DfpObject::__load_dfp_bytes(uint8_t* b)
{

    size_t offset = 0;
    uint64_t sim_data_len;
    PortInfo* port_cfg = nullptr;

    src_dfp_bytes = b;

    //------------------------------------------------------------------
    // first 8 bytes are DFP version header (or legacy #bytes)
    memcpy(&sim_data_len, b + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);


    //------------------------------------------------------------------
    // DFP v6
    if(sim_data_len == 6) {
        meta.dfp_version_str = "6";
        meta.dfp_version = 6;
        mxpack_list_t* templ = nullptr;
        mxpack_dict_t* tempd = nullptr;
        mxpack_ascii_t* temps = nullptr;


        uint8_t dtype;
        memcpy(&dtype, b + offset, 1);
        offset += 1;
        // check for dict as outermost type
        if(dtype != 0x01) {
            throw(std::runtime_error("DFPv6 outermost type is not a dict"));
            return -1;
        }

        // init the dict
        mxpack_dict_t d;
        size_t num_pbytes = mxpack_process_dict(&d, b + offset);
        if(num_pbytes == 0) {
            throw(std::runtime_error("DFPv6 MXPACK parsing failed"));
            return -1;
        }

        // number of models
        templ = (mxpack_list_t*) mxpack_get_keyval(&d, "models");
        meta.num_models = templ->num_elem;

        // subversion (if present)
        uint8_t* subver = (uint8_t*) mxpack_get_keyval(&d, "dfp_sub_version");
        uint8_t subver_default = 0;
        if(subver == NULL) {
            meta.subversion = 0; // default to 0 if not present
            subver = &subver_default; // use default value
        }
        else {
            meta.subversion = *subver;
        }

        // compile date/time
        temps = (mxpack_ascii_t*) mxpack_get_keyval(&d, "compile_timestamp");
        meta.compile_time = std::string(temps->s);

        // compiler version
        temps = (mxpack_ascii_t*) mxpack_get_keyval(&d, "compiler_version");
        meta.compiler_version = std::string(temps->s);

        // use load-balancing 2x2 config?
        bool* use_2x2_lb = (bool*) mxpack_get_keyval(&d, "use_multigroup_loadbalance");
        if(use_2x2_lb == nullptr) {
            meta.use_multigroup_lb = false;
        }
        else {
            meta.use_multigroup_lb = *use_2x2_lb;
        }

        // MXA generation
        tempd = (mxpack_dict_t*) mxpack_get_keyval(&d, "sim_meta");
        uint8_t chip_gen = *( (uint8_t*) mxpack_get_keyval(tempd, "intgen") );
        if(chip_gen == 4) {
            meta.mxa_gen = 3.1;
            meta.mxa_gen_name = "Cascade+";
        }
        else {
            mxpack_free_dict(&d);
            throw(std::runtime_error("DFPv6: this DFP is for an unsupported MXA generation"));
            return -1;
        }


        //---------------------------------------------------//


        // num MXAs
        meta.num_chips = *( (uint8_t*) mxpack_get_keyval(tempd, "num_mpus") );

        // num ports
        meta.num_inports = *( (uint8_t*) mxpack_get_keyval(&d, "num_inports") );
        meta.num_outports = *( (uint8_t*) mxpack_get_keyval(&d, "num_outports") );

        iports = new PortInfo[meta.num_inports];
        oports = new PortInfo[meta.num_outports];

        mxpack_list_t* pinfol = nullptr;
        pinfol = (mxpack_list_t*) mxpack_get_keyval(&d, "inport_info");
        if(pinfol->num_elem != (uint32_t) meta.num_inports) {
            mxpack_free_dict(&d);
            throw(std::runtime_error("DFPv6: mismatching num inports and length of inport info list!"));
            return -1;
        }
        // HardWare
        mxpack_binary_t*    sdfp    = nullptr;
        uint8_t*            pSrc    = nullptr;
        size_t              hoffset = 0;
        sdfp = (mxpack_binary_t*)mxpack_get_keyval(&d, "hw_dfp");
        if (sdfp) {
            pSrc = (uint8_t*) sdfp->data;
            hoffset += sizeof(uint64_t); // skip hw_data_len

            dfpCacheEntry.weight_size = *(uint32_t*)(pSrc + hoffset);
            hoffset += sizeof(uint32_t);
            dfpCacheEntry.pWeightBaseAdr = (uint8_t*)malloc(dfpCacheEntry.weight_size);

            if (dfpCacheEntry.pWeightBaseAdr) {
                memcpy(dfpCacheEntry.pWeightBaseAdr, pSrc + hoffset, dfpCacheEntry.weight_size);
                hoffset += dfpCacheEntry.weight_size;
                //printf("wtmemSize %d buffer %p\r\n", dfpCacheEntry.weight_size, dfpCacheEntry.pWeightBaseAdr);
            }
            else {
                mxpack_free_dict(&d);
                throw(std::runtime_error("DFPv6 hwdfp parse error"));
                return -1;
            }

            //num_rgcfg = *(uint8_t*)(pSrc + hoffset);
            hoffset += 1;
            dfpCacheEntry.config_size = *(uint32_t*)(pSrc + hoffset);
            hoffset += sizeof(uint32_t);
            dfpCacheEntry.pRgCfgBaseAdr = (uint8_t*)malloc(dfpCacheEntry.config_size);

            if (dfpCacheEntry.pRgCfgBaseAdr) {
                memcpy(dfpCacheEntry.pRgCfgBaseAdr, pSrc + hoffset, dfpCacheEntry.config_size);
                hoffset += dfpCacheEntry.config_size;
                //printf("rgcfigSize %d buffer %p\r\n", dfpCacheEntry.config_size, dfpCacheEntry.pRgCfgBaseAdr);
            }
            else {
                mxpack_free_dict(&d);
                throw(std::runtime_error("DFPv6 hwdfp parse error"));
                return -1;
            }
        }
        else {
            mxpack_free_dict(&d);
            throw(std::runtime_error("DFPv6 hwdfp parse error"));
            return -1;
        }

        // INPORTS
        // =============================================================================
        meta.num_used_inports = 0;
        for(uint8_t i = 0; i < meta.num_inports; i++) {
            port_cfg = &(iports[i]);

            mxpack_dict_t* pd = (mxpack_dict_t*) mxpack_get_list_item_ptr(pinfol, i);
            if(pd == nullptr) {
                mxpack_free_dict(&d);
                throw(std::runtime_error("DFPv6 inport_info parse error"));
                return -1;
            }

            port_cfg->port = *( (uint8_t*) mxpack_get_keyval(pd, "port"));
            port_cfg->active = *( (uint8_t*) mxpack_get_keyval(pd, "active"));
            if(port_cfg->active) {
                // port_set
                port_cfg->port_set = *((uint8_t*) mxpack_get_keyval(pd, "port_set"));
                // mpu_id
                port_cfg->mpu_id = *((uint8_t*) mxpack_get_keyval(pd, "mpu_id"));
                // model_index
                port_cfg->model_index = *((uint8_t*) mxpack_get_keyval(pd, "model_index"));
                // format
                tempd = (mxpack_dict_t*) mxpack_get_keyval(pd, "packing_format");
                port_cfg->format = *((uint8_t*) mxpack_get_keyval(tempd, "as_int"));
                // layer name
                temps = (mxpack_ascii_t*) mxpack_get_keyval(pd, "layer_name");
                port_cfg->layer_name = new char[temps->length + 1];
                memset(port_cfg->layer_name, 0, temps->length + 1);
                strncpy(port_cfg->layer_name, temps->s, temps->length);
                // range conversion stuff
                tempd = (mxpack_dict_t*) mxpack_get_keyval(pd, "range_convert");
                port_cfg->range_convert_enabled = *((uint8_t*) mxpack_get_keyval(tempd, "enabled"));
                if(port_cfg->range_convert_enabled) {
                    port_cfg->range_convert_shift = *((float*) mxpack_get_keyval(tempd, "shift"));
                    port_cfg->range_convert_scale = *((float*) mxpack_get_keyval(tempd, "scale"));
                }
                else {
                    port_cfg->range_convert_shift = 0.0;
                    port_cfg->range_convert_scale = 1.0;
                }
                // shape
                templ = (mxpack_list_t*) mxpack_get_keyval(pd, "mxa_shape");
                switch(templ->dtype) {
                    case MXPACK_UINT8:
                        port_cfg->dim_h = *((uint8_t*) mxpack_get_list_item_ptr(templ, 0));
                        port_cfg->dim_w = *((uint8_t*) mxpack_get_list_item_ptr(templ, 1));
                        port_cfg->dim_z = *((uint8_t*) mxpack_get_list_item_ptr(templ, 2));
                        port_cfg->dim_c = *((uint8_t*) mxpack_get_list_item_ptr(templ, 3));
                        break;
                    case MXPACK_UINT16:
                        port_cfg->dim_h = *((uint16_t*) mxpack_get_list_item_ptr(templ, 0));
                        port_cfg->dim_w = *((uint16_t*) mxpack_get_list_item_ptr(templ, 1));
                        port_cfg->dim_z = *((uint16_t*) mxpack_get_list_item_ptr(templ, 2));
                        port_cfg->dim_c = *((uint16_t*) mxpack_get_list_item_ptr(templ, 3));
                        break;
                    case MXPACK_UINT32:
                        port_cfg->dim_h = (uint16_t) * ((uint32_t*) mxpack_get_list_item_ptr(templ, 0));
                        port_cfg->dim_w = (uint16_t) * ((uint32_t*) mxpack_get_list_item_ptr(templ, 1));
                        port_cfg->dim_z = (uint16_t) * ((uint32_t*) mxpack_get_list_item_ptr(templ, 2));
                        port_cfg->dim_c = *((uint32_t*) mxpack_get_list_item_ptr(templ, 3));
                        break;
                    default:
                        // ERROR
                        mxpack_free_dict(&d);
                        throw(std::runtime_error("DFPv6 inport shape parse error"));
                        return -1;
                }

                // input ports never use HPOC
                port_cfg->hpoc_en = false;
                port_cfg->hpoc_dim_c = 0;
                port_cfg->hpoc_list_length = 0;
                port_cfg->hpoc_dummy_channels = nullptr;

                // v6.0 can only have channel first -> channel last as a potential folded op
                if(*subver == 0) {
                    port_cfg->batch = 0;

                    // raw shape
                    auto* rsd = static_cast<mxpack_dict_t*>(mxpack_get_keyval(pd, "raw_shape"));
                    if (rsd != nullptr) {
                        for (uint32_t i = 0; i < rsd->num_keys; ++i) {
                            std::string key_str(rsd->data[i].key);
                            int key_int = std::atoi(key_str.c_str());
                            void* value_ptr = rsd->data[i].value;
                            int value = 0;

                            switch (rsd->data[i].dtype) {
                                case MXPACK_ASCII: {
                                    auto* val_ascii = static_cast<mxpack_ascii_t*>(value_ptr);
                                    if (val_ascii != nullptr && val_ascii->s != nullptr) {
                                        std::string value_str(val_ascii->s);
                                        if (value_str == "NONE" || value_str == "none" || value_str == "None") {
                                            value = 0;
                                        }
                                        else {
                                            value = std::atoi(value_str.c_str());
                                        }
                                    }
                                    break;
                                }
                                case MXPACK_UINT8:
                                    value = *static_cast<uint8_t*>(value_ptr);
                                    break;
                                case MXPACK_UINT16:
                                    value = *static_cast<uint16_t*>(value_ptr);
                                    break;
                                case MXPACK_UINT32:
                                    value = *static_cast<uint32_t*>(value_ptr);
                                    break;
                                default:
                                    std::cerr << "Unsupported dtype in raw_shape for key " << key_str << "\n";
                                    break;
                            }

                            port_cfg->raw_shape[key_int] = value;
                        }


                        // if raw_shape[3] exists and is not equal to dim_c, then
                        // add a folded op of type "legacy_channel_transpose" (string)
                        if (port_cfg->raw_shape.find(port_cfg->raw_shape.size() - 1) != port_cfg->raw_shape.end() && port_cfg->raw_shape[port_cfg->raw_shape.size() - 1] != (int) port_cfg->dim_c) {

                            // check if dim_c is in raw_shape[0] or raw_shape[1]
                            if (port_cfg->raw_shape.find(0) != port_cfg->raw_shape.end() && port_cfg->raw_shape[0] == (int) port_cfg->dim_c) {
                                port_cfg->shape_shift_info.folded_optype.push_back("legacy_channel_transpose");
                            }
                            else if (port_cfg->raw_shape.find(1) != port_cfg->raw_shape.end() && port_cfg->raw_shape[1] == (int) port_cfg->dim_c) {
                                port_cfg->shape_shift_info.folded_optype.push_back("legacy_channel_transpose");
                            }
                            else {
                                port_cfg->shape_shift_info.folded_optype.clear();
                            }

                        }
                        else {
                            port_cfg->shape_shift_info.folded_optype.clear();
                        }

                    }
                    else {
                        port_cfg->raw_shape.clear();
                    }

                }
                else {
                    // v6.1 has batch and folded DFP op parameters to parse
                    if (*subver == 1) {
                        // batch
                        uint8_t* batch_ptr = static_cast<uint8_t*>(mxpack_get_keyval(pd, "batch"));
                        port_cfg->batch = (batch_ptr != nullptr) ? *batch_ptr : 0;

                        // raw_dtype
                        auto* raw_dtype = static_cast<mxpack_ascii_t*>(mxpack_get_keyval(pd, "raw_dtype"));
                        port_cfg->raw_dtype = (raw_dtype != nullptr) ? std::string(raw_dtype->s) : "INVALID";

                        // raw_shape
                        auto* rsd = static_cast<mxpack_dict_t*>(mxpack_get_keyval(pd, "raw_shape"));
                        if (rsd != nullptr) {
                            for (uint32_t i = 0; i < rsd->num_keys; ++i) {
                                std::string key_str(rsd->data[i].key);
                                int key_int = std::atoi(key_str.c_str());
                                void* value_ptr = rsd->data[i].value;
                                int value = 0;


                                switch (rsd->data[i].dtype) {
                                    case MXPACK_ASCII: {
                                        auto* val_ascii = static_cast<mxpack_ascii_t*>(value_ptr);
                                        if (val_ascii != nullptr && val_ascii->s != nullptr) {
                                            std::string value_str(val_ascii->s);
                                            if (value_str == "NONE" || value_str == "none" || value_str == "None") {
                                                value = 0;
                                            }
                                            else {
                                                value = std::atoi(value_str.c_str());
                                            }
                                        }
                                        break;
                                    }
                                    case MXPACK_UINT8:
                                        value = *static_cast<uint8_t*>(value_ptr);
                                        break;
                                    case MXPACK_UINT16:
                                        value = *static_cast<uint16_t*>(value_ptr);
                                        break;
                                    case MXPACK_UINT32:
                                        value = *static_cast<uint32_t*>(value_ptr);
                                        break;
                                    default:
                                        std::cerr << "Unsupported dtype in raw_shape for key " << key_str << "\n";
                                        break;
                                }

                                port_cfg->raw_shape[key_int] = value;
                            }

                            // special case to strip batch down to 1
                            if (port_cfg->raw_shape[0] == 0 || port_cfg->raw_shape[0] == port_cfg->batch) {
                                port_cfg->raw_shape[0] = 1;
                            }
                        }
                        else {
                            port_cfg->raw_shape.clear();
                        }

                        // shape_shift_info
                        auto* ssi = static_cast<mxpack_dict_t*>(mxpack_get_keyval(pd, "shape_shift_info"));
                        if (ssi != nullptr) {
                            // .add
                            auto* add_list = static_cast<mxpack_list_t*>(mxpack_get_keyval(ssi, "+"));
                            port_cfg->shape_shift_info.add.clear();
                            if (add_list != nullptr) {
                                for (uint32_t j = 0; j < add_list->num_elem; ++j) {
                                    port_cfg->shape_shift_info.add.push_back((static_cast<uint8_t*>(add_list->data))[j]);
                                }
                            }

                            // .remove
                            auto* remove_list = static_cast<mxpack_list_t*>(mxpack_get_keyval(ssi, "-"));
                            port_cfg->shape_shift_info.remove.clear();
                            if (remove_list != nullptr) {
                                for (uint32_t j = 0; j < remove_list->num_elem; ++j) {
                                    port_cfg->shape_shift_info.remove.push_back((static_cast<uint8_t*>(remove_list->data))[j]);
                                }
                            }

                            // .folded_optype
                            auto* folded_optype_list = static_cast<mxpack_list_t*>(mxpack_get_keyval(ssi, "folded_optype"));
                            port_cfg->shape_shift_info.folded_optype.clear();
                            if (folded_optype_list != nullptr) {
                                for (uint32_t j = 0; j < folded_optype_list->num_elem; ++j) {
                                    auto* val_ascii = &static_cast<mxpack_ascii_t*>(folded_optype_list->data)[j];
                                    port_cfg->shape_shift_info.folded_optype.emplace_back(val_ascii->s);
                                }
                            }

                            // .folded_opshape (list of list of ints)
                            auto* folded_opshape_list = static_cast<mxpack_list_t*>(mxpack_get_keyval(ssi, "folded_opshape"));
                            port_cfg->shape_shift_info.folded_opshape.clear();
                            if (folded_opshape_list != nullptr) {
                                for (uint32_t j = 0; j < folded_opshape_list->num_elem; ++j) {
                                    auto* shape_list = &static_cast<mxpack_list_t*>(folded_opshape_list->data)[j];
                                    port_cfg->shape_shift_info.folded_opshape.push_back(parse_int_list(shape_list));
                                }
                            }

                            // applying transforms to calculate & initialize permuted_indices @ input ports
                            std::vector<uint32_t> model_shape;
                            for(size_t i = 0; i < port_cfg->raw_shape.size(); ++i) {
                                model_shape.push_back(port_cfg->raw_shape.at(i));
                            }
                            port_cfg->permuted_indices = port_cfg->compute_index_mapping(model_shape, port_cfg->shape_shift_info, true);


                        } // end of shape_shift_info parsing
                    } // end of v6.1 parsing
                }

                port_cfg->total_size = ( port_cfg->dim_h
                                         * port_cfg->dim_w
                                         * port_cfg->dim_z
                                         * port_cfg->dim_c );

                meta.num_used_inports += 1;
                while(meta.model_inports.size() <= port_cfg->model_index) {
                    std::vector<uint8_t> vect;
                    meta.model_inports.push_back(vect);
                }
                meta.model_inports[port_cfg->model_index].push_back(port_cfg->port);
            }
            else {
                // INACTIVE inports skipped
                port_cfg->layer_name = nullptr;
            }

            //// print dims for debug
            //printf("Port[%d]: [ %u, %u, %u, %u ]\n", port_cfg->port, port_cfg->dim_h, port_cfg->dim_w, port_cfg->dim_z, port_cfg->dim_c );
        }
        dfpCacheEntry.input_port_number = meta.num_inports;
        dfpCacheEntry.input_mode_flag = 0;
        dfpCacheEntry.pInputConfigList = (MemxDfpPortConfig*) calloc(meta.num_inports, sizeof(MemxDfpPortConfig));
        for (uint8_t i = 0; i < meta.num_inports; i++) {
            port_cfg = &(iports[i]);
            MemxDfpPortConfig*  pInputCfg = &(dfpCacheEntry.pInputConfigList[i]);


            pInputCfg->port = port_cfg->port;
            pInputCfg->active = port_cfg->active;

            if (pInputCfg->active) {
                port_cfg->active = 1;

                // port_set
                pInputCfg->port_set = port_cfg->port_set;

                // mpu_id
                pInputCfg->mpu_id = port_cfg->mpu_id;

                // model_index
                pInputCfg->model_index = port_cfg->model_index;

                // format
                pInputCfg->format = port_cfg->format;

                // shape
                pInputCfg->dim_x = port_cfg->dim_h;
                pInputCfg->dim_y = port_cfg->dim_w;
                pInputCfg->dim_z = port_cfg->dim_z;
                pInputCfg->dim_c = port_cfg->dim_c;

                // range convert stuff
                pInputCfg->range_convert_enabled = port_cfg->range_convert_enabled;
                pInputCfg->range_convert_shift = port_cfg->range_convert_shift;
                pInputCfg->range_convert_scale = port_cfg->range_convert_scale;
            } // if enabled

        } // for all port

        // OUTPORTS
        // =============================================================================
        pinfol = (mxpack_list_t*) mxpack_get_keyval(&d, "outport_info");
        if(pinfol->num_elem != (uint32_t) meta.num_outports) {
            mxpack_free_dict(&d);
            throw(std::runtime_error("DFPv6: mismatching num outports and length of outport info list!"));
            return -1;
        }

        meta.num_used_outports = 0;
        for(uint8_t i = 0; i < meta.num_outports; i++) {
            port_cfg = &(oports[i]);

            mxpack_dict_t* pd = (mxpack_dict_t*) mxpack_get_list_item_ptr(pinfol, i);
            if(pd == nullptr) {
                mxpack_free_dict(&d);
                throw(std::runtime_error("DFPv6 outport_info parse error"));
                return -1;
            }

            port_cfg->port = *( (uint8_t*) mxpack_get_keyval(pd, "port"));
            port_cfg->active = *( (uint8_t*) mxpack_get_keyval(pd, "active"));
            if(port_cfg->active) {
                // port_set
                port_cfg->port_set = *((uint8_t*) mxpack_get_keyval(pd, "port_set"));
                // mpu_id
                port_cfg->mpu_id = *((uint8_t*) mxpack_get_keyval(pd, "mpu_id"));
                // model_index
                port_cfg->model_index = *((uint8_t*) mxpack_get_keyval(pd, "model_index"));
                // format
                tempd = (mxpack_dict_t*) mxpack_get_keyval(pd, "packing_format");
                port_cfg->format = *((uint8_t*) mxpack_get_keyval(tempd, "as_int"));
                // layer name
                temps = (mxpack_ascii_t*) mxpack_get_keyval(pd, "layer_name");
                port_cfg->layer_name = new char[temps->length + 1];
                memset(port_cfg->layer_name, 0, temps->length + 1);
                strncpy(port_cfg->layer_name, temps->s, temps->length);
                // shape
                templ = (mxpack_list_t*) mxpack_get_keyval(pd, "mxa_shape");
                switch(templ->dtype) {
                    case MXPACK_UINT8:
                        port_cfg->dim_h = *((uint8_t*) mxpack_get_list_item_ptr(templ, 0));
                        port_cfg->dim_w = *((uint8_t*) mxpack_get_list_item_ptr(templ, 1));
                        port_cfg->dim_z = *((uint8_t*) mxpack_get_list_item_ptr(templ, 2));
                        port_cfg->dim_c = *((uint8_t*) mxpack_get_list_item_ptr(templ, 3));
                        break;
                    case MXPACK_UINT16:
                        port_cfg->dim_h = *((uint16_t*) mxpack_get_list_item_ptr(templ, 0));
                        port_cfg->dim_w = *((uint16_t*) mxpack_get_list_item_ptr(templ, 1));
                        port_cfg->dim_z = *((uint16_t*) mxpack_get_list_item_ptr(templ, 2));
                        port_cfg->dim_c = *((uint16_t*) mxpack_get_list_item_ptr(templ, 3));
                        break;
                    case MXPACK_UINT32:
                        port_cfg->dim_h = (uint16_t) * ((uint32_t*) mxpack_get_list_item_ptr(templ, 0));
                        port_cfg->dim_w = (uint16_t) * ((uint32_t*) mxpack_get_list_item_ptr(templ, 1));
                        port_cfg->dim_z = (uint16_t) * ((uint32_t*) mxpack_get_list_item_ptr(templ, 2));
                        port_cfg->dim_c = *((uint32_t*) mxpack_get_list_item_ptr(templ, 3));
                        break;
                    default:
                        // ERROR
                        mxpack_free_dict(&d);
                        throw(std::runtime_error("DFPv6 inport shape parse error"));
                        return -1;
                }

                // v6.0 can only have channel first -> channel last as a potential folded op
                if(*subver == 0) {
                    port_cfg->batch = 0;

                    // raw shape
                    auto* rsd = static_cast<mxpack_dict_t*>(mxpack_get_keyval(pd, "raw_shape"));
                    if (rsd != nullptr) {
                        for (uint32_t i = 0; i < rsd->num_keys; ++i) {
                            std::string key_str(rsd->data[i].key);
                            int key_int = std::atoi(key_str.c_str());
                            void* value_ptr = rsd->data[i].value;
                            int value = 0;

                            switch (rsd->data[i].dtype) {
                                case MXPACK_ASCII: {
                                    auto* val_ascii = static_cast<mxpack_ascii_t*>(value_ptr);
                                    if (val_ascii != nullptr && val_ascii->s != nullptr) {
                                        std::string value_str(val_ascii->s);
                                        if (value_str == "NONE" || value_str == "none" || value_str == "None") {
                                            value = 0;
                                        }
                                        else {
                                            value = std::atoi(value_str.c_str());
                                        }
                                    }
                                    break;
                                }
                                case MXPACK_UINT8:
                                    value = *static_cast<uint8_t*>(value_ptr);
                                    break;
                                case MXPACK_UINT16:
                                    value = *static_cast<uint16_t*>(value_ptr);
                                    break;
                                case MXPACK_UINT32:
                                    value = *static_cast<uint32_t*>(value_ptr);
                                    break;
                                default:
                                    std::cerr << "Unsupported dtype in raw_shape for key " << key_str << "\n";
                                    break;
                            }

                            port_cfg->raw_shape[key_int] = value;
                        }

                        // if raw_shape[3] exists and is not equal to dim_c, then
                        // add a folded op of type "legacy_channel_transpose" (string)
                        int last_dim_idx = port_cfg->raw_shape.size() - 1;
                        if (port_cfg->raw_shape.find(port_cfg->raw_shape.size() - 1) != port_cfg->raw_shape.end() && port_cfg->raw_shape[port_cfg->raw_shape.size() - 1] != (int) port_cfg->dim_c) {

                            // check if dim_c is in raw_shape[0] or raw_shape[1]
                            if (port_cfg->raw_shape.find(0) != port_cfg->raw_shape.end() && port_cfg->raw_shape[0] == (int) port_cfg->dim_c) {
                                port_cfg->shape_shift_info.folded_optype.push_back("legacy_channel_transpose");
                            }
                            else if (port_cfg->raw_shape.find(1) != port_cfg->raw_shape.end() && port_cfg->raw_shape[1] == (int) port_cfg->dim_c) {
                                port_cfg->shape_shift_info.folded_optype.push_back("legacy_channel_transpose");
                            }
                            else {
                                port_cfg->shape_shift_info.folded_optype.clear();
                            }

                        }
                        else {
                            port_cfg->shape_shift_info.folded_optype.clear();
                        }

                    }
                    else {
                        port_cfg->raw_shape.clear();
                    }


                }
                else {
                    // v6.1 has batch and folded DFP op parameters to parse
                    if (*subver == 1) {
                        // batch
                        uint8_t* batch_ptr = static_cast<uint8_t*>(mxpack_get_keyval(pd, "batch"));
                        port_cfg->batch = (batch_ptr != nullptr) ? *batch_ptr : 0;

                        // raw_dtype
                        auto* raw_dtype = static_cast<mxpack_ascii_t*>(mxpack_get_keyval(pd, "raw_dtype"));
                        port_cfg->raw_dtype = (raw_dtype != nullptr) ? std::string(raw_dtype->s) : "INVALID";

                        // raw_shape
                        auto* rsd = static_cast<mxpack_dict_t*>(mxpack_get_keyval(pd, "raw_shape"));
                        if (rsd != nullptr) {
                            for (uint32_t i = 0; i < rsd->num_keys; ++i) {
                                std::string key_str(rsd->data[i].key);
                                int key_int = std::atoi(key_str.c_str());
                                void* value_ptr = rsd->data[i].value;
                                int value = 0;

                                switch (rsd->data[i].dtype) {
                                    case MXPACK_ASCII: {
                                        auto* val_ascii = static_cast<mxpack_ascii_t*>(value_ptr);
                                        if (val_ascii != nullptr && val_ascii->s != nullptr) {
                                            std::string value_str(val_ascii->s);
                                            if (value_str == "NONE" || value_str == "none" || value_str == "None") {
                                                value = 0;
                                            }
                                            else {
                                                value = std::atoi(value_str.c_str());
                                            }
                                        }
                                        break;
                                    }
                                    case MXPACK_UINT8:
                                        value = *static_cast<uint8_t*>(value_ptr);
                                        break;
                                    case MXPACK_UINT16:
                                        value = *static_cast<uint16_t*>(value_ptr);
                                        break;
                                    case MXPACK_UINT32:
                                        value = *static_cast<uint32_t*>(value_ptr);
                                        break;
                                    default:
                                        std::cerr << "Unsupported dtype in raw_shape for key " << key_str << "\n";
                                        break;
                                }

                                port_cfg->raw_shape[key_int] = value;

                                // special case to strip batch down to 1
                                if (port_cfg->raw_shape[0] == 0 || port_cfg->raw_shape[0] == port_cfg->batch) {
                                    port_cfg->raw_shape[0] = 1;
                                }
                            }
                        }
                        else {
                            port_cfg->raw_shape.clear();
                        }

                        // shape_shift_info
                        auto* ssi = static_cast<mxpack_dict_t*>(mxpack_get_keyval(pd, "shape_shift_info"));
                        if (ssi != nullptr) {
                            // .add
                            auto* add_list = static_cast<mxpack_list_t*>(mxpack_get_keyval(ssi, "+"));
                            port_cfg->shape_shift_info.add.clear();
                            if (add_list != nullptr) {
                                for (uint32_t j = 0; j < add_list->num_elem; ++j) {
                                    port_cfg->shape_shift_info.add.push_back((static_cast<uint8_t*>(add_list->data))[j]);
                                }
                            }

                            // .remove
                            auto* remove_list = static_cast<mxpack_list_t*>(mxpack_get_keyval(ssi, "-"));
                            port_cfg->shape_shift_info.remove.clear();
                            if (remove_list != nullptr) {
                                for (uint32_t j = 0; j < remove_list->num_elem; ++j) {
                                    port_cfg->shape_shift_info.remove.push_back((static_cast<uint8_t*>(remove_list->data))[j]);
                                }
                            }

                            // .folded_optype
                            auto* folded_optype_list = static_cast<mxpack_list_t*>(mxpack_get_keyval(ssi, "folded_optype"));
                            port_cfg->shape_shift_info.folded_optype.clear();
                            if (folded_optype_list != nullptr) {
                                for (uint32_t j = 0; j < folded_optype_list->num_elem; ++j) {
                                    auto* val_ascii = &static_cast<mxpack_ascii_t*>(folded_optype_list->data)[j];
                                    port_cfg->shape_shift_info.folded_optype.emplace_back(val_ascii->s);
                                }
                            }

                            // .folded_opshape (list of list of ints)
                            auto* folded_opshape_list = static_cast<mxpack_list_t*>(mxpack_get_keyval(ssi, "folded_opshape"));
                            port_cfg->shape_shift_info.folded_opshape.clear();
                            if (folded_opshape_list != nullptr) {
                                for (uint32_t j = 0; j < folded_opshape_list->num_elem; ++j) {
                                    auto* shape_list = &static_cast<mxpack_list_t*>(folded_opshape_list->data)[j];
                                    port_cfg->shape_shift_info.folded_opshape.push_back(parse_int_list(shape_list));
                                }
                            }
                            // applying transforms to calculate & initialize permuted_indices @ output ports
                            std::vector<uint32_t> mxa_shape = {port_cfg->dim_h, port_cfg->dim_w, port_cfg->dim_z, port_cfg->dim_c};
                            port_cfg->permuted_indices = port_cfg->compute_index_mapping(mxa_shape, port_cfg->shape_shift_info, false);

                        } // end of shape_shift_info parsing
                    } // end of v6.1 parsing
                }

                // HPOC info
                tempd = (mxpack_dict_t*) mxpack_get_keyval(pd, "hpoc");
                port_cfg->hpoc_en = *((uint8_t*) mxpack_get_keyval(tempd, "enabled"));

                if(port_cfg->hpoc_en) {
                    // hpoc'd channel shape
                    templ = (mxpack_list_t*) mxpack_get_keyval(tempd, "shape");
                    switch(templ->dtype) {
                        case MXPACK_UINT8:
                            port_cfg->hpoc_dim_c = *((uint8_t*) mxpack_get_list_item_ptr(templ, 3));
                            break;
                        case MXPACK_UINT16:
                            port_cfg->hpoc_dim_c = *((uint16_t*) mxpack_get_list_item_ptr(templ, 3));
                            break;
                        case MXPACK_UINT32:
                            port_cfg->hpoc_dim_c = *((uint32_t*) mxpack_get_list_item_ptr(templ, 3));
                            break;
                        default:
                            // ERROR
                            mxpack_free_dict(&d);
                            throw(std::runtime_error("DFPv6 HPOC shape parse error"));
                            return -1;
                    }

                    // dummychan list
                    templ = (mxpack_list_t*) mxpack_get_keyval(tempd, "channels");
                    port_cfg->hpoc_list_length = templ->num_elem;
                    port_cfg->hpoc_dummy_channels = (uint16_t*) malloc(templ->num_elem * sizeof(uint16_t));

                    for(unsigned int z = 0; z < port_cfg->hpoc_list_length; z++) {
                        switch(templ->dtype) {
                            case MXPACK_UINT8:
                                port_cfg->hpoc_dummy_channels[z] = *((uint8_t*) mxpack_get_list_item_ptr(templ, z));
                                break;
                            case MXPACK_UINT16:
                                port_cfg->hpoc_dummy_channels[z] = *((uint16_t*) mxpack_get_list_item_ptr(templ, z));
                                break;
                            case MXPACK_UINT32:
                                port_cfg->hpoc_dummy_channels[z] = *((uint32_t*) mxpack_get_list_item_ptr(templ, z));
                                break;
                            default:
                                // ERROR
                                mxpack_free_dict(&d);
                                throw(std::runtime_error("DFPv6 HPOC dummy channel list parse error"));
                                return -1;
                        }
                    }

                }
                else {
                    // HPOC disabled
                    port_cfg->hpoc_dummy_channels = nullptr;
                }


                // set total size based on hpoc or not
                if(port_cfg->hpoc_en) {
                    port_cfg->total_size = ( port_cfg->dim_h
                                             * port_cfg->dim_w
                                             * port_cfg->dim_z
                                             * port_cfg->hpoc_dim_c );
                }
                else {
                    port_cfg->total_size = ( port_cfg->dim_h
                                             * port_cfg->dim_w
                                             * port_cfg->dim_z
                                             * port_cfg->dim_c );
                }

                meta.num_used_outports += 1;
                while(meta.model_outports.size() <= port_cfg->model_index) {
                    std::vector<uint8_t> vect;
                    meta.model_outports.push_back(vect);
                }
                meta.model_outports[port_cfg->model_index].push_back(port_cfg->port);
            }
            else {
                // INACTIVE outports skipped
                port_cfg->active = 0;
                port_cfg->hpoc_en = 0;
                port_cfg->hpoc_list_length = 0;
                port_cfg->hpoc_dummy_channels = nullptr;
                port_cfg->hpoc_dim_c = 0;
                port_cfg->layer_name = nullptr;
            }
        }

        dfpCacheEntry.output_port_number = meta.num_outports;
        dfpCacheEntry.pOuputConfigList = (MemxDfpPortConfig*)calloc(meta.num_outports, sizeof(MemxDfpPortConfig));
        for (uint8_t i = 0; i < meta.num_outports; i++) {
            port_cfg = &(oports[i]);
            MemxDfpPortConfig* pOutputCfg = &(dfpCacheEntry.pOuputConfigList[i]);


            pOutputCfg->port = port_cfg->port;
            pOutputCfg->active = port_cfg->active;

            if (pOutputCfg->active) {
                // port_set
                pOutputCfg->port_set = port_cfg->port_set;

                // mpu_id
                pOutputCfg->mpu_id = port_cfg->mpu_id;

                // model_index
                pOutputCfg->model_index = port_cfg->model_index;

                // format
                pOutputCfg->format = port_cfg->format;

                // shape
                pOutputCfg->dim_x = port_cfg->dim_h;
                pOutputCfg->dim_y = port_cfg->dim_w;
                pOutputCfg->dim_z = port_cfg->dim_z;
                pOutputCfg->dim_c = port_cfg->dim_c;

                //HPOC Stuff
                pOutputCfg->hpoc_en = port_cfg->hpoc_en;

                if (pOutputCfg->hpoc_en) {
                    // hpoc'd channel shape
                    pOutputCfg->hpoc_dim_c = port_cfg->hpoc_dim_c;

                    // dummychan list
                    pOutputCfg->hpoc_list_length = port_cfg->hpoc_list_length;
                    pOutputCfg->hpoc_dummy_channels = (uint16_t*)calloc(pOutputCfg->hpoc_list_length, sizeof(uint16_t));

                    for (uint8_t z = 0; z < pOutputCfg->hpoc_list_length; z++) {
                        pOutputCfg->hpoc_dummy_channels[z] = port_cfg->hpoc_dummy_channels[z];
                    }
                }
            } // if enabled

        } // for all port
        // sanity check
        if(meta.model_inports.size() != meta.model_outports.size()) {
            mxpack_free_dict(&d);
            throw(std::runtime_error("DFPv6 # of input model_idxs != # of output model_idxs"));
            return -1;
        }

        // clean exit
        mxpack_free_dict(&d);

    }
    //------------------------------------------------------------------
    // invalid file or ancient DFP
    else {
        throw(std::runtime_error("Invalid file or unsupported DFP version"));
        return -1;
    }


    return 0;

}

int DfpObject::__load_dfp_file(const char* f)
{

    FILE* fp = nullptr;
    src_file_path = std::string(f);

    fp = fopen(f, "rb");

    if(fp == nullptr) {
        throw(std::runtime_error("Invalid file or unsupported DFP version"));
        return -1;
    }

    // get size and alloc
    fseek(fp, 0L, SEEK_END);
    size_t sz = ftell(fp);
    dfp_byte_size = sz;

    uint8_t* all = new uint8_t[sz];

    // read all the data
    fseek(fp, 0L, SEEK_SET);
    if(fread(all, sz, 1, fp) != 1) {
        fclose(fp);
        delete[] all;
        src_dfp_bytes = nullptr;
        return -1;
    }

    // close and call load_dfp_bytes
    fclose(fp);
    fp = nullptr;

    int retval = __load_dfp_bytes(all);

    return retval;
}

void DfpObject::set_device_ids(const std::vector<int> &device_ids)
{
    device_ids_to_use = device_ids;
}


// Input -->
// shape: (on which transforms has to performed)
// shape_shift_info as stored in the input/output portinfo
// inport = true (if input port) else false
// Output -->
// returns indices after performing folded tranposes/reshpaes/(add&remove dims as required)
std::vector<unsigned int> PortInfo::compute_index_mapping(std::vector<uint32_t> shape, const PortInfo::shape_shift_info_t &shape_shift_info, bool inport)
{

    // Internal helper: generate all indices for a given shape (Cartesian product)
    // given shape = {2,3}, the fn will return all coordinate indices for a 2×3 array
    // i.e { {0,0}, {0,1}, {0,2}, {1,0}, {1,1}, {1,2} }
    auto generate_indices = [](const std::vector<int> &shape) {
        std::vector<std::vector<int>> result;
        std::vector<int> current(shape.size(), 0);

        // Recursive lambda via self-passing
        auto backtrack = [&](auto &&self, unsigned int depth) -> void {
            if (depth == shape.size())
            {
                result.push_back(current);
                return;
            }
            for (int i = 0; i < shape[depth]; ++i)
            {
                current[depth] = i;
                self(self, depth + 1);  // Recursively call itself
            }
        };

        backtrack(backtrack, 0);  // Initial call
        return result;
    };

    // Internal helper: transpose shape and get flat indices mapping
    auto transpose_general = [&](const std::vector<uint32_t> &shape, const std::vector<uint32_t> &permute) {
        std::vector<int> transposed_shape(permute.size());
        for (unsigned int i = 0; i < permute.size(); ++i) {
            transposed_shape[i] = shape[permute[i]];
        }

        // Build reverse map from original axis to position in permute
        std::vector<unsigned int> map_idx(shape.size());
        for (unsigned int idx = 0; idx < permute.size(); ++idx) {
            map_idx[permute[idx]] = idx;
        }

        // Compute strides for original shape
        int ndim = shape.size();
        std::vector<int> strides(ndim, 1);
        for (int i = ndim - 2; i >= 0; --i) {
            strides[i] = strides[i + 1] * shape[i + 1];
        }

        // Generate all index tuples in transposed space
        std::vector<std::vector<int>> transposed_indices = generate_indices(transposed_shape);

        // Map back to flat indices
        std::vector<unsigned int> permuted_indices;
        for (const auto &transposed_idx : transposed_indices) {
            std::vector<unsigned int> original_idx(ndim);
            for (int original_axis = 0; original_axis < ndim; ++original_axis) {
                int transposed_axis = map_idx[original_axis];
                original_idx[original_axis] = transposed_idx[transposed_axis];
            }

            int flat_index = 0;
            for (int i = 0; i < ndim; ++i) {
                flat_index += original_idx[i] * strides[i];
            }

            permuted_indices.push_back(flat_index);
        }

        return permuted_indices;
    };

    const std::vector <std::string> op_lst         = shape_shift_info.folded_optype;
    const std::vector <std::vector<int>> op_shapes = shape_shift_info.folded_opshape;
    bool first_transpose = true;

    // Initialize indices (for cases when there are no transposes or reshapes)
    // Compute total number of elements
    unsigned int total_size = 1;
    for (int dim : shape) {
        total_size *= (unsigned int)dim;
    }

    //Generate linear indices: 0 .. total_size-1
    std::vector<unsigned int> indices(total_size);
    for (unsigned int i = 0; i < total_size; ++i) {
        indices[i] = i;
    }

    // if it's an output port perform add and remove dims before folded ops
    if(!inport) {

        // then add and remove dims for shape should be handled here.
        // perform add operations only.
        std::vector<uint32_t> next_shape(shape.size());

        for (unsigned int i = 0; i < shape_shift_info.add.size(); i++) {
            // get the index from the shape_shift_info.add vector, and
            // add a singleton dimention to the current_shape
            int add_index = shape_shift_info.add[i];

            // add a singleton dimension at the add_index]
            next_shape.resize(shape.size() + 1);
            for (int j = 0; j < (int)shape.size() + 1; j++) {
                if (j < add_index) {
                    next_shape[j] = shape[j];
                }
                else if (j == add_index) {
                    next_shape[j] = 1; // singleton dimension
                }
                else {
                    next_shape[j] = shape[j - 1];
                }
            }

            // then set current_shape to next_shape and clear next_shape
            shape = next_shape;
            next_shape.clear();
        }

        // then do all sub operations ONLY
        for (unsigned int i = 0; i < shape_shift_info.remove.size(); i++) {
            // get the index from the shape_shift_info.remove vector, and
            // remove the dimension at that index from the current_shape
            int remove_index = shape_shift_info.remove[i] - i;

            // remove the dimension at the remove_index
            next_shape.resize(shape.size() - 1);
            for (int j = 0; j < (int)shape.size(); j++) {
                if (j < remove_index) {
                    next_shape[j] = shape[j];
                }
                else if (j > remove_index) {
                    next_shape[j - 1] = shape[j];
                }
            }

            // then set current_shape to next_shape and clear next_shape
            shape = next_shape;
            next_shape.clear();

        }
    }

    for (unsigned int idx = 0; idx < op_lst.size(); ++idx) {
        const std::string &op = op_lst[idx];
        std::vector<uint32_t> op_shape(op_shapes[idx].begin(), op_shapes[idx].end()); // because shape is of type uint32_t

        if (op == "reshape") {
            shape = op_shape;
        }
        else if (op == "transpose" && first_transpose) {
            indices = transpose_general(shape, op_shape);
            std::vector<uint32_t> new_shape;
            for (unsigned int i = 0; i < op_shape.size(); ++i) {
                new_shape.push_back(shape[op_shape[i]]);
            }
            shape = new_shape;
            first_transpose = false;
        }
        else if (op == "transpose") {
            std::vector<unsigned int> transpose_indices = transpose_general(shape, op_shape);
            std::vector<unsigned int> new_indices(indices.size());
            for (unsigned int i = 0; i < indices.size(); ++i) {
                new_indices[i] = indices[transpose_indices[i]];
            }
            indices = new_indices;
            std::vector<uint32_t> new_shape;
            for (unsigned i = 0; i < op_shape.size(); ++i) {
                new_shape.push_back(shape[op_shape[i]]);
            }
            shape = new_shape;
        }
    }

    return indices;
}

