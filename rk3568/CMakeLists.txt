cmake_minimum_required(VERSION 3.10)
project(rk3568_edge_audio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RKNN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../deps/rknn_runtime_1.6.0" CACHE PATH
    "Directory containing rknn_api.h and lib/aarch64/librknnrt.so")
set(RKNN_INCLUDE "${RKNN_ROOT}/include")
set(RKNN_LIBRARY "${RKNN_ROOT}/lib/aarch64/librknnrt.so")

if(NOT EXISTS "${RKNN_INCLUDE}/rknn_api.h")
    message(FATAL_ERROR "Missing ${RKNN_INCLUDE}/rknn_api.h")
endif()
if(NOT EXISTS "${RKNN_LIBRARY}")
    message(FATAL_ERROR "Missing ${RKNN_LIBRARY}")
endif()

add_executable(audio_receiver audio_receiver.cpp)
target_include_directories(audio_receiver PRIVATE "${RKNN_INCLUDE}")
target_link_libraries(audio_receiver PRIVATE "${RKNN_LIBRARY}")
target_compile_options(audio_receiver PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(audio_receiver PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "\$ORIGIN/lib")

