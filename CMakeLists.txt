cmake_minimum_required(VERSION 3.13)

project(MX_API)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_STANDARD 17)

option(NO_AVX "Disable AVX/AVX2 options for x86 builds" OFF)
option(BUILD_TOOLS "Build the acclBench executable" ON)
option(DISABLE_SAN "Only useful when BUILD_TYPE is 'Debug'. Disable sanitizer such as AddressSanitizer (ASan), LeakSanitizer (LSan), ... " OFF)


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_compile_definitions(OS_LINUX)
endif()

get_filename_component(MX_API_HOME_DIR "." REALPATH)
message(STATUS "MX_API_HOME_DIR  set to " ${MX_API_HOME_DIR})
get_filename_component(MX_API_DIR "mx_accl" REALPATH)
message(STATUS "MX_API_DIR  set to " ${MX_API_DIR})
get_filename_component(MX_API_BUILD_DIR "build" REALPATH)
message(STATUS "MX_API_BUILD_DIR  set to " ${MX_API_BUILD_DIR})

set(CMAKE_COMMON_CFLAGS "-Wall -Wextra -pipe -fPIC -O2 -fvect-cost-model=dynamic -fsimd-cost-model=unlimited -floop-unroll-and-jam -ftree-loop-if-convert -ftree-loop-im -ftree-loop-ivcanon -funswitch-loops -fgcse-after-reload -fipa-cp-clone -ftree-loop-distribution -ftree-partial-pre -fsplit-paths -fpredictive-commoning -fno-math-errno -funsafe-math-optimizations -ffinite-math-only -fno-signed-zeros -fno-trapping-math -fno-signaling-nans -fcx-limited-range -fopenmp -Wno-unused-variable -Wno-alloc-size-larger-than")

set(CMAKE_X86_FLAGS_BASE "-mpopcnt -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mfxsr -mcx16 -msahf -mpclmul -mfsgsbase -mmovbe -mprfchw -mxsave -mxsaveopt -march=generic")
if(NO_AVX)
    set(CMAKE_X86_FLAGS_AVX2 "")
else()
    set(CMAKE_X86_FLAGS_AVX2 "-mavx -mavx2 -mfma -mbmi -mbmi2 -maes -mf16c -mlzcnt -madx -mxsavec -mxsaves -mclflushopt")
endif()

# Option to select build type (Release or Debug)
set(BUILD_TYPE "Release" CACHE STRING "Build type (Release or Debug or Packaging or Regression)")
if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting build type to '${BUILD_TYPE}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
endif()


if(CMAKE_BUILD_TYPE MATCHES "Debug")

  # Basic debug and common compilation flags
  set(BASIC_DEBUG_FLAGS "-Wall -Wextra -fPIC -O0 -g -fopenmp -fno-omit-frame-pointer -Wno-unused-variable")

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${BASIC_DEBUG_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${BASIC_DEBUG_FLAGS}")

  if(NOT DISABLE_SAN)
      # Sanitizer flags (only applied if enabled)
      set(SAN_FLAGS "-fsanitize=address -fsanitize=leak -fsanitize=null -fsanitize=bounds -fsanitize=bounds-strict -fsanitize=pointer-overflow")

      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SAN_FLAGS}")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SAN_FLAGS}")
  endif()


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
      "${CMAKE_C_FLAGS} ${CMAKE_COMMON_CFLAGS} -march=rv64gc_zicntr_ziccif_ziccrse_ziccamoa_za64rs_zba_zbb_zbs_zihpm_zihintpause_zic64b_zicbom_zicbop_zicboz_zkt_zfh_zicond_zbc"
    )
    set(
      CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} ${CMAKE_COMMON_CFLAGS} -march=rv64gc_zicntr_ziccif_ziccrse_ziccamoa_za64rs_zba_zbb_zbs_zihpm_zihintpause_zic64b_zicbom_zicbop_zicboz_zkt_zfh_zicond_zbc"
    )
  endif()
endif()

#define SUBDIR macro
MACRO(SUBDIRLIST result curdir)
  FILE(GLOB children RELATIVE ${curdir} ${curdir}/*)
  SET(dirlist "")
  FOREACH(child ${children})
    IF(IS_DIRECTORY ${curdir}/${child})
      LIST(APPEND dirlist ${child})
    ENDIF()
  ENDFOREACH()
  SET(${result} ${dirlist})
ENDMACRO()


set(MXACCL_DYNAMIC "mx_accl")

SUBDIRLIST(SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR} "*")

FOREACH(subdir ${SUBDIRS})
  if(${subdir} STREQUAL "tools" AND NOT BUILD_TOOLS)
    message(STATUS "Excluding 'tools' directory from build due to BUILD_TOOLS=OFF.")
  elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/CMakeLists.txt)
    ADD_SUBDIRECTORY(${subdir})
  endif()
ENDFOREACH()


# add the cpuinfo library
SET(CPUINFO_LIBRARY_TYPE "static" CACHE STRING "Type of cpuinfo library to build (static or shared)")
SET(CPUINFO_RUNTIME_TYPE "static" CACHE STRING "Type of runtime library (shared, static, or default) to use")
SET(CPUINFO_LOG_LEVEL "fatal" CACHE STRING "Minimum logging level (info with lower severity will be ignored)")
OPTION(CPUINFO_BUILD_TOOLS "Build command-line tools" OFF)
OPTION(CPUINFO_BUILD_BENCHMARKS "Build cpuinfo micro-benchmarks" OFF)
OPTION(CPUINFO_BUILD_PKG_CONFIG "Build pkg-config manifest" OFF)
OPTION(USE_SYSTEM_LIBS "Use system libraries instead of downloading and building them" OFF)
OPTION(USE_SYSTEM_GOOGLEBENCHMARK "Use system Google Benchmark library instead of downloading and building it" OFF)
add_subdirectory(extern/cpuinfo)

target_compile_options(cpuinfo PRIVATE -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-implicit-fallthrough)
target_compile_options(cpuinfo_internals PRIVATE -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-implicit-fallthrough)
