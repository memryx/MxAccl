// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifdef OS_LINUX
    #include <dlfcn.h>
#else
    #include <windows.h>
#endif
#include <sstream>
#include <cstdlib>
#include <unordered_set>

#include <memx/accl/prepost.h>


std::vector<std::string> prepost_split(const std::string &str)
{
    std::vector<std::string> prefixes;
    size_t start = 0;
    char delimiter = ':';
    size_t end = str.find(delimiter);

    while (end != std::string::npos) {
        prefixes.push_back(str.substr(start, end - start) + "/");
        start = end + 1;
        end = str.find(delimiter, start);
    }

    // Add the last token
    prefixes.push_back(str.substr(start, end) + "/");

    return prefixes;
}

#ifdef OS_LINUX
PrePost* createObject(std::string soFileName, std::string functionName, std::string model_path,
                      const std::vector<size_t> &out_sizes, PluginType type)
{
    // Load the shared object file
    const char* LD_LIBRARY_PATH = std::getenv("LD_LIBRARY_PATH");
    std::vector<std::string> ld_paths;
    if(LD_LIBRARY_PATH) {
        ld_paths = prepost_split(std::string(LD_LIBRARY_PATH));
    }
    soFileName += ".so";
    std::vector<std::string> library_paths{"/opt/memryx/accl-plugins/", "/usr/lib/", "/lib/x86_64-linux-gnu/", "/lib/aarch64-linux-gnu/"};
    library_paths.insert(library_paths.end(), ld_paths.begin(), ld_paths.end());
    void* handle = nullptr;
    for(std::string prefix : library_paths) {
        std::filesystem::path soFilePath = prefix + soFileName;
        handle = dlopen(soFilePath.c_str(), RTLD_NOW);
        if(handle) {
            break;
        }
        else if(std::filesystem::exists(soFilePath)) {
            if(type != Plugin_Tflite) {
                throw std::runtime_error(std::string(dlerror()));
            }
            else {
                throw std::runtime_error(std::string(dlerror()) +
                                         std::string(".\n\n tflite is not installed by default with memx-accl installation for older linux versions. Please refer this link to install the tflite depedency, https://developer.memryx.com/docs/tutorials/requirements/installation.html#tflite\n"));
            }
        }
    }

    if(!handle) {
        throw std::runtime_error("Failed to load shared object: " + std::string(soFileName) + ". Try to `apt install memx-accl-plugins`");
    }

    // Get the function pointer for creating objects from the shared object
    CreatePlugin createFunc = reinterpret_cast<CreatePlugin>(dlsym(handle, functionName.c_str()));
    if (!createFunc) {
        dlclose(handle);
        throw std::runtime_error("Couldn't load the function: " + functionName);
    }

    // Call the function to create the object
    PrePost* obj = createFunc(model_path.c_str(), out_sizes);

    return obj;
}

#else
PrePost* createObject(std::string soFileName, std::string functionName, std::string model_path,
                      const std::vector<size_t> &out_sizes, PluginType type )
{
    // Load the shared object file
    soFileName += ".dll";
    HMODULE handle = LoadLibraryA(soFileName.c_str());
    if (!handle) {
        std::cerr << "Failed to load shared object: " << soFileName  << std::endl;
        return nullptr;
    }

    // Get the function pointer for creating objects from the shared object
    CreatePlugin createFunc = reinterpret_cast<CreatePlugin>(GetProcAddress(handle, functionName.c_str()));
    if (!createFunc) {
        std::cerr << "Failed to get symbol: " << functionName << std::endl;
        FreeLibrary(handle);
        return nullptr;
    }

    // Call the function to create the object
    PrePost* obj = createFunc(model_path.c_str(), out_sizes);

    return obj;
}
#endif

PrePost* mx_create_prepost(std::filesystem::path model_path_, const std::vector<size_t> &out_sizes)
{
    PrePost* obj = NULL;
    std::string model_path = model_path_.string();
    size_t lastDotPos = model_path.find_last_of(".");
    std::string extension = model_path.substr(lastDotPos + 1);
    if(extension == "onnx") {
        std::string plugin_path = "libonnxinfer";
        obj = createObject(plugin_path, "createOnnx", model_path, out_sizes, Plugin_Onnx);
        obj->type = Plugin_Onnx;
    }
    else if(extension == "tflite") {
        std::string plugin_path = "libtfliteinfer";
        obj = createObject(plugin_path, "createTflite", model_path, out_sizes, Plugin_Tflite);
        obj->type = Plugin_Tflite;
    }
    else if(extension == "pb") {
        std::string plugin_path = "libtfinfer";
        obj = createObject(plugin_path, "createTf", model_path, out_sizes, Plugin_Tf);
        obj->type = Plugin_Tf;
    }
    else if(extension == "keras") {
        std::ostringstream oss;
        oss << "The connected model, " << model_path << " is a keras model which is not supported by MX_API as ";
        oss << "keras doesn't have a C++ API. So we suggest you to convert the model to Tflite and instructions ";
        oss << "can be found here, https://www.tensorflow.org/lite/models/convert/convert_models#convert_a_keras_model_";
        throw std::runtime_error(oss.str());
    }
    else {
        throw(std::runtime_error("Unknown file extension passed for pre-processing or post-processing"));
    }
    return obj;
}

PrePost::PrePost(): dfp_pattern(0), real_featuremaps(0)
{
}

void PrePost::match_names(std::vector<std::string> dfp_names, ProcessType prc_type)
{
    if(prc_type == Process_Pre) {
        std::vector<std::string> model_names = this->get_output_names();
        int dfp_size = static_cast<int>(dfp_names.size());
        int model_size = static_cast<int>(model_names.size());
        if(model_size > dfp_size) {
            throw std::logic_error("output size of pre-processing model is greater than input size of dfp");
        }
        std::unordered_set<int> dfp_exisits;
        for(int i = 0; i < model_size; ++i) {
            for(int j = 0; j < dfp_size; ++j) {
                if(model_names[i] == dfp_names[j]) {
                    dfp_pattern.push_back(j);
                    dfp_exisits.insert(j);
                    break;
                }
            }
        }

        if(static_cast<int>(dfp_pattern.size()) != model_size) {
            throw std::logic_error("pre-processing model output names don't match dfp input names");
        }

        for(int i = 0; i < dfp_size; ++i) {
            if(dfp_exisits.find(i) == dfp_exisits.end()) {
                real_featuremaps.push_back(i);
            }
        }
    }
    else {
        std::vector<std::string> model_names = this->get_input_names();
        int dfp_size = static_cast<int>(dfp_names.size());
        int model_size = static_cast<int>(model_names.size());

        if(model_size > dfp_size) {
            throw std::logic_error("input size of post-processing model is greater than output size of dfp");
        }

        std::unordered_set<int> dfp_exisits;
        for(int i = 0; i < model_size; ++i) {
            for(int j = 0; j < dfp_size; ++j) {
                if(model_names[i] == dfp_names[j]) {
                    dfp_pattern.push_back(j);
                    dfp_exisits.insert(j);
                    break;
                }
            }
        }

        if(static_cast<int>(dfp_pattern.size()) != model_size) {
            throw std::logic_error("post-processing model input names don't match dfp output names");
        }

        if(dfp_size > model_size) {
            for(int i = 0; i < dfp_size; ++i) {
                if(dfp_exisits.find(i) == dfp_exisits.end()) {
                    real_featuremaps.push_back(i);
                }
            }
        }
    }
}
