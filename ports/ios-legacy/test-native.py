#!/usr/bin/env python3
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

"""在 Apple Silicon Mac 上验证真实 TLS 汇编包装和 Mach 时钟后备实现。"""

from pathlib import Path
import platform
import subprocess


def run(command):
    subprocess.run([str(value) for value in command], check=True)


def main():
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise SystemExit("此测试需要 Apple Silicon Mac。")
    repo = Path(__file__).resolve().parents[2]
    tests = repo / "ports/ios-legacy/tests"
    out = repo / "artifacts/legacy-ios/native-tests"
    out.mkdir(parents=True, exist_ok=True)
    sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
    common = ["xcrun", "clang", "-target", "arm64-apple-macos12.0", "-isysroot", sdk]
    # 包装器只使用通用汇编宏，不使用运行时字段偏移。
    (out / "AsmOffsets.inc").write_text("// No runtime offsets are used by these ABI tests.\n")
    runtime = repo / "src/coreclr/nativeaot/Runtime"
    run(common + [
        "-DTARGET_ARM64", "-DHOST_ARM64", "-I" + str(runtime / "unix"), "-I" + str(out),
        runtime / "arm64/PthreadTls.S", tests / "tls-abi.S", tests / "tls-abi.c",
        "-o", out / "tls-abi",
    ])
    run([out / "tls-abi"])

    # 在本机强制选择真正的 Mach 后备分支，与系统时钟交叉比较。
    (out / "minipalconfig.h").write_text(
        "#define HAVE_MACH_ABSOLUTE_TIME 1\n"
        "#define HAVE_CLOCK_GETTIME_NSEC_NP 0\n"
        "#define HAVE_CLOCK_MONOTONIC 0\n"
        "#define HAVE_CLOCK_MONOTONIC_COARSE 0\n"
    )
    run(common + [
        "-std=c11", "-DHOST_ARM64", "-I" + str(repo / "src/native"), "-I" + str(out),
        repo / "src/native/minipal/time.c", tests / "mach-clock.c", "-o", out / "mach-clock",
    ])
    run([out / "mach-clock"])


if __name__ == "__main__":
    main()
