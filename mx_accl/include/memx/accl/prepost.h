// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_PREPOST_H
#define MX_PREPOST_H

#pragma once
#include <vector>
#include <string>
#include <stdint.h>

#include <memx/memx.h>

#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/path.h>

enum MEMX_API_EXPORT PluginType {
    Plugin_Onnx,
    Plugin_Tflite,
    Plugin_Tf,
    Plugin_None
};

enum MEMX_API_EXPORT ProcessType {
    Process_Pre,
    Process_Post
};
class PrePost
{
  public:
    MEMX_API_EXPORT virtual void runinference(std::vector<MX::Types::FeatureMap*> input,
            std::vector<MX::Types::FeatureMap*> output) = 0;
    MEMX_API_EXPORT virtual std::vector<std::vector<int64_t>> get_output_shapes() = 0;
    MEMX_API_EXPORT virtual std::vector<std::vector<int64_t>> get_input_shapes() = 0;
    MEMX_API_EXPORT virtual std::vector<size_t> get_output_sizes() = 0;
    MEMX_API_EXPORT virtual std::vector<size_t> get_input_sizes() = 0;
    MEMX_API_EXPORT virtual std::vector<std::string> get_input_names() = 0;
    MEMX_API_EXPORT virtual std::vector<std::string> get_output_names() = 0;
    MEMX_API_EXPORT virtual ~PrePost() {};
    PluginType type = Plugin_None;
    bool dynamic_output = false;
    std::vector<int> dfp_pattern;
    std::vector<int> real_featuremaps;
    MEMX_API_EXPORT void match_names(std::vector<std::string> dfp_names, ProcessType prc_type);
    MEMX_API_EXPORT PrePost();
  private:
};

typedef PrePost* (*CreatePlugin)(const char* model_path, const std::vector<size_t> &out_sizes);

MEMX_API_EXPORT PrePost* mx_create_prepost(std::filesystem::path model_path, const std::vector<size_t> &out_sizes = {});

MEMX_API_EXPORT std::vector<std::string> prepost_split(const std::string &str);
MEMX_API_EXPORT PrePost* createObject(std::string soFileName, std::string functionName, std::string model_path,
                                      const std::vector<size_t> &out_sizes, PluginType type);

#endif
