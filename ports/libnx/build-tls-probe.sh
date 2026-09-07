#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
output="$repo_root/artifacts/libnx/tls-probe"
mkdir -p "$repo_root/artifacts/log/libnx"
cmake -S "$repo_root/ports/libnx/tests/tls" -B "$output" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/ports/libnx/cmake/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$output" -j "${LIBNX_BUILD_JOBS:-4}"
"$DEVKITA64/bin/aarch64-none-elf-objdump" -d --disassemble=nx_runtime_tls_address \
    "$output/nativeaot-tls-libnx.elf" > "$output/libnx-tls-disassembly.txt"
"$DEVKITA64/bin/aarch64-none-elf-objdump" -d --disassemble=nx_runtime_tls_address \
    "$output/nativeaot-tls-linux_control.elf" > "$output/linux-tls-disassembly.txt"
sha256sum "$output"/*.nro
