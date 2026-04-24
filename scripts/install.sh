#!/usr/bin/env bash
# Installer bundled inside canscope release tarballs.
# Reads the layout next to this script (bin/, optional canscope/lib/, share/, etc/)
# and copies it to PREFIX (default /usr/local) + system data/config dirs.
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"

BIN_DIR="${DESTDIR}${PREFIX}/bin"
LIB_DIR="${DESTDIR}${PREFIX}/canscope/lib"
DATA_DIR="${DESTDIR}/usr/share/canscope"
CONFIG_DIR="${DESTDIR}/etc/canscope"

cd "$(dirname "$(readlink -f "$0")")"

if [ ! -f bin/canscope ]; then
    echo "Error: bin/canscope not found. This script must be run from the unpacked tarball." >&2
    exit 1
fi

install -d "$BIN_DIR" "$DATA_DIR" "$CONFIG_DIR"
install -m 755 bin/canscope "$BIN_DIR/"

if [ -d canscope/lib ]; then
    install -d "$LIB_DIR"
    cp -a canscope/lib/. "$LIB_DIR/"
    echo "Installed shared libraries to $LIB_DIR"
fi

if [ -d share/canscope ]; then
    install -m 644 share/canscope/* "$DATA_DIR/" 2>/dev/null || true
fi

if [ -d etc/canscope ]; then
    install -m 644 etc/canscope/* "$CONFIG_DIR/" 2>/dev/null || true
fi

cat <<EOF
canscope installed.

  Binary : $BIN_DIR/canscope
  Data   : $DATA_DIR
  Config : $CONFIG_DIR

Run: canscope -h
EOF
