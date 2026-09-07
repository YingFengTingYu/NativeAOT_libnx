# Public devkitPro/libnx target. No Linux or Android sysroot is used.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSTEM_VARIANT libnx)
set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO must point to the devkitPro installation")
endif()
set(LIBNX_DEVKITPRO "$ENV{DEVKITPRO}" CACHE PATH "devkitPro installation")
set(LIBNX_DEVKITA64 "${LIBNX_DEVKITPRO}/devkitA64")
set(CMAKE_C_COMPILER "${LIBNX_DEVKITA64}/bin/aarch64-none-elf-gcc")
set(CMAKE_CXX_COMPILER "${LIBNX_DEVKITA64}/bin/aarch64-none-elf-g++")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_C_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_CXX_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_AR "${LIBNX_DEVKITA64}/bin/aarch64-none-elf-ar")
set(CMAKE_RANLIB "${LIBNX_DEVKITA64}/bin/aarch64-none-elf-ranlib")
set(CMAKE_NM "${LIBNX_DEVKITA64}/bin/aarch64-none-elf-nm")
set(CMAKE_OBJDUMP "${LIBNX_DEVKITA64}/bin/aarch64-none-elf-objdump")
set(CMAKE_READELF "${LIBNX_DEVKITA64}/bin/aarch64-none-elf-readelf")

set(LIBNX_ARCH_FLAGS "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec -fPIE -D__SWITCH__ -I${LIBNX_DEVKITPRO}/libnx/include")
set(CMAKE_C_FLAGS_INIT "${LIBNX_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${LIBNX_ARCH_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${LIBNX_ARCH_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=${LIBNX_DEVKITPRO}/libnx/switch.specs")
set(CMAKE_C_STANDARD_LIBRARIES_INIT "-L${LIBNX_DEVKITPRO}/libnx/lib -lnx")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "-L${LIBNX_DEVKITPRO}/libnx/lib -lnx")

set(CMAKE_FIND_ROOT_PATH
    "${LIBNX_DEVKITA64}/aarch64-none-elf"
    "${LIBNX_DEVKITPRO}/libnx"
    "${LIBNX_DEVKITPRO}/portlibs/switch")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
