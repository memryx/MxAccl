## original cmake
cmake_minimum_required(VERSION 3.14)
project(mxapi)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_STANDARD 17)

#####################################################################################################
# CFLAGS same as uppper mx_accl CMakeLists
set(CMAKE_COMMON_CFLAGS "-Wall -Wextra -pipe -fPIC -O2 -fvect-cost-model=dynamic -fsimd-cost-model=unlimited -floop-unroll-and-jam -ftree-loop-if-convert -ftree-loop-im -ftree-loop-ivcanon -funswitch-loops -fgcse-after-reload -fipa-cp-clone -ftree-loop-distribution -ftree-partial-pre -fsplit-paths -fpredictive-commoning -fno-math-errno -funsafe-math-optimizations -ffinite-math-only -fno-signed-zeros -fno-trapping-math -fno-signaling-nans -fcx-limited-range -fopenmp -Wno-unused-variable -Wno-alloc-size-larger-than")

set(CMAKE_X86_FLAGS_BASE "-mpopcnt -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mfxsr -mcx16 -msahf -mpclmul -mfsgsbase -mmovbe -mprfchw -mxsave -mxsaveopt -mtune=generic")
set(CMAKE_X86_FLAGS_AVX2 "-mavx -mavx2 -mfma -mbmi -mbmi2 -maes -mf16c -mlzcnt -madx -mxsavec -mxsaves -mclflushopt")

if(CMAKE_BUILD_TYPE MATCHES "Debug")
  set(
    CMAKE_C_FLAGS
    "${CMAKE_C_FLAGS} -Wall -Wextra -fPIC -O0 -g -fopenmp -fno-omit-frame-pointer -fsanitize=null -fsanitize=bounds -fsanitize=bounds-strict -Wno-unused-variable"
  )
  set(
    CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC -O0 -g -fopenmp -fno-omit-frame-pointer -fsanitize=null -fsanitize=bounds -fsanitize=bounds-strict -Wno-unused-variable"
  )
else()
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)|(x86_64)|(X86_64)")
    set(
      CMAKE_C_FLAGS
      "${CMAKE_C_FLAGS} ${CMAKE_COMMON_CFLAGS} ${CMAKE_X86_FLAGS_BASE} ${CMAKE_X86_FLAGS_AVX2}"
    )
    set(
      CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} ${CMAKE_COMMON_CFLAGS} ${CMAKE_X86_FLAGS_BASE} ${CMAKE_X86_FLAGS_AVX2}"
    )
  elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64)|(AARCH64)|(arm64)|(ARM64)")
    set(
      CMAKE_C_FLAGS
      "${CMAKE_C_FLAGS} ${CMAKE_COMMON_CFLAGS} -march=armv8-a+simd -mtune=cortex-a76"
    )
    set(
      CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} ${CMAKE_COMMON_CFLAGS} -march=armv8-a+simd -mtune=cortex-a76"
    )
  elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "(riscv64)|(RISCV64)|(RV64)|(rv64gc)")
    set(
      CMAKE_C_FLAGS
      "${CMAKE_C_FLAGS} ${CMAKE_COMMON_CFLAGS} -march=rv64gc_zba_zbb_zbs"
    )
    set(
      CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} ${CMAKE_COMMON_CFLAGS} -march=rv64gc_zba_zbb_zbs"
    )
  endif()
endif()

#####################################################################################################


# You can also use find_package(pybind11 REQUIRED) if installed system-wide
add_subdirectory(pybind11)  # Clone from https://github.com/pybind/pybind11

# This will automatically name the output module "mxapi.<python_abi_tag>.so"
pybind11_add_module(mxapi bindings.cpp)

# find the path to the site-packages/numpy/core/include/ directory for the currently
# active Python environment, and add that to the target_include_directories
execute_process(
    COMMAND python3 -c "import numpy; print(numpy.get_include())"
    OUTPUT_VARIABLE PYTHON_NUMPY_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Include directories
target_include_directories(mxapi PRIVATE
                           ${PYTHON_NUMPY_INCLUDE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/spdlog
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_link_libraries(mxapi PRIVATE mx_accl)

# Set RPATH for the build tree so the executable can find 
# the 'mx_accl' shared library without manual LD_LIBRARY_PATH exports.
set_target_properties(mxapi PROPERTIES
    BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/../../build/mx_accl"
)
