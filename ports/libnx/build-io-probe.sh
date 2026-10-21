#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
output="${LIBNX_IO_PROBE_OUTPUT:-$repo_root/artifacts/libnx/io-probe}"
mkdir -p "$output/romfs"
python3 - "$output/romfs/fixture.bin" <<'PY'
import pathlib, sys
pathlib.Path(sys.argv[1]).write_bytes(bytes((i * 17 + (i >> 12)) & 255 for i in range(4 * 1024 * 1024)))
PY
cmake -S "$repo_root/ports/libnx/tests/io" -B "$output" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/ports/libnx/cmake/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build "$output" -j "${LIBNX_BUILD_JOBS:-4}"
