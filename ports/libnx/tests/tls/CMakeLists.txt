cmake_minimum_required(VERSION 3.20)
project(NativeAotLibnxTlsProbe LANGUAGES C CXX ASM)

get_filename_component(RUNTIME_REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(AOT_RUNTIME "${RUNTIME_REPO}/src/coreclr/nativeaot/Runtime")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/AsmOffsets.inc"
    COMMAND "${CMAKE_CXX_COMPILER}" -E -P -x c++ -DHOST_64BIT -DHOST_ARM64
        "-I${AOT_RUNTIME}" "-I${AOT_RUNTIME}/arm64" "${AOT_RUNTIME}/unix/AsmOffsets.cpp"
        -o "${CMAKE_CURRENT_BINARY_DIR}/AsmOffsets.inc"
    DEPENDS "${AOT_RUNTIME}/AsmOffsets.h" "${AOT_RUNTIME}/unix/AsmOffsets.cpp")
add_custom_target(tls_probe_offsets DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/AsmOffsets.inc")

foreach(mode IN ITEMS libnx linux_control)
    set(target "nativeaot-tls-${mode}")
    add_executable(${target} main.cpp runtime_tls.S)
    add_dependencies(${target} tls_probe_offsets)
    target_compile_definitions(${target} PRIVATE HOST_ARM64 HOST_64BIT)
    if(mode STREQUAL libnx)
        target_compile_definitions(${target} PRIVATE TARGET_LIBNX)
    else()
        target_compile_definitions(${target} PRIVATE LIBNX_TLS_NEGATIVE_CONTROL)
    endif()
    target_compile_options(${target} PRIVATE -O2 -g -ffunction-sections -fdata-sections)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}" "${AOT_RUNTIME}/unix" "${LIBNX_DEVKITPRO}/libnx/include")
    target_link_directories(${target} PRIVATE "${LIBNX_DEVKITPRO}/libnx/lib")
    target_link_libraries(${target} PRIVATE nx)
    target_link_options(${target} PRIVATE "LINKER:-Map,${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
    set_target_properties(${target} PROPERTIES SUFFIX .elf)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${LIBNX_DEVKITPRO}/tools/bin/elf2nro" "$<TARGET_FILE:${target}>"
            "${CMAKE_CURRENT_BINARY_DIR}/${target}.nro")
endforeach()
