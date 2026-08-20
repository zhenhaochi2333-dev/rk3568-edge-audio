cmake_minimum_required(VERSION 3.16)
project(rk3568_edge_audio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RKNN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../deps/rknn_runtime_2.3.2" CACHE PATH
    "Directory containing rknn_api.h and lib/aarch64/librknnrt.so")
set(SHERPA_ONNX_ROOT "" CACHE PATH
    "sherpa-onnx install prefix containing include/ and lib/")

set(EDGEAUDIO_HAS_RKNN OFF)
if(UNIX AND EXISTS "${RKNN_ROOT}/include/rknn_api.h" AND EXISTS "${RKNN_ROOT}/lib/aarch64/librknnrt.so")
    set(EDGEAUDIO_HAS_RKNN ON)
endif()

set(EDGEAUDIO_HAS_SHERPA OFF)
if(EXISTS "${SHERPA_ONNX_ROOT}/include/sherpa-onnx/c-api/c-api.h")
    set(EDGEAUDIO_HAS_SHERPA ON)
endif()

if(WIN32)
    add_library(edgeaudio_host_check OBJECT asr_engine.cpp)
    target_include_directories(edgeaudio_host_check PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../include")
    target_compile_definitions(edgeaudio_host_check PRIVATE
        EDGEAUDIO_HAS_RKNN=0 EDGEAUDIO_HAS_SHERPA=0)
    target_compile_options(edgeaudio_host_check PRIVATE /W4)
    message(STATUS "Windows host check builds the portable ASR wrapper only; audio_receiver is Linux/POSIX code")
    return()
endif()

add_executable(audio_receiver audio_receiver.cpp asr_engine.cpp)
target_include_directories(audio_receiver PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../include")
target_compile_definitions(audio_receiver PRIVATE
    EDGEAUDIO_HAS_RKNN=$<BOOL:${EDGEAUDIO_HAS_RKNN}>
    EDGEAUDIO_HAS_SHERPA=$<BOOL:${EDGEAUDIO_HAS_SHERPA}>)
if(MSVC)
    target_compile_options(audio_receiver PRIVATE /W4)
else()
    target_compile_options(audio_receiver PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EDGEAUDIO_HAS_RKNN)
    target_include_directories(audio_receiver PRIVATE "${RKNN_ROOT}/include")
    target_link_libraries(audio_receiver PRIVATE "${RKNN_ROOT}/lib/aarch64/librknnrt.so")
endif()
if(EDGEAUDIO_HAS_SHERPA)
    find_library(SHERPA_C_API sherpa-onnx-c-api PATHS "${SHERPA_ONNX_ROOT}/lib" NO_DEFAULT_PATH)
    if(NOT SHERPA_C_API)
        message(FATAL_ERROR "SHERPA_ONNX_ROOT has headers but no sherpa-onnx-c-api library")
    endif()
    target_include_directories(audio_receiver PRIVATE "${SHERPA_ONNX_ROOT}/include")
    target_link_libraries(audio_receiver PRIVATE "${SHERPA_C_API}")
endif()

if(UNIX)
    target_link_libraries(audio_receiver PRIVATE pthread)
    set_target_properties(audio_receiver PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN/lib")
endif()

message(STATUS "EdgeAudio RKNN backend: ${EDGEAUDIO_HAS_RKNN}")
message(STATUS "EdgeAudio sherpa-onnx ASR backend: ${EDGEAUDIO_HAS_SHERPA}")
