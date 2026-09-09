#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
export PATH="${DEVKITA64:-${DEVKITPRO:?}/devkitA64}/bin:$PATH"
crypto_target=System.Security.Cryptography.Native.Libnx
if [[ "${LIBNX_USE_OPENSSL:-0}" == 1 ]]; then
    bash ports/libnx/build-openssl.sh
    crypto_target=System.Security.Cryptography.Native.OpenSsl-Static
    bash ports/libnx/configure-runtime.sh -DLIBNX_USE_OPENSSL=ON
else
    bash ports/libnx/configure-runtime.sh -DLIBNX_USE_OPENSSL=OFF
fi
cmake --build artifacts/obj/libnx/runtime \
    --target Runtime.WorkstationGC bootstrapperdll aotminipal eventpipe-disabled standalonegc-disabled System.Native System.IO.Compression.Native "$crypto_target" \
    -- -j"${LIBNX_BUILD_JOBS:-4}" -k0 \
    2>&1 | tee artifacts/log/libnx/cross-build.log
