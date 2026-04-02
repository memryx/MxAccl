cmake_minimum_required(VERSION 3.15)
project(mxa_manager)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# If MX_API_DIR is not set externally, default it to one level up
if (NOT DEFINED MX_API_DIR)
    set(MX_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
    message(STATUS "MX_API_DIR not defined: defaulting to ${MX_API_DIR}")
endif()

# If MX_API_HOME_DIR is not set externally, default it to one level up
if (NOT DEFINED MX_API_HOME_DIR)
    set(MX_API_HOME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
    message(STATUS "MX_API_HOME_DIR not defined: defaulting to ${MX_API_HOME_DIR}")
endif()

# Collect source files
file(GLOB_RECURSE mgr_src_files src/*.cpp)

# Platform-specific settings
if (WIN32)
    add_executable(mxa_manager
        main_win.cpp
        ${mgr_src_files}
    )
    target_compile_definitions(mxa_manager PRIVATE
        PLATFORM_WINDOWS
        WIN32_LEAN_AND_MEAN
    )
    if (MSVC)
        target_compile_options(mxa_manager PRIVATE /utf-8)
    endif()

    find_library(MXACCL_DYNAMIC
        NAMES memx-accl
        PATHS
            "C:/Program Files/MemryX/lib"
        NO_DEFAULT_PATH
    )
    if (NOT MXACCL_DYNAMIC)
        message(FATAL_ERROR "Cannot find memx-accl.lib")
    endif()

    find_library(UDRIVER_LIB
        NAMES udriver
        PATHS
            "C:/Program Files/MemryX/lib"
        NO_DEFAULT_PATH
    )
    if (NOT UDRIVER_LIB)
        message(FATAL_ERROR "Cannot find udriver.lib")
    endif()

    target_link_libraries(mxa_manager PRIVATE
        ws2_32
        advapi32
        ${MXACCL_DYNAMIC}
        ${UDRIVER_LIB}
    )


else()
    add_executable(mxa_manager
            main_linux.cpp
            ${mgr_src_files}
    )
    target_compile_definitions(mxa_manager PRIVATE PLATFORM_LINUX)

    target_link_libraries(mxa_manager PRIVATE
        ${MXACCL_DYNAMIC}
        memx
        cpuinfo
    )
endif()

# Include directories
target_include_directories(mxa_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MX_API_DIR}/include
    ${MX_API_DIR}/mx_accl/include
    ${MX_API_HOME_DIR}/extern/spdlog
    ${MX_API_HOME_DIR}/extern/asio
    ${MX_API_HOME_DIR}/extern/cpuinfo/include
)

# Optimization flags for non-Debug builds
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (MSVC)
        # Enable Link-Time Code Generation on MSVC
        target_compile_options(mxa_manager PRIVATE /GL)
        target_link_options(mxa_manager PRIVATE /LTCG)
    else()
        # Enable LTO on GCC/Clang
        target_compile_options(mxa_manager PRIVATE -fno-pic -fPIE -flto -fno-semantic-interposition)
        target_link_options(mxa_manager PRIVATE -fno-pic -fPIE -flto -fno-semantic-interposition)
endif()
endif()

# Service Control and Event Source Registration on Windows
# Usage (must be run with Administrator privileges):
#   cmake --build . --config Release --target register_service
#   cmake --build . --config Release --target unregister_service
#   cmake --build . --config Release --target start_service
if (WIN32)
    set(SVC_NAME "MxaManagerSvc")
    set(SVC_EXE_PATH "$<TARGET_FILE:mxa_manager>")
    set(EVENT_SOURCE_KEY "HKLM\\SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\${SVC_NAME}")

    # Target to register the service AND its event source in the registry
    add_custom_target(register_service
        # Create the service
        COMMAND sc.exe create ${SVC_NAME} binPath= "\"${SVC_EXE_PATH}\"" start= auto
        # Add the registry key for the event source
        COMMAND reg.exe add "${EVENT_SOURCE_KEY}" /v EventMessageFile /t REG_SZ /d "${SVC_EXE_PATH}" /f
        # Add the supported event types value
        COMMAND reg.exe add "${EVENT_SOURCE_KEY}" /v TypesSupported /t REG_DWORD /d 7 /f
        COMMENT "Registering Windows service '${SVC_NAME}' and its Event Source."
        VERBATIM
    )

    # Target to unregister the service AND remove its event source from the registry
    add_custom_target(unregister_service
        # Stop the service if it's running (ignore errors if it's not)
        COMMAND sc.exe stop ${SVC_NAME}
        # Delete the service
        COMMAND sc.exe delete ${SVC_NAME}
        # Delete the registry key for the event source
        COMMAND reg.exe delete "${EVENT_SOURCE_KEY}" /f
        COMMENT "Unregistering Windows service '${SVC_NAME}' and its Event Source."
        VERBATIM
    )

    # Target to start the service
    add_custom_target(start_service
        COMMAND sc.exe start ${SVC_NAME}
        COMMENT "Starting Windows service '${SVC_NAME}'"
        VERBATIM
    )
endif()
