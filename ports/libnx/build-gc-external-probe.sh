#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
export LIBNX_GC_POLLS=false
export LIBNX_GC_PROBE_OUTPUT="${LIBNX_GC_PROBE_OUTPUT:-$repo_root/artifacts/libnx/gc-external-probe}"
export LIBNX_GC_PROBE_HOST="$repo_root/ports/libnx/tests/gc-external/main.c"
bash "$repo_root/ports/libnx/build-gc-poll-probe.sh"
