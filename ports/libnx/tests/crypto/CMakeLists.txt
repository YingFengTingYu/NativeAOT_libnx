cmake_minimum_required(VERSION 3.20)
project(NativeAotLibnxCryptoProbe LANGUAGES C)
get_filename_component(RUNTIME_REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
add_executable(nativeaot-crypto main.c)
target_include_directories(nativeaot-crypto PRIVATE
    "${RUNTIME_REPO}/src/native/libs/System.Security.Cryptography.Native.Libnx"
    "${LIBNX_DEVKITPRO}/portlibs/switch/include" "${LIBNX_DEVKITPRO}/libnx/include")
target_compile_options(nativeaot-crypto PRIVATE -O2 -g -Wall -Wextra -Werror)
target_link_directories(nativeaot-crypto PRIVATE "${LIBNX_DEVKITPRO}/libnx/lib")
target_link_libraries(nativeaot-crypto PRIVATE
    "${RUNTIME_REPO}/artifacts/obj/libnx/runtime/libSystem.Security.Cryptography.Native.Libnx.a"
    "${LIBNX_DEVKITPRO}/portlibs/switch/lib/libmbedcrypto.a" nx)
set_target_properties(nativeaot-crypto PROPERTIES SUFFIX .elf)
add_custom_command(TARGET nativeaot-crypto POST_BUILD
    COMMAND "${LIBNX_DEVKITPRO}/tools/bin/elf2nro" "$<TARGET_FILE:nativeaot-crypto>"
        "${CMAKE_CURRENT_BINARY_DIR}/nativeaot-crypto.nro")
