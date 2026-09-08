#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
output="$repo_root/artifacts/libnx/network-managed"
project="$repo_root/ports/libnx/tests/network-managed/NetworkProbe.csproj"
mkdir -p "$output"
export NUGET_PACKAGES="$output/packages"
bash ports/libnx/generate-interop-list.sh
properties=(-p:UseArtifactsOutput=true -p:ArtifactsPath="$output" -p:LibnxRuntimeRoot="$repo_root"
    -p:LibnxJitPath="$repo_root/artifacts/libnx/host-jit/current/libclrjit_universal_arm64_x64.so"
    -p:LibnxDirectPInvokeList="$repo_root/artifacts/libnx/interop/directpinvoke.txt")
dotnet restore "$project" "${properties[@]}" --source https://api.nuget.org/v3/index.json
dotnet msbuild "$project" -t:Build,_ComputeResolvedCopyLocalPublishAssets,_ComputeAssembliesToCompileToNative,IlcCompile \
    -p:Configuration=Release "${properties[@]}" -v:minimal 2>&1 | tee "$output/build.log"
LIBNX_MANAGED_OBJECT="$output/obj/NetworkProbe/release_linux-arm64/native/NetworkProbe.o" \
LIBNX_PROBE_HOST="$repo_root/ports/libnx/tests/network-managed/main.c" \
LIBNX_PROBE_OUTPUT="$output" bash ports/libnx/link-managed-probe.sh
