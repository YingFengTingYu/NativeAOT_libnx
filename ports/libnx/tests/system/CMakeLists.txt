cmake_minimum_required(VERSION 3.20)
project(NativeAotLibnxSystemProbe LANGUAGES C)
get_filename_component(RUNTIME_REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
add_executable(nativeaot-system main.c "${RUNTIME_REPO}/src/coreclr/nativeaot/Runtime/libnx/LibnxThreads.c")
target_include_directories(nativeaot-system PRIVATE
    "${RUNTIME_REPO}/src/native"
    "${RUNTIME_REPO}/src/native/libs/Common" "${RUNTIME_REPO}/src/native/libs/System.Native"
    "${RUNTIME_REPO}/artifacts/obj/libnx/runtime/system-native-config" "${LIBNX_DEVKITPRO}/libnx/include")
target_compile_options(nativeaot-system PRIVATE -O2 -g -Wall -Wextra -Werror)
target_link_directories(nativeaot-system PRIVATE "${LIBNX_DEVKITPRO}/libnx/lib")
target_link_libraries(nativeaot-system PRIVATE
    "${RUNTIME_REPO}/artifacts/obj/libnx/runtime/libSystem.Native.a"
    "${RUNTIME_REPO}/artifacts/obj/libnx/runtime/shared_minipal/libaotminipal.a" nx)
set_target_properties(nativeaot-system PROPERTIES SUFFIX .elf)
add_custom_command(TARGET nativeaot-system POST_BUILD
    COMMAND "${LIBNX_DEVKITPRO}/tools/bin/elf2nro" "$<TARGET_FILE:nativeaot-system>"
        "${CMAKE_CURRENT_BINARY_DIR}/nativeaot-system.nro")
