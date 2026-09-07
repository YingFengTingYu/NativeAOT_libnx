#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${DEVKITA64:=${DEVKITPRO:?}/devkitA64}"
export PATH="$DEVKITA64/bin:$PATH"
mkdir -p "$repo_root/artifacts/log/libnx" "$repo_root/artifacts/obj"
cd "$repo_root"
bash eng/native/version/copy_version_files.sh

# This is an honest configure attempt against the upstream runtime graph.
# It is not yet a supported runtime build; preserve its diagnostics and status.
cmake -S src/coreclr -B artifacts/obj/libnx/runtime -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/ports/libnx/cmake/toolchain.cmake" \
    -DCLR_CMAKE_TARGET_OS=libnx -DCLR_CMAKE_TARGET_ARCH=arm64 \
    -DCLR_CMAKE_NATIVEAOT_ONLY=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$repo_root/artifacts/bin/libnx/runtime" \
    "$@" \
    2>&1 | tee artifacts/log/libnx/cross-configure.log
