cmake_minimum_required(VERSION 3.20)
project(NativeAotLibnxIoProbe LANGUAGES C)
get_filename_component(RUNTIME_REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
add_executable(nativeaot-io main.c
    "${RUNTIME_REPO}/src/coreclr/nativeaot/Runtime/libnx/LibnxThreads.c"
    "${RUNTIME_REPO}/src/coreclr/nativeaot/Runtime/libnx/LibnxMemory.c")
target_include_directories(nativeaot-io PRIVATE
    "${RUNTIME_REPO}/src/native"
    "${RUNTIME_REPO}/src/coreclr/nativeaot/Runtime/libnx"
    "${RUNTIME_REPO}/src/native/libs/Common" "${RUNTIME_REPO}/src/native/libs/System.Native"
    "${RUNTIME_REPO}/artifacts/obj/libnx/runtime/system-native-config" "${LIBNX_DEVKITPRO}/libnx/include")
target_compile_options(nativeaot-io PRIVATE -O2 -g -Wall -Wextra -Werror)
target_link_options(nativeaot-io PRIVATE -Wl,--wrap=fsFileRead -Wl,--wrap=fsStorageRead)
target_link_directories(nativeaot-io PRIVATE "${LIBNX_DEVKITPRO}/libnx/lib")
target_link_libraries(nativeaot-io PRIVATE
    "${RUNTIME_REPO}/artifacts/obj/libnx/runtime/libSystem.Native.a"
    "${RUNTIME_REPO}/artifacts/obj/libnx/runtime/shared_minipal/libaotminipal.a" nx)
set_target_properties(nativeaot-io PROPERTIES SUFFIX .elf)
add_custom_command(TARGET nativeaot-io POST_BUILD
    COMMAND "${LIBNX_DEVKITPRO}/tools/bin/nacptool" --create "NativeAOT IO probe" "NativeAOT" "1.0"
        "${CMAKE_CURRENT_BINARY_DIR}/probe.nacp"
    COMMAND "${LIBNX_DEVKITPRO}/tools/bin/elf2nro" "$<TARGET_FILE:nativeaot-io>"
        "${CMAKE_CURRENT_BINARY_DIR}/nativeaot-io.nro"
        "--nacp=${CMAKE_CURRENT_BINARY_DIR}/probe.nacp"
        "--romfsdir=${CMAKE_CURRENT_BINARY_DIR}/romfs")
