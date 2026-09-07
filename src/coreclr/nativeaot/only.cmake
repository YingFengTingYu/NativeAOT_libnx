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
