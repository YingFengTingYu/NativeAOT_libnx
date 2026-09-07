#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${DEVKITA64:=${DEVKITPRO:?}/devkitA64}"
export PATH="$DEVKITA64/bin:$PATH"
output="$repo_root/artifacts/libnx/platform-probe"
cmake -S "$repo_root/ports/libnx/tests/platform" -B "$output" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/ports/libnx/cmake/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$output" -j "${LIBNX_BUILD_JOBS:-4}"
"$DEVKITA64/bin/aarch64-none-elf-readelf" -h "$output/liblibnx-platform-probe.a"
"$DEVKITA64/bin/aarch64-none-elf-objdump" -dr "$output/liblibnx-platform-probe.a" > "$output/tls-disassembly.txt"
