cmake_minimum_required(VERSION 3.20)
project(NativeAotLibnxPlatformProbe LANGUAGES C CXX ASM)

get_filename_component(RUNTIME_REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(CLR_CMAKE_TARGET_OS libnx)
set(CLR_CMAKE_TARGET_ARCH arm64)
include("${RUNTIME_REPO}/eng/native/configurepaths.cmake")
include("${RUNTIME_REPO}/eng/native/configurecompiler.cmake")
include(CheckFunctionExists)
check_function_exists(pthread_create LIBNX_PROBE_HAS_PTHREAD_CREATE)
check_function_exists(libnx_deliberately_nonexistent_api LIBNX_PROBE_HAS_NONEXISTENT_API)
if(NOT LIBNX_PROBE_HAS_PTHREAD_CREATE OR LIBNX_PROBE_HAS_NONEXISTENT_API)
    message(FATAL_ERROR "API probes must link: pthread_create must exist and the negative control must fail")
endif()

if(NOT CLR_CMAKE_HOST_LIBNX OR NOT CLR_CMAKE_TARGET_LIBNX OR
   NOT CLR_CMAKE_HOST_ARCH_ARM64 OR NOT CLR_CMAKE_TARGET_ARCH_ARM64)
    message(FATAL_ERROR "The upstream compiler configuration did not identify libnx/ARM64")
endif()
if(CLR_CMAKE_HOST_LINUX OR CLR_CMAKE_TARGET_LINUX)
    message(FATAL_ERROR "The libnx target must not select the Linux platform")
endif()

add_library(libnx-platform-probe STATIC platform.cpp)
