#!/usr/bin/env python3
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

"""使用同版本官方托管库和本地 pthread TLS 运行时构建验证程序。"""

import argparse
from pathlib import Path
import shutil
import subprocess


def run(command):
    subprocess.run([str(value) for value in command], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=["ios", "osx"], default="ios")
    parser.add_argument("--sdk", default=str(Path.home() / "SDKs/iPhoneOS9.3.sdk"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    rid = args.platform + "-arm64"
    variant = "legacy-ios" if args.platform == "ios" else "legacy-ios-host"
    built = repo / f"artifacts/bin/coreclr/{args.platform}.arm64.Release/{variant}/aotsdk"
    if not (built / "libRuntime.WorkstationGC.a").is_file():
        raise SystemExit(f"请先构建 pthread TLS 运行时：{built}")
    stock = Path.home() / f".nuget/packages/microsoft.netcore.app.runtime.nativeaot.{rid}/10.0.7/runtimes/{rid}/native"
    if not stock.is_dir():
        raise SystemExit(f"缺少对应版本的官方 NativeAOT 包：{stock}")
    out = repo / f"artifacts/legacy-ios/probes/{args.platform}"
    tools = repo / "artifacts/bin/coreclr/ios.arm64.Release/arm64/ilc"
    if not (tools / "ilc").is_file():
        raise SystemExit(f"请先构建本地 ILCompiler：{tools}")
    native = out / "runtime-native"
    shutil.copytree(stock, native, dirs_exist_ok=True)
    for source in built.iterdir():
        if source.suffix in [".a", ".o"]:
            shutil.copy2(source, native / source.name)

    if args.platform == "ios":
        system_native = repo / "artifacts/obj/coreclr/ios.arm64.Release/legacy-ios/libs-native/System.Native/libSystem.Native.a"
        if not system_native.is_file():
            raise SystemExit(f"请先使用旧 SDK 构建 System.Native-Static：{system_native}")
        shutil.copy2(system_native, native / "libSystem.Native.a")

    sdk = args.sdk if args.platform == "ios" else subprocess.check_output(
        ["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
    triple = "arm64-apple-ios7.0" if args.platform == "ios" else "arm64-apple-macos12.0"
    callback = out / "callback.o"
    run(["xcrun", "clang", "-target", triple, "-isysroot", sdk, "-c",
         repo / "ports/ios-legacy/tests/managed/callback.c", "-o", callback])
    dotnet = shutil.which("dotnet")
    if dotnet is None:
        raise SystemExit("请先安装 .NET 10 SDK。")
    command = [
        dotnet, "publish",
        repo / "ports/ios-legacy/tests/managed/LegacyIOSProbe.csproj", "-c", "Release", "-r", rid,
        f"-p:BaseIntermediateOutputPath={out}/obj/", f"-p:BaseOutputPath={out}/bin/",
        f"-p:IlcSdkPath={native}/", f"-p:IlcFrameworkNativePath={native}/",
        f"-p:IlcToolsPath={tools}/",
        f"-p:LegacyIOSProbeObject={callback}", f"-p:SysRoot={sdk}",
        "-o", out / "publish",
    ]
    if args.platform == "ios":
        command.append("-p:AppleMinOSVersion=7.0")
    run(command)
    print(f"验证程序：{out / 'publish/LegacyIOSProbe'}")


if __name__ == "__main__":
    main()
