# NativeAOT's native runtime does not require the CoreCLR host, JIT, debugger,
# or all of the framework's native shims to be configured together with it.
# The framework shims and ILC are built separately when producing a runtime pack.
include_directories("${CLR_DIR}/pal/prebuilt/inc")
include_directories("${CLR_ARTIFACTS_OBJ_DIR}")

set(EP_GENERATED_HEADER_PATH "${GENERATED_INCLUDE_DIR}")
include("${CLR_SRC_NATIVE_DIR}/eventpipe/configure.cmake")
add_subdirectory("${CLR_SRC_NATIVE_DIR}/containers" containers)
add_subdirectory("${CLR_SRC_NATIVE_DIR}/eventpipe" eventpipe)
add_subdirectory("${CLR_SRC_NATIVE_DIR}/minipal" shared_minipal)
add_subdirectory(nativeaot)
if(CLR_CMAKE_TARGET_LIBNX)
  # The initial libnx shim uses the public PAL declarations, but not the full
  # Unix implementation. Unprobed optional features remain disabled.
  configure_file("${CLR_SRC_NATIVE_DIR}/libs/Common/pal_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/system-native-config/pal_config.h")
  add_library(System.Native STATIC "${CLR_SRC_NATIVE_DIR}/libs/System.Native/pal_libnx.c"
    "${CLR_SRC_NATIVE_DIR}/libs/System.Native/pal_libnx_io.c"
    "${CLR_SRC_NATIVE_DIR}/libs/System.Native/pal_errno.c")
  target_include_directories(System.Native PRIVATE "${CLR_SRC_NATIVE_DIR}/libs/Common"
    "${CMAKE_CURRENT_SOURCE_DIR}/nativeaot/Runtime/libnx"
    "${CLR_SRC_NATIVE_DIR}/libs/System.Native" "${CMAKE_CURRENT_BINARY_DIR}/system-native-config")
  target_link_libraries(System.Native PRIVATE aotminipal)
  add_library(System.Security.Cryptography.Native.Libnx STATIC
    "${CLR_SRC_NATIVE_DIR}/libs/System.Security.Cryptography.Native.Libnx/pal_digest.c"
    "${CLR_SRC_NATIVE_DIR}/libs/System.Security.Cryptography.Native.Libnx/pal_random.c")
  target_include_directories(System.Security.Cryptography.Native.Libnx PRIVATE
    "${CLR_SRC_NATIVE_DIR}/libs/Common"
    "${LIBNX_DEVKITPRO}/portlibs/switch/include")
  target_link_libraries(System.Security.Cryptography.Native.Libnx PRIVATE
    "${LIBNX_DEVKITPRO}/portlibs/switch/lib/libmbedcrypto.a")
  include("${CMAKE_CURRENT_SOURCE_DIR}/../../ports/libnx/cmake/compression.cmake")
endif()
