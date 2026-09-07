#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${DEVKITA64:=${DEVKITPRO:?}/devkitA64}"
native="$repo_root/artifacts/obj/libnx/runtime"
output="$repo_root/artifacts/libnx/interop"
mkdir -p "$output"
{
    "$DEVKITA64/bin/aarch64-none-elf-nm" -g --defined-only "$native/libSystem.Native.a" |
        awk '$2 ~ /^[TW]$/ && $3 ~ /^SystemNative_/ { print "System.Native!" $3 }' | sort -u
    "$DEVKITA64/bin/aarch64-none-elf-nm" -g --defined-only "$native/libSystem.Security.Cryptography.Native.Libnx.a" |
        awk '$2 ~ /^[TW]$/ && $3 ~ /^CryptoNative_/ { print "System.Security.Cryptography.Native.OpenSsl!" $3 }' | sort -u
} > "$output/directpinvoke.txt"
sha256sum "$output/directpinvoke.txt"
