#!/bin/sh
# Licensed to the .NET Foundation under one or more agreements.
# The .NET Foundation licenses this file to you under the MIT license.

# Run on the jailbroken iPad, beside the two generated .tar.gz archives.
set -eu
case "${1:-}" in ''|--check) ;; *) printf 'Usage: install-uikit-device.sh [--check]\n'; exit 2 ;; esac
cd "$(dirname "$0")"
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:$PATH
[ "$(id -u)" = 0 ] || { printf 'Run this script in a root shell on the device.\n'; exit 1; }
state_dir=/usr/local/libexec/NativeAOTUIKitTools
mkdir -p "$state_dir"
for number in 32 64; do
    archive=NativeAOTUIKit$number.tar.gz
    [ -f "$archive" ] || { printf 'Missing %s\n' "$archive"; exit 1; }
    tar -tzf "$archive" > "$state_dir/members-$number.txt"
    while IFS= read -r member; do
        case "$member" in
            NativeAOTUIKit$number.app|NativeAOTUIKit$number.app/*) ;;
            *) printf 'Unexpected archive entry: %s\n' "$member"; exit 1 ;;
        esac
        case "/$member/" in */../*|*/./*) printf 'Invalid archive path\n'; exit 1 ;; esac
    done < "$state_dir/members-$number.txt"
    app_dir=/Applications/NativeAOTUIKit$number.app
    if [ -e "$app_dir" ] && [ ! -f "$state_dir/owns-$number" ]; then
        printf 'Refusing to replace an unowned application: %s\n' "$app_dir"
        exit 1
    fi
done
[ "${1:-}" != --check ] || { printf 'PASS UIKit archives and installation paths\n'; exit 0; }

# Only terminate our test executables after checking their full process path.
ps -A -o pid= -o comm= | while read -r process_id executable; do
    case "$executable" in
        /Applications/NativeAOTUIKit32.app/LegacyUIKit32|/Applications/NativeAOTUIKit64.app/LegacyUIKit64)
            printf 'Stopping test application %s\n' "$process_id"
            kill -TERM "$process_id" || true
            ;;
    esac
done
sleep 1
for number in 32 64; do
    app_dir=/Applications/NativeAOTUIKit$number.app
    staging=$state_dir/stage-$number
    mkdir -p "$staging"
    tar -xzf "NativeAOTUIKit$number.tar.gz" -C "$staging"
    [ -f "$staging/NativeAOTUIKit$number.app/LegacyUIKit$number" ]
    chown -R root:wheel "$staging/NativeAOTUIKit$number.app"
    chmod 755 "$staging/NativeAOTUIKit$number.app" "$staging/NativeAOTUIKit$number.app/LegacyUIKit$number"
    if [ -d "$app_dir" ]; then
        rm -rf "$state_dir/previous-$number.app"
        mv "$app_dir" "$state_dir/previous-$number.app"
    fi
    mv "$staging/NativeAOTUIKit$number.app" "$app_dir"
    rmdir "$staging"
    printf 'org.nativeaot.legacy.uikit.arm%s\n' "$number" > "$state_dir/owns-$number"
done
uicache
printf 'Installed AOT 32 and AOT 64. Open them from the Home screen.\n'
