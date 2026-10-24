#!/usr/bin/env bash
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/ports/libnx" "$fixture/sdk/devkitA64/bin" "$fixture/sdk/tools/bin" \
    "$fixture/src/native/external/zlib-ng" "$fixture/src/native/external/brotli" \
    "$fixture/sdk/portlibs/switch/licenses/switch-mbedtls"
cp "$repo/ports/libnx/link-managed-probe.sh" "$fixture/ports/libnx/"
for file in LICENSE.TXT THIRD-PARTY-NOTICES.TXT src/native/external/zlib-ng/LICENSE.md src/native/external/brotli/LICENSE \
    sdk/portlibs/switch/licenses/switch-mbedtls/LICENSE; do echo license > "$fixture/$file"; done
cat > "$fixture/sdk/devkitA64/bin/aarch64-none-elf-gcc" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
echo invoked >> "$TEST_COMPILE_LOG"
while [[ $# -gt 0 ]]; do
    if [[ $1 == -o ]]; then printf 'object\n' > "$2"; exit 0; fi
    shift
done
exit 1
SH
cp "$fixture/sdk/devkitA64/bin/aarch64-none-elf-gcc" "$fixture/sdk/devkitA64/bin/aarch64-none-elf-g++"
printf '#!/usr/bin/env bash\ncp "$1" "$2"\n' > "$fixture/sdk/tools/bin/elf2nro"
chmod +x "$fixture/sdk/devkitA64/bin/"* "$fixture/sdk/tools/bin/elf2nro"
export DEVKITPRO="$fixture/sdk" DEVKITA64="$fixture/sdk/devkitA64" TEST_COMPILE_LOG="$fixture/compile.log"
export LIBNX_MANAGED_OBJECT="$fixture/managed.o" LIBNX_PROBE_HOST="$fixture/main.c" LIBNX_PROBE_OUTPUT="$fixture/output"
unset LIBNX_PROBE_PKG_CONFIG LIBNX_PROBE_HOST_OBJECT LIBNX_SKIP_NRO
script="$fixture/ports/libnx/link-managed-probe.sh"
bash "$script"
test -s "$fixture/output/managed-probe.nro"
test "$(wc -l < "$TEST_COMPILE_LOG")" = 2
export LIBNX_PROBE_HOST_OBJECT="$fixture/output/main.o" LIBNX_SKIP_NRO=1
bash "$script"
test ! -e "$fixture/output/managed-probe.nro"
test -s "$fixture/output/managed-probe.elf"
test -s "$fixture/output/licenses/dotnet-runtime.txt"
test "$(wc -l < "$TEST_COMPILE_LOG")" = 3
export LIBNX_PROBE_HOST_OBJECT="$fixture/missing.o"
if bash "$script"; then echo 'Missing host object was accepted.' >&2; exit 1; fi
test "$(wc -l < "$TEST_COMPILE_LOG")" = 3
echo 'Link options: default NRO, prebuilt host, ELF-only output and missing-object rejection passed.'
