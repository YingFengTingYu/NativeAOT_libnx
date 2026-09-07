cmake_minimum_required(VERSION 3.20)
project(NativeAotLibnxThreadProbe LANGUAGES C)
get_filename_component(RUNTIME_REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(PLATFORM "${RUNTIME_REPO}/src/coreclr/nativeaot/Runtime/libnx")
add_executable(nativeaot-threads main.c "${PLATFORM}/LibnxThreads.c" "${PLATFORM}/LibnxSystem.c")
target_compile_definitions(nativeaot-threads PRIVATE LIBNX_THREAD_TESTING)
target_include_directories(nativeaot-threads PRIVATE "${PLATFORM}" "${LIBNX_DEVKITPRO}/libnx/include")
target_compile_options(nativeaot-threads PRIVATE -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
target_link_directories(nativeaot-threads PRIVATE "${LIBNX_DEVKITPRO}/libnx/lib")
target_link_libraries(nativeaot-threads PRIVATE nx)
set_target_properties(nativeaot-threads PROPERTIES SUFFIX .elf)
add_custom_command(TARGET nativeaot-threads POST_BUILD
    COMMAND "${LIBNX_DEVKITPRO}/tools/bin/elf2nro" "$<TARGET_FILE:nativeaot-threads>"
        "${CMAKE_CURRENT_BINARY_DIR}/nativeaot-threads.nro")
