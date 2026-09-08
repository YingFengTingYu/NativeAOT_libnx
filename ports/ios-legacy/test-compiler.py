#!/usr/bin/env python3
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

"""验证 Mach-O 最低版本参数、默认行为和无效输入处理。"""

from pathlib import Path
import re
import subprocess


def main():
    repo = Path(__file__).resolve().parents[2]
    compiler = repo / "artifacts/bin/coreclr/ios.arm64.Release/arm64/ilc/ilc"
    response = repo / "artifacts/legacy-ios/probes/ios/obj/Release/net10.0/ios-arm64/native/LegacyIOSProbe.ilc.rsp"
    arguments = response.read_text(encoding="utf-8-sig").splitlines()
    arguments = [line for line in arguments if not line.startswith(("-o:", "--macho-minimum-os-version:", "--exportsfile:"))]
    out = repo / "artifacts/legacy-ios/compiler-tests"
    out.mkdir(parents=True, exist_ok=True)
    project = repo / "ports/ios-legacy/tests/managed"
    for name, version, expected in [("default", None, "12.2"), ("ios7", "7.0", "7.0")]:
        obj = out / (name + ".o")
        rsp = out / (name + ".rsp")
        lines = arguments + ["-o:" + str(obj)]
        if version is not None:
            lines.append("--macho-minimum-os-version:" + version)
        rsp.write_text("\n".join(lines) + "\n")
        subprocess.run([str(compiler), "@" + str(rsp)], cwd=project, check=True)
        commands = subprocess.check_output(["xcrun", "otool", "-l", str(obj)], text=True)
        versions = re.findall(r"cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform (?:2|IOS)\s+minos ([\d.]+)", commands)
        if versions != [expected]:
            raise SystemExit(f"{name}: 预期 {expected}，实际 {versions}")
        print(f"PASS {name}: {expected}", flush=True)

    for value in ["bad", "65536.0", "7.256", "7.0.256", "7.0.0.1"]:
        rsp = out / "invalid.rsp"
        rsp.write_text("\n".join(arguments + ["-o:" + str(out / "invalid.o"), "--macho-minimum-os-version:" + value]) + "\n")
        result = subprocess.run([str(compiler), "@" + str(rsp)], cwd=project, capture_output=True, text=True)
        if result.returncode == 0 or "Invalid Mach-O deployment version" not in result.stdout + result.stderr:
            raise SystemExit(f"无效版本未被正确拒绝：{value}\n{result.stdout}{result.stderr}")
    print("PASS invalid version validation")


if __name__ == "__main__":
    main()
