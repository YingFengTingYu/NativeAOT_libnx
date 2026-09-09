#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
port="$repo_root/ports/libnx/openssl"
output="$repo_root/artifacts/libnx/openssl"
readarray -t release < <(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["version"]); print(d["url"]); print(d["sha256"])' "$port/openssl.lock.json")
version="${release[0]}"
archive="$output/downloads/openssl-$version.tar.gz"
toolchain="${DEVKITA64:-${DEVKITPRO:?}/devkitA64}/bin/aarch64-none-elf-"
fingerprint="$( { sha256sum "$port/openssl.lock.json" "$port/99-libnx.conf" "$port/"*.patch "${BASH_SOURCE[0]}"; "${toolchain}gcc" --version; } | sha256sum | cut -d' ' -f1)"
mkdir -p "$output/downloads" "$output/install"
if [[ -f "$output/build-fingerprint" && "$(cat "$output/build-fingerprint")" == "$fingerprint" ]] &&
    (cd "$output" && sha256sum --status --check installed.sha256); then
    echo "OpenSSL $version: verified build cache."
    exit 0
fi
if [[ ! -f "$archive" ]]; then
    curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' "${release[1]}" -o "$archive.tmp"
    mv "$archive.tmp" "$archive"
fi
echo "${release[2]}  $archive" | sha256sum --check
# Source generation performs many small filesystem operations. Keep this
# disposable tree on the container filesystem, not the Windows bind mount.
work="${LIBNX_OPENSSL_WORK_ROOT:-/tmp/libnx-openssl}/$fingerprint"
source="$work/openssl-$version"
mkdir -p "$work/build"
if [[ ! -f "$source/Configure" ]]; then
    tar -xzf "$archive" -C "$work"
fi
if ! grep -q 'Libnx CSRNG' "$source/providers/implementations/rands/seeding/rand_unix.c"; then
    patch -d "$source" -p1 < "$port/csrng.patch"
    patch -d "$source" -p1 < "$port/platform.patch"
fi
cp "$port/99-libnx.conf" "$source/Configurations/99-libnx.conf"
cd "$work/build"
# TLS records travel through memory BIOs and SslStream's InnerStream. OpenSSL
# does not own sockets or load modules/configuration from a host Linux system.
# Keep pthreads: SslStream and certificate validation run concurrently.
perl "$source/Configure" libnx-aarch64 --cross-compile-prefix="$toolchain" \
    --prefix="$output/install" --openssldir=/dotnet/ssl --libdir=lib \
    --with-rand-seed=getrandom no-shared no-module no-dso no-engine no-async \
    no-sock no-dgram no-quic no-asm no-tests no-apps no-ui-console \
    -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec \
    -fPIE -ffunction-sections -fdata-sections -D__SWITCH__ -I"$DEVKITPRO/libnx/include" \
    2>&1 | tee "$output/configure.log"
make -j"${LIBNX_BUILD_JOBS:-4}" build_libs > "$output/build.log" 2>&1 || { tail -40 "$output/build.log"; exit 1; }
make install_dev > "$output/install.log" 2>&1 || { tail -30 "$output/install.log"; exit 1; }
mkdir -p "$output/install/licenses"
cp "$source/LICENSE.txt" "$port/openssl.lock.json" "$output/install/licenses/"
cd "$output"
sha256sum install/lib/libssl.a install/lib/libcrypto.a > installed.sha256
printf '%s\n' "$fingerprint" > build-fingerprint
cat installed.sha256
