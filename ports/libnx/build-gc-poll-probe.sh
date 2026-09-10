#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
output="${LIBNX_GC_PROBE_OUTPUT:-$repo_root/artifacts/libnx/gc-poll-probe}"
project="$repo_root/ports/libnx/tests/gc-polls/GcPollProbe.csproj"
mkdir -p "$output"
export NUGET_PACKAGES="${NUGET_PACKAGES:-$output/packages}"
properties=(-p:UseArtifactsOutput=true -p:ArtifactsPath="$output" -p:LibnxRuntimeRoot="$repo_root"
    -p:LibnxJitPath="$repo_root/artifacts/libnx/host-jit/current/libclrjit_universal_arm64_x64.so"
    -p:LibnxDirectPInvokeList="$repo_root/artifacts/libnx/interop/directpinvoke.txt"
    -p:LibnxLoopGcPolls="${LIBNX_GC_POLLS:-true}")
dotnet restore "$project" "${properties[@]}" --source https://api.nuget.org/v3/index.json
dotnet msbuild "$project" -t:Build,_ComputeResolvedCopyLocalPublishAssets,_ComputeAssembliesToCompileToNative,IlcCompile \
    -p:Configuration=Release "${properties[@]}" -v:minimal 2>&1 | tee "$output/build.log"
LIBNX_MANAGED_OBJECT="$output/obj/GcPollProbe/release_linux-arm64/native/GcPollProbe.o" \
LIBNX_PROBE_HOST="$repo_root/ports/libnx/tests/gc-polls/main.c" \
LIBNX_PROBE_OUTPUT="$output" bash ports/libnx/link-managed-probe.sh
