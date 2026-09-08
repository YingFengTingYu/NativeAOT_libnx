#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cmake -S "$repo_root/ports/libnx/tests/network" -B "$repo_root/artifacts/libnx/network-probe" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/ports/libnx/cmake/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build "$repo_root/artifacts/libnx/network-probe" -j "${LIBNX_BUILD_JOBS:-4}"
