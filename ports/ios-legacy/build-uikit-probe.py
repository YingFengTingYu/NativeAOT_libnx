#!/usr/bin/env python3
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

"""构建由 C# 直接调用 UIKit 的最小应用。"""

import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=["arm", "arm64"], default="arm64")
    parser.add_argument("--sdk", type=Path, default=Path.home() / "SDKs/iPhoneOS9.3.sdk")
    args = parser.parse_args()
    port = Path(__file__).resolve().parent
    out = port.parents[1] / "artifacts/legacy-ios/uikit" / ("ios-" + args.arch)
    number = "32" if args.arch == "arm" else "64"
    subprocess.run([sys.executable, str(port / "publish-project.py"), str(port / "examples/UIKit/UIKit.csproj"),
                    "--arch", args.arch, "--sdk", str(args.sdk), "--output", str(out / "code"), "--link-framework", "UIKit"], check=True)
    subprocess.run([sys.executable, str(port / "package-app.py"), str(out / "code/publish"),
                    "--bundle-id", "org.nativeaot.legacy.uikit.arm" + number,
                    "--display-name", "AOT " + number, "--app-name", "NativeAOTUIKit" + number,
                    "--url-scheme", "nativeaot-uikit" + number,
                    "--output", str(out / "package"), "--sign"], check=True)


if __name__ == "__main__":
    main()
