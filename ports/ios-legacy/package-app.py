#!/usr/bin/env python3
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

"""把 publish-project.py 的输出打包为旧 iOS .app / IPA（越狱测试签名）。"""

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import zipfile
import zlib


def png(size, architecture):
    # A small original, code-drawn icon: A plus the architecture number.
    glyphs = {"A": [14, 17, 17, 31, 17, 17, 17], "3": [30, 1, 1, 14, 1, 1, 30],
              "2": [14, 17, 1, 2, 4, 8, 31], "6": [14, 16, 16, 30, 17, 17, 14],
              "4": [2, 6, 10, 18, 31, 2, 2]}
    base = (28, 104, 220) if architecture == "arm" else (19, 134, 122)
    pixels = bytearray(base * (size * size))
    def glyph(letter, left, top, scale):
        for y, row in enumerate(glyphs[letter]):
            for x in range(5):
                if row & (1 << (4 - x)):
                    for dy in range(scale):
                        for dx in range(scale):
                            px, py = left + x * scale + dx, top + y * scale + dy
                            offset = (py * size + px) * 3
                            pixels[offset:offset + 3] = b"\xff\xff\xff"
    scale = max(1, size // 18)
    glyph("A", (size - scale * 5) // 2, size // 9, scale)
    scale = max(1, size // 26)
    number = "32" if architecture == "arm" else "64"
    for i, letter in enumerate(number):
        glyph(letter, (size - scale * 11) // 2 + i * scale * 6, size * 3 // 5, scale)
    raw = b"".join(b"\0" + pixels[y * size * 3:(y + 1) * size * 3] for y in range(size))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("publish", type=Path, help="包含 build-manifest.json 的发布目录")
    parser.add_argument("--bundle-id", required=True)
    parser.add_argument("--display-name", required=True)
    parser.add_argument("--app-name", required=True, help="不含 .app 后缀的目录名")
    parser.add_argument("--url-scheme", help="可选的应用专用 URL scheme")
    parser.add_argument("--orientation", choices=["portrait", "landscape"], default="portrait")
    parser.add_argument("--hide-status-bar", action="store_true")
    parser.add_argument("--version", default="1.0", help="应用显示版本")
    parser.add_argument("--output", type=Path, required=True, help="专用打包输出目录")
    parser.add_argument("--sign", action="store_true", help="为越狱设备生成 ad-hoc 双摘要签名")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+", args.bundle_id):
        parser.error("--bundle-id 需要反向域名形式的标识符。")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.app_name):
        parser.error("--app-name 只接受字母、数字、下划线和连字符。")
    if args.url_scheme and not re.fullmatch(r"[a-z][a-z0-9+.-]*", args.url_scheme):
        parser.error("--url-scheme 需要不含冒号的小写 URL scheme。")
    source = args.publish.expanduser().resolve()
    manifest = json.loads((source / "build-manifest.json").read_text())
    binary_name = manifest["binary"]
    if Path(binary_name).name != binary_name or not manifest["static_audit_passed"]:
        parser.error("发布清单无效，或静态检查未通过。")
    binary = source / binary_name
    if hashlib.sha256(binary.read_bytes()).hexdigest() != manifest["sha256"]:
        parser.error("可执行文件与发布清单的哈希不一致。")
    out = args.output.expanduser().resolve()
    identity = {"source": str(source), "bundle_id": args.bundle_id, "app_name": args.app_name}
    marker = out / ".legacy-ios-app-output.json"
    if out.exists() and any(out.iterdir()) and (not marker.is_file() or json.loads(marker.read_text()) != identity):
        parser.error("输出目录非空或属于其他应用，请选择专用目录。")
    out.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps(identity, indent=2) + "\n")
    work = Path(tempfile.mkdtemp(prefix="package-", dir=out))
    bundle = work / (args.app_name + ".app")
    shutil.copytree(source, bundle, ignore=shutil.ignore_patterns("build-manifest.json"))
    (bundle / binary_name).chmod(0o755)
    arch = manifest["rid"].removeprefix("ios-")
    if arch not in ["arm", "arm64"]:
        parser.error("只支持 iOS ARM32 / ARM64 发布产物。")
    for size in [57, 114, 120, 72, 144, 76, 152]:
        (bundle / f"Icon-{size}.png").write_bytes(png(size, arch))
    info = {
        "CFBundleDevelopmentRegion": "zh_CN", "CFBundleExecutable": binary_name,
        "CFBundleIdentifier": args.bundle_id, "CFBundleName": args.app_name,
        "CFBundleDisplayName": args.display_name, "CFBundlePackageType": "APPL",
        "CFBundleInfoDictionaryVersion": "6.0", "CFBundleShortVersionString": args.version, "CFBundleVersion": "1",
        "CFBundleSupportedPlatforms": ["iPhoneOS"], "MinimumOSVersion": manifest["minimum_os"],
        "LSRequiresIPhoneOS": True, "UIDeviceFamily": [1, 2], "UIRequiresFullScreen": True,
        "UIFileSharingEnabled": True, "UISupportedInterfaceOrientations": ["UIInterfaceOrientationPortrait"],
        "UIViewControllerBasedStatusBarAppearance": False, "UIStatusBarStyle": "UIStatusBarStyleLightContent",
        "CFBundleIconFiles": ["Icon-57", "Icon-114", "Icon-120", "Icon-72", "Icon-144", "Icon-76", "Icon-152"],
        "CFBundleIcons": {"CFBundlePrimaryIcon": {"CFBundleIconFiles": ["Icon-57", "Icon-114", "Icon-120"]}},
        "CFBundleIcons~ipad": {"CFBundlePrimaryIcon": {"CFBundleIconFiles": ["Icon-72", "Icon-144", "Icon-76", "Icon-152"]}},
    }
    if args.url_scheme:
        info["CFBundleURLTypes"] = [{"CFBundleURLName": args.bundle_id, "CFBundleURLSchemes": [args.url_scheme]}]
    if args.orientation == "landscape":
        info["UISupportedInterfaceOrientations"] = ["UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"]
        info["UIInterfaceOrientation"] = "UIInterfaceOrientationLandscapeLeft"
    if args.hide_status_bar:
        info["UIStatusBarHidden"] = True
    (bundle / "Info.plist").write_bytes(plistlib.dumps(info))
    (bundle / "PkgInfo").write_bytes(b"APPL????")
    if args.sign:
        subprocess.run(["codesign", "--force", "--sign", "-", "--identifier", args.bundle_id,
                        "--digest-algorithm=sha1,sha256", str(bundle)], check=True)
        subprocess.run(["codesign", "--verify", "--strict", str(bundle)], check=True)
    elif manifest["signed"]:
        # A binary signature with a different identifier isn't a bundle signature.
        subprocess.run(["codesign", "--remove-signature", str(bundle / binary_name)], check=True)
    ipa = work / (args.app_name + ".ipa")
    with zipfile.ZipFile(ipa, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(bundle.rglob("*")):
            if path.is_file():
                archive.write(path, str(Path("Payload") / bundle.name / path.relative_to(bundle)))
    tar = work / (args.app_name + ".tar.gz")
    with tarfile.open(tar, "w:gz", format=tarfile.USTAR_FORMAT) as archive:
        archive.add(bundle, arcname=bundle.name)
    report = {**manifest, **identity, "signed": args.sign, "binary_sha256": hashlib.sha256((bundle / binary_name).read_bytes()).hexdigest(),
              "device_tested": False, "application": str(out / bundle.name)}
    (work / "app-manifest.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    for path in [bundle, ipa, tar, work / "app-manifest.json"]:
        destination = out / path.name
        if destination.exists():
            destination.rename(work / ("previous-" + path.name))
        path.rename(destination)
    print(f"已打包：{out / bundle.name}\nIPA：{out / ipa.name}\n系统目录安装归档：{out / tar.name}")
    print("签名为越狱测试用途；打包成功不等于启动或界面验证通过。")


if __name__ == "__main__":
    main()
