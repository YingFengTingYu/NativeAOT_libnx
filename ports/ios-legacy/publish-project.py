#!/usr/bin/env python3
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

"""将普通 net10.0 可执行项目发布为 iOS 7 ARMv7/ARM64 NativeAOT 命令行程序。"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


REPO = Path(__file__).resolve().parents[2]
PORT = Path(__file__).resolve().parent


def run(command, log, cwd=REPO):
    command = [str(value) for value in command]
    log.with_suffix(".command.json").write_text(json.dumps(command, indent=2) + "\n")
    with log.open("w") as stream:
        result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(errors="replace")[-16000:])
        raise SystemExit(f"构建失败（{result.returncode}），完整日志：{log}")


def require(path):
    if not path.is_file():
        raise SystemExit(f"缺少构建输入：{path}。请按 ports/ios-legacy/README.md 构建对应架构的运行时和基础库。")
    return path


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path, help="普通 SDK 风格的 .csproj 路径")
    parser.add_argument("--arch", choices=["arm", "arm64"], default="arm64")
    parser.add_argument("--sdk", type=Path, default=Path.home() / "SDKs/iPhoneOS9.3.sdk")
    parser.add_argument("--output", type=Path, help="专用输出目录，默认在仓库 artifacts 下")
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--framework", help="多目标项目必须选择 net10.0")
    parser.add_argument("--native-source", type=Path, action="append", default=[], help="额外的 C / Objective-C 源文件，可重复")
    parser.add_argument("--native-library", type=Path, action="append", default=[], help="额外的 .a / .o 文件，可重复")
    parser.add_argument("--link-framework", action="append", default=[], help="额外的 Apple framework 名称，可重复")
    parser.add_argument("--direct-pinvoke", action="append", default=[], help="静态解析的 DllImport 库名，可重复")
    parser.add_argument("--sign", action="store_true", help="生成越狱测试用 ad-hoc SHA-1/SHA-256 签名")
    parser.add_argument("--strip", action="store_true", help="移除本地原生符号，完整符号程序保留在工作目录")
    args = parser.parse_args()
    project = require(args.project.expanduser().resolve())
    if project.suffix != ".csproj":
        parser.error("输入必须是 .csproj。")
    sdk = args.sdk.expanduser().resolve()
    require(sdk / "SDKSettings.plist")
    rid = "ios-" + args.arch
    core = REPO / f"artifacts/bin/coreclr/ios.{args.arch}.Release/aotsdk"
    sfx = REPO / f"artifacts/bin/runtime/net10.0-ios-Release-{args.arch}"
    native = REPO / f"artifacts/bin/coreclr/ios.{args.arch}.Release/legacy-ios/aotsdk"
    system_native = REPO / f"artifacts/obj/coreclr/ios.{args.arch}.Release/legacy-ios/libs-native/System.Native/libSystem.Native.a"
    ilc = REPO / "artifacts/bin/coreclr/ios.arm64.Release/arm64/ilc/ilc"
    dotnet = REPO / ".dotnet/dotnet"
    libraries = [native / name for name in ["libbootstrapper.o", "libRuntime.WorkstationGC.a",
                 "libeventpipe-disabled.a", "libstandalonegc-disabled.a", "libaotminipal.a", "libstdc++compat.a"]]
    for path in [core / "System.Private.CoreLib.dll", sfx / "System.Console.dll", system_native, ilc, dotnet, *libraries]:
        require(path)
    for source in args.native_source:
        require(source.expanduser().resolve())
        if source.suffix not in [".c", ".m"]:
            parser.error("--native-source 当前只接受 .c / .m；其他语言请先编译为 .a / .o。")

    project_key = project.stem + "-" + hashlib.sha256(str(project).encode()).hexdigest()[:8]
    out = (args.output or REPO / "artifacts/legacy-ios/projects" / project_key / rid).expanduser().resolve()
    marker = out / ".legacy-ios-output.json"
    identity = {"project": str(project), "rid": rid}
    if out.exists() and any(out.iterdir()) and (not marker.is_file() or json.loads(marker.read_text()) != identity):
        parser.error(f"输出目录非空或属于另一项目/架构，请选择专用空目录：{out}")
    out.mkdir(parents=True, exist_ok=True)
    write_json(marker, identity)
    work = Path(tempfile.mkdtemp(prefix="build-", dir=out))
    print(f"构建 {project.name} → {rid}；日志：{work}", flush=True)

    # Let the SDK evaluate and compile the real project graph, including source
    # generators, resources and managed packages. The managed stage has no RID;
    # target-specific CoreLib and framework implementations are supplied to ILC.
    properties = [f"-p:Configuration={args.configuration}", "-p:PublishAot=false", "-p:PublishTrimmed=false",
                  f"-p:LegacyIOSArchitecture={args.arch}", "-p:LegacyIOSMinimumOSVersion=7.0",
                  "-p:RuntimeIdentifier=", "-p:RuntimeIdentifiers=", "-p:SelfContained=false", "-p:UseAppHost=false",
                  "-p:PublishSingleFile=false", "-p:PublishReadyToRun=false", "-p:UseArtifactsOutput=true",
                  f"-p:ArtifactsPath={work / 'managed'}", "-p:InvariantGlobalization=true", "-p:EventSourceSupport=false",
                  "-p:BuildInParallel=false"]
    if args.framework:
        properties.append(f"-p:TargetFramework={args.framework}")
    property_names = ["TargetFramework", "TargetFrameworks", "OutputType", "AssemblyName", "TargetPath", "PublishDir",
                      "ProjectAssetsFile", "AllowUnsafeBlocks", "DefineConstants", "OptimizationPreference"]
    item_names = ["NativeLibrary", "DirectPInvoke", "IlcArg", "RuntimeHostConfigurationOption", "ResolvedFileToPublish"]
    metadata = work / "project.json"
    query = ["-getProperty:" + ",".join(property_names), "-getItem:" + ",".join(item_names), "-getResultOutputFile:" + str(metadata)]
    msbuild = [dotnet, "msbuild", project, "-nologo", *properties, *query]
    run(msbuild, work / "evaluate.log")
    evaluated = json.loads(metadata.read_text())["Properties"]
    if evaluated["TargetFramework"] != "net10.0" or (evaluated["TargetFrameworks"] and not args.framework):
        raise SystemExit("当前支持普通 net10.0 项目；多目标项目请传 --framework net10.0。iOS workload 项目尚未接入。")
    if evaluated["OutputType"].lower() != "exe":
        raise SystemExit("项目需要 OutputType=Exe；原生库输出尚未接入。应用可在生成可执行文件后使用 package-app.py 打包。")
    name = evaluated["AssemblyName"]
    if not name or Path(name).name != name or name in [".", ".."]:
        raise SystemExit("AssemblyName 必须是有效的文件名。")
    run([*msbuild, "-restore", "-t:Publish"], work / "managed.log")
    result = json.loads(metadata.read_text())
    published = Path(result["Properties"]["PublishDir"])
    items = result["Items"]
    entry = require(published / (name + ".dll"))

    # A RID-neutral restore cannot select iOS native / managed runtime assets.
    # Fail visibly instead of embedding a host library or silently losing them.
    assets = json.loads(Path(result["Properties"]["ProjectAssetsFile"]).read_text())
    rid_packages = sorted({key for target in assets["targets"].values() for key, value in target.items()
                           if value.get("runtimeTargets") or value.get("native")})
    if rid_packages:
        raise SystemExit("暂不支持自动选择含 RID 专属资产的 NuGet 包：" + ", ".join(rid_packages) +
                         "。请在项目中显式拆分纯托管依赖与 iOS 原生库。")

    references = {p.name: p for p in sorted(sfx.glob("*.dll"))}
    managed_files = list(published.rglob("*.dll"))
    satellites = []
    for path in managed_files:
        if path.name.endswith(".resources.dll"):
            satellites.append(path)
            continue
        if path != entry:
            if path.name in {p.name for p in core.glob("*.dll")}:
                raise SystemExit(f"项目不能覆盖 NativeAOT CoreLib 或运行时辅助程序集：{path.name}")
            if path.name in references and references[path.name].is_relative_to(published):
                raise SystemExit(f"发布目录存在同名程序集：{path.name}")
            references[path.name] = path
    references.update({p.name: p for p in core.glob("*.dll")})

    for sidecar in [ilc.parent.parent / "libjitinterface_arm64.dylib", *ilc.parent.parent.glob("libclrjit_*.dylib")]:
        require(sidecar)
        destination = ilc.parent / sidecar.name
        if not destination.is_file() or (sidecar.stat().st_size, sidecar.stat().st_mtime_ns) != (destination.stat().st_size, destination.stat().st_mtime_ns):
            shutil.copy2(sidecar, destination)
    require(ilc.parent / f"libclrjit_universal_{args.arch}_arm64.dylib")
    features = {"System.Globalization.Invariant": "true", "System.Globalization.PredefinedCulturesOnly": "true",
                "System.Diagnostics.Tracing.EventSource.IsSupported": "false", "System.Diagnostics.Debugger.IsSupported": "false",
                "System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported": "false",
                "System.Reflection.Metadata.MetadataUpdater.IsSupported": "false",
                "System.StartupHookProvider.IsSupported": "false",
                "System.Resources.ResourceManager.AllowCustomResourceTypes": "false",
                "System.Runtime.InteropServices.BuiltInComInterop.IsSupported": "false",
                "System.Runtime.InteropServices.EnableConsumingManagedCodeFromNativeHosting": "false",
                "System.Threading.Thread.EnableAutoreleasePool": "false", "System.GC.Server": "false"}
    for item in items["RuntimeHostConfigurationOption"]:
        key, value = item["Identity"], item["Value"]
        if key in features and value.lower() != features[key]:
            raise SystemExit(f"当前运行时配置不支持 {key}={value}。")
        features[key] = value
    obj = work / (name + ".o")
    arguments = [str(entry), f"-o:{obj}", "--targetos:ios", f"--targetarch:{args.arch}", "--macho-minimum-os-version:7.0",
                 "--noinlinetls", "-O", "--stacktracedata", "--scanreflection", "--generateunmanagedentrypoints:System.Private.CoreLib,HIDDEN",
                 f"--runtimeknob:RUNTIME_IDENTIFIER={rid}"]
    arguments += [f"-r:{p}" for p in references.values()]
    arguments += [f"--satellite:{p}" for p in satellites]
    optimization = result["Properties"]["OptimizationPreference"].lower()
    if optimization in ["speed", "size"]:
        arguments.append("--Ot" if optimization == "speed" else "--Os")
    arguments += [f"--initassembly:{x}" for x in ["System.Private.CoreLib", "System.Private.TypeLoader",
                  "System.Private.Reflection.Execution", "System.Private.StackTraceMetadata"]]
    direct = {"__Internal", "System.Native", *args.direct_pinvoke, *[x["Identity"] for x in items["DirectPInvoke"]]}
    arguments += [f"--directpinvoke:{x}" for x in sorted(direct)]
    for key, value in features.items():
        arguments.append(f"--runtimeknob:{key}={value}")
        if value.lower() in ["true", "false"]:
            arguments.append(f"--feature:{key}={value.lower()}")
    for item in items["IlcArg"]:
        value = item["Identity"]
        if value == "--noinlinetls":
            continue
        if value.startswith(("-o:", "-r:", "--target", "--macho-minimum", "--feature:", "--runtimeknob:", "@")):
            raise SystemExit(f"IlcArg 不能覆盖通用入口的目标和运行时配置：{value}")
        arguments.append(value)
    response = work / "ilc.rsp"
    if any('"' in value or "\n" in value or "\r" in value for value in arguments):
        raise SystemExit("ILC 参数不支持引号或换行符。")
    response.write_text("\n".join('"' + value + '"' for value in arguments) + "\n", encoding="utf-8")
    run([ilc, "@" + str(response)], work / "ilc.log", cwd=project.parent)

    triple = ("armv7" if args.arch == "arm" else "arm64") + "-apple-ios7.0"
    extra_objects = []
    for index, source in enumerate(args.native_source):
        output = work / f"native-{index}.o"
        run(["xcrun", "clang", "-target", triple, "-isysroot", sdk, "-c", source.expanduser().resolve(), "-o", output],
            work / f"native-{index}.log")
        extra_objects.append(output)
    extra_libraries = [p.expanduser().resolve() for p in args.native_library]
    extra_libraries += [Path(x["FullPath"]) for x in items["NativeLibrary"]]
    expected_arch = "armv7" if args.arch == "arm" else "arm64"
    for path in extra_libraries:
        require(path)
        if path.suffix not in [".a", ".o"]:
            raise SystemExit(f"当前只链接静态 .a / .o 文件：{path}")
        archs = subprocess.check_output(["xcrun", "lipo", "-archs", str(path)], text=True).split()
        if expected_arch not in archs:
            raise SystemExit(f"原生库缺少 {expected_arch}：{path}（{archs}）")
    frameworks = sorted({"Foundation", "Security", "CoreFoundation", *args.link_framework})
    if any(not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value) for value in frameworks):
        raise SystemExit("--link-framework 需要 framework 名称。")
    stage = work / "publish"
    stage.mkdir()
    binary = stage / name
    linker = ["xcrun", "clang", "-target", triple, "-isysroot", sdk, "-Wl,-dead_strip", "-Wl,-map," + str(work / "link.map")]
    if args.arch == "arm":
        linker += ["-Wl,-no_compact_unwind", "-Wl,-keep_dwarf_unwind", "-Wl,-segprot,__AOT,rx,rx"]
    linker += [obj, *extra_objects, *extra_libraries, *libraries, system_native, "-lc++", "-liconv", "-lz"]
    for framework in frameworks:
        linker += ["-framework", framework]
    run([*linker, "-o", binary], work / "link.log")
    if args.strip:
        shutil.copy2(binary, work / (name + ".unstripped"))
        run(["xcrun", "strip", "-x", binary], work / "strip.log")
    if args.sign:
        run(["codesign", "--force", "--sign", "-", "--digest-algorithm=sha1,sha256", binary], work / "sign.log")
        run(["codesign", "--verify", "--strict", binary], work / "sign-verify.log")
    audit_command = ["python3", PORT / "audit-probe.py", "--arch", args.arch, "--binary", binary, "--report", work / "audit.json"]
    if args.strip:
        audit_command += ["--symbols-binary", work / (name + ".unstripped")]
    run(audit_command, work / "audit.log")

    # Embed managed resources in AOT; carry ordinary publish content beside the
    # executable. Reject native payloads which this entry point did not link.
    for path in published.rglob("*"):
        if not path.is_file() or path.suffix in [".dll", ".pdb"] or path.name in [name + ".deps.json", name + ".runtimeconfig.json"]:
            continue
        if path.suffix in [".so", ".dylib", ".a", ".o"]:
            raise SystemExit(f"发布内容含未显式链接的原生文件：{path}")
        destination = stage / path.relative_to(published)
        if destination == binary or destination.name == "build-manifest.json":
            raise SystemExit(f"发布内容与输出文件重名：{path}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
    manifest = {**identity, "minimum_os": "7.0", "sdk": str(sdk), "configuration": args.configuration,
                "binary": name, "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "signed": args.sign,
                "work_directory": str(work), "static_audit_passed": True, "device_tested": False,
                "stripped": args.strip,
                "managed_dependencies": sorted(p.name for p in managed_files if p != entry), "features": features}
    write_json(stage / "build-manifest.json", manifest)
    destination = out / "publish"
    if destination.exists():
        destination.rename(work / "previous-publish")
    stage.rename(destination)
    audit = json.loads((work / "audit.json").read_text())
    audit["binary"] = str(destination / name)
    write_json(work / "audit.json", audit)
    warnings = [line for log in [work / "managed.log", work / "ilc.log"] for line in log.read_text().splitlines()
                if re.search(r"warning|警告", line, re.IGNORECASE)]
    if warnings:
        print("构建警告（完整内容见日志）：\n" + "\n".join(warnings[:20]))
    print(f"已生成：{destination / name}\n静态检查通过；尚未对本次产物进行真机验证。")
    print(f"构建清单：{destination / 'build-manifest.json'}")


if __name__ == "__main__":
    main()
