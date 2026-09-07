cmake_minimum_required(VERSION 3.20)
project(NativeAotLibnxMemoryProbe LANGUAGES C)
get_filename_component(RUNTIME_REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(PLATFORM "${RUNTIME_REPO}/src/coreclr/nativeaot/Runtime/libnx")
add_executable(nativeaot-memory main.c "${PLATFORM}/LibnxMemory.c" "${PLATFORM}/LibnxEvents.c")
target_compile_definitions(nativeaot-memory PRIVATE LIBNX_MEMORY_TESTING LIBNX_EVENT_TESTING)
target_include_directories(nativeaot-memory PRIVATE "${PLATFORM}" "${LIBNX_DEVKITPRO}/libnx/include")
target_compile_options(nativeaot-memory PRIVATE -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
target_link_directories(nativeaot-memory PRIVATE "${LIBNX_DEVKITPRO}/libnx/lib")
target_link_libraries(nativeaot-memory PRIVATE nx)
set_target_properties(nativeaot-memory PROPERTIES SUFFIX .elf)
add_custom_command(TARGET nativeaot-memory POST_BUILD
    COMMAND "${LIBNX_DEVKITPRO}/tools/bin/elf2nro" "$<TARGET_FILE:nativeaot-memory>"
        "${CMAKE_CURRENT_BINARY_DIR}/nativeaot-memory.nro")
