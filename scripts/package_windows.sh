#!/usr/bin/env bash
# Pack a Windows release zip (x86_64 static binary + WSL installer wrapper).
# Usage: package_windows.sh <static_build_dir> <version>
#   static_build_dir : path to the static x86_64 build dir (e.g. build/native_static)
#   version          : git tag, e.g. v0.1.0
#
# canscope is a Linux-only TUI (SocketCAN); on Windows it runs inside WSL2.
# The bundle ships the static linux x86_64 binary plus install.bat which
# delegates to the bundled install.sh via `wsl bash`.
set -euo pipefail

BUILD_DIR="$1"
VERSION="$2"

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG_NAME="canscope-${VERSION}-windows-x86_64"
STAGE="${PROJECT_ROOT}/dist/stage/${PKG_NAME}"
OUT="${PROJECT_ROOT}/dist/${PKG_NAME}.zip"

rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/share/canscope" "$STAGE/etc/canscope"

cp "${BUILD_DIR}/canscope" "$STAGE/bin/"

cp "${PROJECT_ROOT}/thirdparty/j1939da_2018.csv"  "$STAGE/share/canscope/"
cp "${PROJECT_ROOT}/thirdparty/j1939da_2018.xlsx" "$STAGE/share/canscope/"
cp "${PROJECT_ROOT}/playback.yaml"                "$STAGE/etc/canscope/"

cp "${PROJECT_ROOT}/scripts/install.sh"  "$STAGE/"
cp "${PROJECT_ROOT}/scripts/install.bat" "$STAGE/"
chmod +x "$STAGE/install.sh"

cat > "$STAGE/README.txt" <<EOF
canscope ${VERSION} — windows-x86_64 (runs inside WSL2)

canscope is a Linux-only TUI tool. On Windows it must be installed inside a
WSL2 distribution (Ubuntu, Debian, Arch, etc.).

Install:
  Double-click install.bat
  (it invokes WSL and runs install.sh with sudo inside your default distro)

After installation:
  From PowerShell or cmd:  wsl canscope -h
  From WSL shell:          canscope -h

CAN hardware on Windows-via-WSL2:
  WSL2 does not expose SocketCAN by default. Two paths:

  1. USB-CAN adapter via usbipd-win:
       https://learn.microsoft.com/en-us/windows/wsl/connect-usb
     Then load can_dev/vcan modules inside WSL (requires custom kernel).

  2. Custom WSL2 kernel built with CAN/SocketCAN support:
       https://github.com/microsoft/WSL2-Linux-Kernel
     Enable CONFIG_CAN, CONFIG_CAN_VCAN, CONFIG_CAN_SLCAN, then point WSL at it.

For ad-hoc testing without hardware, use vcan + cangen or feed a recorded
candump log via stdin from inside WSL.
EOF

mkdir -p "$(dirname "$OUT")"
( cd "${PROJECT_ROOT}/dist/stage" && zip -qr "$OUT" "$PKG_NAME" )
rm -rf "$STAGE"

echo "Built: $OUT ($(du -h "$OUT" | cut -f1))"
