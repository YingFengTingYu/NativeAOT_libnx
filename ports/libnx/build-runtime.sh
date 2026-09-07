#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
bash ports/libnx/configure-runtime.sh
cmake --build artifacts/obj/libnx/runtime \
    --target Runtime.WorkstationGC bootstrapperdll aotminipal eventpipe-disabled standalonegc-disabled \
    -- -j"${LIBNX_BUILD_JOBS:-4}" -k0 \
    2>&1 | tee artifacts/log/libnx/cross-build.log
