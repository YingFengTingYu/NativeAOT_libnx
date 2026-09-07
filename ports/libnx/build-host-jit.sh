#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
if [[ "$(uname -m)" != x86_64 ]]; then
    echo 'This script currently supports a Linux x64 build host.' >&2
    exit 1
fi
build_dir=artifacts/obj/coreclr/linux.x64.Release/libnx-baseline
output=artifacts/libnx/host-jit/current
mkdir -p "$output" artifacts/log/libnx
if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    bash src/coreclr/build-runtime.sh -release -gcc -ninja -component nativeaot \
        -configureonly -subdir libnx-baseline \
        -cmakeargs '-DFEATURE_EVENT_TRACE=0 -DFEATURE_PERFTRACING=0'
fi
cmake --build "$build_dir" --target clrjit_universal_arm64_x64 \
    -- -j"${LIBNX_BUILD_JOBS:-4}" 2>&1 | tee artifacts/log/libnx/host-jit-build.log
cp "$build_dir/jit/libclrjit_universal_arm64_x64.so" "$output/"
sha256sum "$output/libclrjit_universal_arm64_x64.so"
