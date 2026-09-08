#!/usr/bin/env python3
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

"""使用源码构建的 iOS ARM32 CoreLib、基础库和运行时生成探针。"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess


def run(command, log):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True)
    log.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        print(result.stdout + result.stderr)
        raise SystemExit(result.returncode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--minimal", action="store_true", help="只验证托管 Main 和一次 P/Invoke")
    parser.add_argument("--sign", action="store_true", help="为越狱真机测试生成 ad-hoc 双摘要签名")
    parser.add_argument("--sdk", default=str(Path.home() / "SDKs/iPhoneOS9.3.sdk"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    name = "ArmHello" if args.minimal else "LegacyIOSProbe32"
    out = repo / "artifacts/legacy-ios/probes/ios-arm" / ("minimal" if args.minimal else "full")
    out.mkdir(parents=True, exist_ok=True)
    sdk = Path(args.sdk).expanduser().resolve()
    core = repo / "artifacts/bin/coreclr/ios.arm.Release/aotsdk"
    sfx = repo / "artifacts/bin/runtime/net10.0-ios-Release-arm"
    native = repo / "artifacts/bin/coreclr/ios.arm.Release/legacy-ios/aotsdk"
    system_native = repo / "artifacts/obj/coreclr/ios.arm.Release/legacy-ios/libs-native/System.Native/libSystem.Native.a"
    ilc = repo / "artifacts/bin/coreclr/ios.arm64.Release/arm64/ilc/ilc"
    dotnet = repo / ".dotnet/dotnet"
    sdk_version = json.loads((repo / "global.json").read_text())["sdk"]["version"]
    csc = repo / f".dotnet/sdk/{sdk_version}/Roslyn/bincore/csc.dll"
    for required in [sdk / "SDKSettings.plist", core / "System.Private.CoreLib.dll",
                     sfx / "System.Console.dll", native / "libRuntime.WorkstationGC.a",
                     system_native, ilc, dotnet, csc]:
        if not required.is_file():
            raise SystemExit(f"缺少构建输入：{required}。请先按 README 构建 ARM32 运行时、托管库和本地 ILC。")

    # A tools-only rebuild can recreate the managed ILC output without its
    # separately built native backends. Refresh those from the host build.
    host_tools = ilc.parent.parent
    sidecars = [host_tools / "libjitinterface_arm64.dylib", *host_tools.glob("libclrjit_*.dylib")]
    for sidecar in sidecars:
        if not sidecar.is_file():
            raise SystemExit(f"缺少本机 JIT 后端：{sidecar}。请先构建 clr.alljits。")
        shutil.copy2(sidecar, ilc.parent / sidecar.name)

    # CoreLib and its layout-sensitive companions must come from the ARM build.
    references = {p.name: p for p in sfx.glob("*.dll")}
    references.update({p.name: p for p in core.glob("*.dll")})
    tests = repo / "ports/ios-legacy/tests/managed"
    source = tests / ("Arm32Hello.cs" if args.minimal else "Program.cs")
    run([dotnet, csc, "/nologo", "/noconfig", "/nostdlib", "/target:exe",
         "/unsafe", "/nullable:enable", "/optimize+", f"/out:{out / (name + '.dll')}",
         *[f"/r:{p}" for p in references.values()], source], out / "csc.log")

    features = {
        "System.Globalization.Invariant": True,
        "System.Globalization.PredefinedCulturesOnly": True,
        "System.Diagnostics.Tracing.EventSource.IsSupported": False,
        "System.Diagnostics.Debugger.IsSupported": False,
        "System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported": False,
        "System.Reflection.Metadata.MetadataUpdater.IsSupported": False,
        "System.Runtime.InteropServices.BuiltInComInterop.IsSupported": False,
        "System.Runtime.InteropServices.EnableConsumingManagedCodeFromNativeHosting": False,
        "System.Threading.Thread.EnableAutoreleasePool": False,
    }
    arguments = [str(out / (name + ".dll")), f"-o:{out / (name + '.o')}",
                 "--targetos:ios", "--targetarch:arm", "--macho-minimum-os-version:7.0",
                 "--noinlinetls", "-O", "--directpinvoke:__Internal", "--directpinvoke:System.Native",
                 "--generateunmanagedentrypoints:System.Private.CoreLib,HIDDEN",
                 "--runtimeknob:RUNTIME_IDENTIFIER=ios-arm", "--initassembly:System.Private.CoreLib"]
    arguments += [f"-r:{p}" for p in references.values()]
    if args.minimal:
        arguments.append("--reflectiondata:none")
    else:
        arguments += ["--stacktracedata", "--scanreflection"]
        arguments += [f"--initassembly:{x}" for x in ["System.Private.TypeLoader",
                      "System.Private.Reflection.Execution", "System.Private.StackTraceMetadata"]]
    for feature, enabled in features.items():
        value = str(enabled).lower()
        arguments += [f"--feature:{feature}={value}", f"--runtimeknob:{feature}={value}"]
    response = out / (name + ".rsp")
    response.write_text("\n".join('"' + x + '"' for x in arguments) + "\n", encoding="utf-8")
    run([ilc, "@" + str(response)], out / "ilc.log")

    callback = out / "callback.o"
    run(["xcrun", "clang", "-target", "armv7-apple-ios7.0", "-isysroot", sdk,
         "-c", tests / ("arm32-hello.c" if args.minimal else "callback.c"), "-o", callback], out / "clang.log")
    libraries = ["libbootstrapper.o", "libRuntime.WorkstationGC.a", "libeventpipe-disabled.a",
                 "libstandalonegc-disabled.a", "libaotminipal.a", "libstdc++compat.a"]
    binary = out / name
    run(["xcrun", "clang", "-target", "armv7-apple-ios7.0", "-isysroot", sdk,
         "-Wl,-dead_strip", "-Wl,-no_compact_unwind", "-Wl,-map," + str(out / "link.map"),
         out / (name + ".o"), callback, *[native / x for x in libraries], system_native,
         "-lc++", "-liconv", "-lz", "-framework", "Foundation", "-framework", "Security",
         "-framework", "CoreFoundation", "-o", binary], out / "link.log")
    if args.sign:
        run(["codesign", "--force", "--sign", "-", "--digest-algorithm=sha1,sha256", binary], out / "sign.log")
        run(["codesign", "--verify", "--strict", binary], out / "sign-verify.log")
    print(f"已生成 ARMv7 探针：{binary}")
    print("编译和链接成功不等于真机测试通过。")


if __name__ == "__main__":
    main()
