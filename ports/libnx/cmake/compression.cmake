# Build the upstream compression wrapper and vendored codecs for libnx.
function(configure_libnx_compression)
  # Begin with portable codec paths; platform CPU detection and optimized
  # implementations need their own validation on this target.
  set(WITH_OPTIM OFF)
  set(WITH_NATIVE_INSTRUCTIONS OFF)
  include("${CLR_SRC_NATIVE_DIR}/external/zlib-ng.cmake")
  include("${CLR_SRC_NATIVE_DIR}/external/brotli.cmake")
  add_library(System.IO.Compression.Native STATIC
    "${CLR_SRC_NATIVE_DIR}/libs/System.IO.Compression.Native/pal_zlib.c"
    "${CLR_SRC_NATIVE_DIR}/libs/System.IO.Compression.Native/entrypoints.c")
  target_include_directories(System.IO.Compression.Native PRIVATE
    "${CLR_SRC_NATIVE_DIR}/libs/Common"
    "${CMAKE_CURRENT_BINARY_DIR}/system-native-config"
    ${BROTLI_INCLUDE_DIRS})
  target_link_libraries(System.IO.Compression.Native PRIVATE zlib ${BROTLI_LIBRARIES} aotminipal)
endfunction()
configure_libnx_compression()
