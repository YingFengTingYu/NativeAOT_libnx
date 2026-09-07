#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${DEVKITA64:=${DEVKITPRO:?}/devkitA64}"
: "${LIBNX_MANAGED_OBJECT:?Set LIBNX_MANAGED_OBJECT to the ILC-generated ARM64 object}"
: "${LIBNX_PROBE_HOST:?Set LIBNX_PROBE_HOST to the native probe main.c}"
output="$repo_root/artifacts/libnx/managed-probe"
native="$repo_root/artifacts/obj/libnx/runtime"
mkdir -p "$output"
flags=(-g -O2 -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec
       -fPIE -ffunction-sections -fdata-sections -D__SWITCH__ -I"$DEVKITPRO/libnx/include")
"$DEVKITA64/bin/aarch64-none-elf-gcc" "${flags[@]}" -DNX_MANAGED_PROBE \
    -c "$LIBNX_PROBE_HOST" -o "$output/main.o"
"$DEVKITA64/bin/aarch64-none-elf-g++" "${flags[@]}" \
    -specs="$DEVKITPRO/libnx/switch.specs" "$output/main.o" "$LIBNX_MANAGED_OBJECT" \
    -Wl,--whole-archive "$native/nativeaot/Bootstrap/dll/libbootstrapperdll.a" -Wl,--no-whole-archive \
    -Wl,--start-group "$native/nativeaot/Runtime/Full/libRuntime.WorkstationGC.a" \
    "$native/shared_minipal/libaotminipal.a" \
    "$native/libSystem.Native.a" \
    "$native/libSystem.Security.Cryptography.Native.Libnx.a" \
    "$DEVKITPRO/portlibs/switch/lib/libmbedcrypto.a" \
    "$native/libSystem.IO.Compression.Native.a" \
    "$native/_deps/fetchzlibng-build/libz.a" \
    "$native/_deps/brotli-build/libbrotlienc.a" \
    "$native/_deps/brotli-build/libbrotlidec.a" \
    "$native/_deps/brotli-build/libbrotlicommon.a" \
    "$native/nativeaot/Runtime/Full/libstandalonegc-disabled.a" \
    "$native/nativeaot/Runtime/eventpipe/libeventpipe-disabled.a" \
    -L"$DEVKITPRO/libnx/lib" -lnx -lm -Wl,--end-group -Wl,--eh-frame-hdr \
    -Wl,-T,"$repo_root/ports/libnx/linker-aot.ld" \
    -Wl,-Map,"$output/managed-probe.map" -o "$output/managed-probe.elf" \
    >"$output/link.log" 2>&1
"$DEVKITPRO/tools/bin/elf2nro" "$output/managed-probe.elf" "$output/managed-probe.nro"
mkdir -p "$output/licenses"
cp "$repo_root/LICENSE.TXT" "$output/licenses/dotnet-runtime.txt"
cp "$repo_root/THIRD-PARTY-NOTICES.TXT" "$output/licenses/dotnet-third-party-notices.txt"
cp "$repo_root/src/native/external/zlib-ng/LICENSE.md" "$output/licenses/zlib-ng.txt"
cp "$repo_root/src/native/external/brotli/LICENSE" "$output/licenses/brotli.txt"
cp "$DEVKITPRO/portlibs/switch/licenses/switch-mbedtls/LICENSE" "$output/licenses/mbedtls.txt"
for probe_licenses in "$(dirname -- "$LIBNX_PROBE_HOST")/../licenses" "${LIBNX_EXTRA_LICENSES:-}"; do
    if [[ -d "$probe_licenses" ]]; then
        for license in "$probe_licenses"/*; do
            if [[ -f "$license" ]]; then
                cp "$license" "$output/licenses/"
            fi
        done
    fi
done
