#!/usr/bin/env python3
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

"""检查最终 iOS 探针的架构、版本、动态依赖与已知新系统入口。"""

import argparse
import json
from pathlib import Path
import re
import subprocess


def output(*command):
    return subprocess.check_output(command, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=["arm64", "arm"], default="arm64")
    parser.add_argument("--minimal", action="store_true", help="检查 ARM32 最小启动探针")
    args = parser.parse_args()
    if args.minimal and args.arch != "arm":
        parser.error("--minimal 需要 --arch arm。")
    repo = Path(__file__).resolve().parents[2]
    if args.arch == "arm":
        variant = "minimal/ArmHello" if args.minimal else "full/LegacyIOSProbe32"
        binary = repo / "artifacts/legacy-ios/probes/ios-arm" / variant
    else:
        binary = repo / "artifacts/legacy-ios/probes/ios/publish/LegacyIOSProbe"
    architecture = output("xcrun", "lipo", "-archs", str(binary)).strip()
    commands = output("xcrun", "otool", "-l", str(binary))
    imports = output("xcrun", "nm", "-u", str(binary))
    dependencies = output("xcrun", "otool", "-L", str(binary))
    versions = re.findall(r"cmd LC_VERSION_MIN_IPHONEOS\s+cmdsize \d+\s+version ([\d.]+)\s+sdk ([\d.]+)", commands)
    forbidden = [
        "tlv_bootstrap", "tlv_atexit", "cxa_thread_atexit", "clock_gettime",
        "CCRandomGenerateBytes", "thread_get_register_pointer_values", "aligned_alloc",
        "os_unfair_lock", "getentropy", "chkstk_darwin", "swift",
    ]
    suspect_imports = sorted({line.strip() for line in imports.splitlines()
                              if any(name in line for name in forbidden)})
    suspect_libraries = [line.strip() for line in dependencies.splitlines()[1:]
                         if any(name in line for name in ["CryptoKit.framework", "Network.framework", "libswift"])]
    tls_sections = re.findall(r"sectname (__thread\w+)", commands)
    report = {
        "binary": str(binary), "architecture": architecture,
        "minimum_and_sdk": versions, "native_tls_sections": tls_sections,
        "suspect_imports": suspect_imports, "suspect_libraries": suspect_libraries,
        "validation_scope": "static",
    }
    expected_architecture = "armv7" if args.arch == "arm" else "arm64"
    report["passed"] = architecture == expected_architecture and versions == [("7.0", "9.3")] and not (
        tls_sections or suspect_imports or suspect_libraries)
    if args.arch == "arm":
        symbols = output("xcrun", "nm", "-m", str(binary))
        runtime_globals = [line for line in symbols.splitlines()
                           if line.endswith((" ___security_cookie", " _RhpTrapThreads"))]
        report["runtime_globals"] = runtime_globals
        report["passed"] &= len(runtime_globals) == 2 and all("weak" not in line for line in runtime_globals)
    path = binary.parent / "audit.json" if args.arch == "arm" else repo / "artifacts/legacy-ios/probes/ios/audit.json"
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if not report["passed"]:
        raise SystemExit(1)
    print("通过静态检查；这不代替真实 iOS 7 设备验证。")


if __name__ == "__main__":
    main()
