<picture>
  <source srcset="figures/mx_accl.png" media="(prefers-color-scheme: dark)">
  <source srcset="figures/mx_accl_light.png" media="(prefers-color-scheme: light)">
  <img src="figures/mx_accl_light.png" alt="MemryX Accl">
</picture>


<!-- Badges for quick project insights -->
[![MemryX SDK](https://img.shields.io/badge/MemryX%20SDK-2.2-brightgreen)](https://developer.memryx.com)
[![C++](https://img.shields.io/badge/C++-17-blue)](https://en.cppreference.com)


# MemryX Accl
The MxAccl repository provides the **open-source code** for both the `mx_accl` runtime library, `mxa-manager` daemon, and the `acclBench` benchmarking tool. These components enable seamless integration and performance measurement of C++ applications using the MemryX MX3 accelerator.

### MxAccl Library
The `mx_accl` library is designed to efficiently handle multi-model and multi-input streams. It offers:
- **Auto-threading Mode**: Where the library manages send and receive threads automatically, simplifying usage.
- **Manual-threading Mode**: Where the user manually creates and manages threads, providing greater control and customization.

This architecture helps maximize the performance of the MX3’s pipelined dataflow system, while also allowing users to manage resources and threading as needed.

### AcclBench Tool
The `acclBench` command line interface tool provides an easy way to benchmark model performance on the MX3 accelerator. It measures the latency and FPS of inference operations, capturing the performance from the host side, which includes driver and interface time. `acclBench` supports both single and multi-stream scenarios and is built using the high-performance MxAccl C++ API.


### Full Documentation
For detailed information on using the `mx_accl` library and `acclBench` tool, please visit the MemryX Developer Hub:
- **[MxAccl Library Documentation](https://developer.memryx.com/api/accelerator/accelerator.html)**
- **[Benchmark Tools Documentation](https://developer.memryx.com/tools/benchmark/benchmark.html)**

> **Note**: If you are looking for the **Python API** for the MemryX MX3 accelerator, please visit the [Developer Hub](https://developer.memryx.com/api/accelerator/accelerator.html) for documentation and usage examples. This repository only contains the **C++ version** of the library.


### Repository Overview
This repository contains the source code for the core `mx_accl` library and associated tools. These components are typically pre-built and packaged as `memx-accl` within the MemryX SDK.

| Folder                               | Description                                                                                                                  |
| -------------------------------------| ---------------------------------------------------------------------------------------------------------------------        |
| `mx_accl`                            | Core MxAccl runtime library code
| `mx_accl/pymodule`                   | Python bindings for the MxAccl library
| `mxa_manager`                        | MXA-Manager daemon and config files
| `tools`                              | Utilities like acclBench
| `misc`                               | Copies of the binary `libmemx.so` library, used for building Yocto packages separately

> **IMPORTANT**: For most users, we highly recommend using the prebuilt `memx-accl` package provided by the  [MemryX SDK](https://developer.memryx.com), as it simplifies development and ensures all dependencies are properly managed.

## Recommended Installation: MemryX SDK

To simplify development and avoid building from source, install the `memx-accl` package through the MemryX SDK. The SDK includes precompiled libraries, drivers, and tools optimized for MemryX accelerators. Follow the **[installation guide](https://developer.memryx.com/get_started/)** for step-by-step instructions.

The **[MemryX Developer Hub](https://developer.memryx.com)** provides comprehensive documentation, tutorials, and examples to get you started quickly.

The **[Runtime section](https://developer.memryx.com/runtime/index.html)** of the DevHub also has deeper details about this MxAccl library.

## Advanced Installation: Building from Source

For advanced users who prefer to build the MxAccl library from source, follow these instructions:

### Step 0: Install memx-drivers

If you haven't already, install the MemryX drivers and runtime libraries by following the instructions in the [MemryX SDK Installation Guide](https://developer.memryx.com/get_started/install_software.html).

### Step 1: Clone the repository

``` bash
git clone https://github.com/memryx/MxAccl.git
```

### Step 2: Build MxAccl

```bash
mkdir build && cd build

cmake ..
make -j$(nproc)
```

### (Optional) Step 3: Build Python Bindings

```bash
# activate your Python venv with numpy, etc. installed
cd ../mx_accl/pymodule
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Usage

### MxAccl
The typical use of `MxAccl` is to integrate it directly into your C++ application to manage model inference on the MemryX MX3 accelerator. For complete documentation and detailed integration tutorials, visit the [MxAccl Documentation](https://developer.memryx.com/api/accelerator/accelerator.html) and [Tutorials Page](https://developer.memryx.com/tutorials/tutorials.html).

#### mxa_manager

The `mxa_manager` daemon is responsible for managing the MX3 accelerator and its resources. See its [documentation here](https://developer.memryx.com/runtime/usage/shared_local.html).

### acclBench

To measure the performance (latency and FPS) of a model on the MemryX MX3 accelerator, use the `acclBench` command line tool. Here's how to get started:


#### Step 1: Obtain a DFP Model File
You can download a precompiled MobileNet DFP file from the link below:

- [MobileNet DFP Download](https://developer.memryx.com/_downloads/2925dc76b5b06046a0f3d66815b1ae5e/mobilenet.dfp)

For more information on creating and using DFP files, refer to the [Hello MXA Tutorial](https://developer.memryx.com/get_started/hello_mxa.html).


#### Step 2: Run acclBench
Once you have the DFP file and have successfully installed the MemryX SDK, drivers, and runtime libraries, navigate to the directory where you downloaded the MobileNet DFP file and run:

```bash
acclBench -d mobilenet.dfp -f 1000
```

##### Explanation of the Command

* `-d mobilenet.dfp`: Specifies the DFP model file to be used for benchmarking.
* `-f 1000`: Sets the number of frames for testing inference performance. The default is 1000 frames.

## Pre/Post Plugins

For Onnx, Tensorflow, and TFLite pre/post plugins, refer to the [MxUtils](https://github.com/memryx/MxUtils) repository. These plugins are packaged separately to minimize dependencies for the core MxAccl library and are only required if pre/post models are used.

## License
MxAccl is Free and open-source software under the [MPL-2.0 License](LICENSE-MPL-2.0).

#### Third-Party
See the LICENSE.md files within each `extern/` for [asio](https://think-async.com/Asio/), [cpuinfo](https://github.com/pytorch/cpuinfo), [pybind11](https://github.com/pybind/pybind11), and [spdlog](https://github.com/gabime/spdlog) licenses, all permissive open-source.


## See Also
Enhance your experience with MemryX solutions by exploring the following resources:

- **[Developer Hub](https://developer.memryx.com/index.html):** Access comprehensive documentation for MemryX hardware and software.
- **[MemryX SDK Installation Guide](https://developer.memryx.com/get_started/install.html):** Learn how to set up essential tools and drivers to start using MemryX accelerators.
- **[Tutorials](https://developer.memryx.com/tutorials/tutorials.html):** Follow detailed, step-by-step instructions for various use cases and applications.
- **[Model Explorer](https://developer.memryx.com/model_explorer/models.html):** Discover and explore models that have been compiled and optimized for MemryX accelerators.
- **[Examples](https://github.com/memryx/MemryX_eXamples):** Explore a collection of end-to-end AI applications powered by MemryX hardware and software.
