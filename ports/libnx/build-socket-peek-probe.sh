#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
output="$repo_root/artifacts/libnx/socketpeek-probe"
mkdir -p "$output"
toolchain="${DEVKITA64:-${DEVKITPRO:?}/devkitA64}/bin/aarch64-none-elf"
"$toolchain-gcc" -O2 -Wall -Wextra -Werror -march=armv8-a+crc+crypto -mtune=cortex-a57 \
    -mtp=soft -fPIE -D__SWITCH__ -I"$DEVKITPRO/libnx/include" \
    -specs="$DEVKITPRO/libnx/switch.specs" "$repo_root/ports/libnx/tests/socket-peek/main.c" \
    -L"$DEVKITPRO/libnx/lib" -lnx -o "$output/nativeaot-socketpeek.elf"
"$DEVKITPRO/tools/bin/elf2nro" "$output/nativeaot-socketpeek.elf" "$output/nativeaot-socketpeek.nro"
