cmake_minimum_required(VERSION 3.10)

get_filename_component(app_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${MX_API_DIR}/include)

file(GLOB local_src
    "*.c"
    "*.cpp"
	)

add_executable(${app_name} ${local_src})

target_link_libraries(${app_name} mx_accl )
