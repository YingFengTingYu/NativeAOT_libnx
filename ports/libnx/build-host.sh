#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
mkdir -p artifacts/log/libnx

bash src/coreclr/build-runtime.sh -release -gcc -ninja -component nativeaot \
    -numproc "${LIBNX_BUILD_JOBS:-4}" -subdir libnx-baseline \
    -cmakeargs '-DFEATURE_EVENT_TRACE=0 -DFEATURE_PERFTRACING=0' \
    2>&1 | tee artifacts/log/libnx/linux-host-build.log
