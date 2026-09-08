#!/usr/bin/env python3
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

"""构建 iOS 7 ARMv7/ARM64 NativeAOT，或用于本机验证的 pthread TLS 实现。"""

import argparse
import json
from pathlib import Path
import plistlib
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=["ios", "osx"], default="ios")
    parser.add_argument("--arch", choices=["arm64", "arm"], default="arm64")
    parser.add_argument("--sdk", default=str(Path.home() / "SDKs/iPhoneOS9.3.sdk"))
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if args.jobs < 1:
        raise SystemExit("--jobs 必须大于零。")
    if args.arch == "arm" and args.platform != "ios":
        raise SystemExit("ARM32 适配只面向 iOS。")
    repo = Path(__file__).resolve().parents[2]
    variant = "legacy-ios" if args.platform == "ios" else "legacy-ios-host"
    cmake_args = ["-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    if args.platform == "ios":
        sdk = Path(args.sdk).expanduser().resolve()
        with (sdk / "SDKSettings.plist").open("rb") as stream:
            metadata = plistlib.load(stream)
        if metadata.get("CanonicalName") != "iphoneos9.3":
            raise SystemExit("当前验证配置需要 iPhoneOS9.3.sdk 真机 SDK。")
        if not (sdk / "SDKSettings.json").exists():
            (sdk / "SDKSettings.json").write_text(json.dumps(metadata, indent=2) + "\n")
        modern_sdk = Path(subprocess.check_output(
            ["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip())
        headers = modern_sdk / "usr/include/c++/v1"
        if not (headers / "new").is_file():
            raise SystemExit(f"找不到现代 libc++ 头文件：{headers}")
        cmake_args += [
            f"-DCMAKE_OSX_SYSROOT={sdk}", "-DCMAKE_OSX_DEPLOYMENT_TARGET=7.0",
            f"-DCMAKE_CXX_FLAGS=-isystem{headers}",
        ]
    else:
        cmake_args.append("-DCLR_CMAKE_NATIVEAOT_USE_PTHREAD_TLS=ON")
        if shutil.which("brew"):
            icu = subprocess.run(["brew", "--prefix", "icu4c"], capture_output=True, text=True)
            if icu.returncode == 0:
                cmake_args.append("-DCLR_CMAKE_ICU_DIR=" + icu.stdout.strip())
    command = [
        "bash", "src/coreclr/build-runtime.sh", "-os", args.platform, "-arch", args.arch,
        "-release", "-component", "nativeaot", "-subdir", variant,
        "-numproc", str(args.jobs), "-cmakeargs", " ".join(cmake_args),
    ]
    if args.platform == "ios":
        command.append("-cross")
    subprocess.run(command, cwd=repo, check=True)
    if args.platform == "ios":
        build = repo / f"artifacts/obj/coreclr/ios.{args.arch}.Release/legacy-ios"
        subprocess.run(["cmake", "--build", str(build), "--target", "System.Native-Static",
                        "-j", str(args.jobs)], check=True)


if __name__ == "__main__":
    main()
