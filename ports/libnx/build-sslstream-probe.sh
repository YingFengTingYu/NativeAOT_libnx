#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
output="$repo_root/artifacts/libnx/sslstream"
project="$repo_root/ports/libnx/tests/sslstream/TlsProbe.csproj"
mkdir -p "$output"
readarray -t trust < <(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["url"]); print(d["sha256"])' ports/libnx/openssl/ca-bundle.lock.json)
if [[ ! -f "$output/cert.pem" ]]; then
    curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' "${trust[0]}" -o "$output/cert.pem.tmp"
    mv "$output/cert.pem.tmp" "$output/cert.pem"
fi
echo "${trust[1]}  $output/cert.pem" | sha256sum --check
export LIBNX_USE_OPENSSL=1
export NUGET_PACKAGES="$repo_root/artifacts/libnx/network-managed/packages"
bash ports/libnx/generate-interop-list.sh
properties=(-p:UseArtifactsOutput=true -p:ArtifactsPath="$output" -p:LibnxRuntimeRoot="$repo_root"
    -p:LibnxJitPath="$repo_root/artifacts/libnx/host-jit/current/libclrjit_universal_arm64_x64.so"
    -p:LibnxDirectPInvokeList="$repo_root/artifacts/libnx/interop/directpinvoke.txt")
dotnet restore "$project" "${properties[@]}" --source https://api.nuget.org/v3/index.json
dotnet msbuild "$project" -t:Build,_ComputeResolvedCopyLocalPublishAssets,_ComputeAssembliesToCompileToNative,IlcCompile \
    -p:Configuration=Release "${properties[@]}" -v:minimal 2>&1 | tee "$output/build.log"
LIBNX_MANAGED_OBJECT="$output/obj/TlsProbe/release_linux-arm64/native/TlsProbe.o" \
LIBNX_PROBE_HOST="$repo_root/ports/libnx/tests/sslstream/main.c" \
LIBNX_PROBE_OUTPUT="$output" bash ports/libnx/link-managed-probe.sh
