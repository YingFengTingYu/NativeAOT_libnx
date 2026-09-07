cmake_minimum_required(VERSION 3.20)
project(NativeAotLibnxContextProbe LANGUAGES C CXX)
get_filename_component(RUNTIME_REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(AOT_RUNTIME "${RUNTIME_REPO}/src/coreclr/nativeaot/Runtime")
add_executable(nativeaot-context main.c context.cpp "${AOT_RUNTIME}/unix/NativeContext.cpp")
target_compile_definitions(nativeaot-context PRIVATE HOST_ARM64 HOST_64BIT HOST_LIBNX HOST_UNIX
    TARGET_ARM64 TARGET_64BIT TARGET_LIBNX TARGET_UNIX NDEBUG NATIVEAOT FEATURE_NATIVEAOT FEATURE_EVENT_TRACE DISABLE_CONTRACTS)
target_compile_options(nativeaot-context PRIVATE -O2 -g -ffunction-sections -fdata-sections
    -fno-strict-aliasing)
target_include_directories(nativeaot-context PRIVATE
    "${RUNTIME_REPO}/src/native" "${RUNTIME_REPO}/src/native/inc"
    "${RUNTIME_REPO}/src/coreclr/gc/env" "${RUNTIME_REPO}/src/coreclr/runtime"
    "${RUNTIME_REPO}/src/coreclr/pal/inc/rt" "${AOT_RUNTIME}" "${AOT_RUNTIME}/inc" "${AOT_RUNTIME}/unix"
    "${RUNTIME_REPO}/artifacts/obj/libnx/runtime/nativeaot/Runtime"
    "${LIBNX_DEVKITPRO}/libnx/include")
target_link_directories(nativeaot-context PRIVATE "${LIBNX_DEVKITPRO}/libnx/lib")
target_link_libraries(nativeaot-context PRIVATE nx)
set_target_properties(nativeaot-context PROPERTIES SUFFIX .elf)
add_custom_command(TARGET nativeaot-context POST_BUILD
    COMMAND "${LIBNX_DEVKITPRO}/tools/bin/elf2nro" "$<TARGET_FILE:nativeaot-context>"
        "${CMAKE_CURRENT_BINARY_DIR}/nativeaot-context.nro")
