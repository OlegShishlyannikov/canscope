#!/usr/bin/env bash
# Pack a Linux release tarball from a finished build directory.
# Usage: package_linux.sh <arch> <linkage> <build_dir> <version> [os_tag]
#   arch    : x86_64 | arm64
#   linkage : dynamic | static
#   build_dir : path to cmake build dir (e.g. build/native)
#   version : git tag, e.g. v0.1.0
#   os_tag  : optional, appended only for dynamic builds (e.g. ubuntu-24.04, manjaro)
#             — meaningful because dynamic builds link against a specific distro's
#             system libs (libboost, libsqlite3, libicu, libz). Static builds are
#             self-contained and skip the suffix.
#
# Layout produced under dist/canscope-${VERSION}-linux-${ARCH}-${LINKAGE}[-${OS}]/:
#   bin/canscope
#   canscope/lib/*.so*           (only when shared libs are present)
#   share/canscope/{xlsx,csv}
#   etc/canscope/playback.yaml
#   install.sh
#   README.txt
set -euo pipefail

ARCH="$1"
LINKAGE="$2"
BUILD_DIR="$3"
VERSION="$4"
OS_TAG="${5:-}"

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG_NAME="canscope-${VERSION}-linux-${ARCH}-${LINKAGE}"
if [ "$LINKAGE" = "dynamic" ] && [ -n "$OS_TAG" ]; then
    PKG_NAME="${PKG_NAME}-${OS_TAG}"
fi

STAGE="${PROJECT_ROOT}/dist/stage/${PKG_NAME}"
OUT="${PROJECT_ROOT}/dist/${PKG_NAME}.tar.gz"

rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/share/canscope" "$STAGE/etc/canscope"

cp "${BUILD_DIR}/canscope" "$STAGE/bin/"

# Bundle shared libraries and re-stamp RPATH only for dynamic builds.
# Static builds may still have stray .so files in _deps from transitive
# dependencies that ignore BUILD_SHARED_LIBS=OFF (e.g. zlib), but they are
# not linked into the static binary and running patchelf on a static ELF
# fails with "cannot find section '.dynamic'".
if [ "$LINKAGE" = "dynamic" ]; then
    LIB_FOUND=0
    for src in "${BUILD_DIR}/_deps" "${BUILD_DIR}/lely-install/lib"; do
        if [ -d "$src" ]; then
            while IFS= read -r -d '' so; do
                if [ "$LIB_FOUND" -eq 0 ]; then
                    mkdir -p "$STAGE/canscope/lib"
                    LIB_FOUND=1
                fi
                cp -a "$so" "$STAGE/canscope/lib/"
            done < <(find "$src" \( -name '*.so' -o -name '*.so.*' \) \( -type f -o -type l \) -print0)
        fi
    done

    if [ "$LIB_FOUND" -eq 1 ]; then
        patchelf --set-rpath '$ORIGIN/../canscope/lib' "$STAGE/bin/canscope"
    fi
fi

cp "${PROJECT_ROOT}/thirdparty/j1939da_2018.csv"  "$STAGE/share/canscope/"
cp "${PROJECT_ROOT}/thirdparty/j1939da_2018.xlsx" "$STAGE/share/canscope/"
cp "${PROJECT_ROOT}/playback.yaml"                "$STAGE/etc/canscope/"
cp "${PROJECT_ROOT}/scripts/install.sh"           "$STAGE/"
chmod +x "$STAGE/install.sh"

BUILD_TARGET_LINE="linux/${ARCH} (${LINKAGE})"
if [ "$LINKAGE" = "dynamic" ] && [ -n "$OS_TAG" ]; then
    BUILD_TARGET_LINE="${BUILD_TARGET_LINE}, built against ${OS_TAG}"
fi

cat > "$STAGE/README.txt" <<EOF
canscope ${VERSION} — ${BUILD_TARGET_LINE}

Quick install:
  sudo PREFIX=/usr/local ./install.sh

This installs:
  /usr/local/bin/canscope
  /usr/local/canscope/lib/*.so          (dynamic builds only)
  /usr/share/canscope/{xlsx,csv}
  /etc/canscope/playback.yaml

Run from the unpacked tarball without installing:
  ./bin/canscope -j1939-csv share/canscope/j1939da_2018.csv -e "candump can0"

System library requirements (dynamic build only):
  libboost-regex, libsqlite3, libicu, libzlib
  This dynamic build was linked against ${OS_TAG:-the build host} system libs;
  binary-compatible distros should work, but matching the build OS is safest.
    Debian/Ubuntu: apt install libboost-regex-dev libsqlite3-0 libicu-dev zlib1g
    Arch/Manjaro:  pacman -S boost icu sqlite3 zlib

Static builds have no runtime dependencies beyond glibc.
EOF

mkdir -p "$(dirname "$OUT")"
tar -C "${PROJECT_ROOT}/dist/stage" -czf "$OUT" "$PKG_NAME"
rm -rf "$STAGE"

echo "Built: $OUT ($(du -h "$OUT" | cut -f1))"
