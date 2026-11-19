#!/usr/bin/env bash
# Build a clean standalone source + overlay + installer release tarball
# suitable for copying directly to a Jetson Orin Nano.
#
# Usage:
#   ./packaging/build_tarball.sh [OUT_DIR]

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DRIVER_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
VERSION=$(tr -d '[:space:]' < "$DRIVER_DIR/VERSION")
OUT_DIR="${1:-$DRIVER_DIR/dist}"
PKG_STEM="jetson-imx708-dual-${VERSION}"
TAR_PATH="${OUT_DIR}/${PKG_STEM}.tar.gz"

mkdir -p "$OUT_DIR"

TMP_STAGE=$(mktemp -d)
trap 'rm -rf "$TMP_STAGE"' EXIT

STAGE="${TMP_STAGE}/${PKG_STEM}"
mkdir -p "$STAGE/include" "$STAGE/src" "$STAGE/dts" "$STAGE/scripts" "$STAGE/debian" "$STAGE/packaging"

cp "$DRIVER_DIR/VERSION" "$DRIVER_DIR/COPYING" "$DRIVER_DIR/LICENSE" \
   "$DRIVER_DIR/Makefile" "$DRIVER_DIR/dkms.conf" "$DRIVER_DIR/README.md" "$STAGE/"
cp "$DRIVER_DIR/include/imx708.h" "$STAGE/include/"
cp "$DRIVER_DIR/src/Makefile" "$DRIVER_DIR/src/imx708_mode_tbls.h" \
   "$DRIVER_DIR/src/nv_imx708.c" "$STAGE/src/"
cp "$DRIVER_DIR/dts/tegra234-p3767-camera-p3768-imx708-dual-r36.dts" \
   "$DRIVER_DIR/dts/tegra234-p3767-camera-p3768-imx708-dual-r35.dts" "$STAGE/dts/"
cp "$DRIVER_DIR/scripts/install.sh" "$DRIVER_DIR/scripts/apply_overlay.sh" \
   "$DRIVER_DIR/scripts/uninstall.sh" "$DRIVER_DIR/scripts/verify.sh" \
   "$DRIVER_DIR/scripts/sync_to_jetson.sh" "$STAGE/scripts/"
cp "$DRIVER_DIR/debian/control" "$DRIVER_DIR/debian/changelog" \
   "$DRIVER_DIR/debian/copyright" "$DRIVER_DIR/debian/rules" \
   "$DRIVER_DIR/debian/postinst" "$DRIVER_DIR/debian/prerm" "$STAGE/debian/"
cp "$DRIVER_DIR/packaging/build_deb.py" "$DRIVER_DIR/packaging/build_tarball.sh" "$STAGE/packaging/"

chmod 0755 "$STAGE/scripts/"*.sh "$STAGE/debian/rules" \
           "$STAGE/debian/postinst" "$STAGE/debian/prerm" \
           "$STAGE/packaging/build_deb.py" "$STAGE/packaging/build_tarball.sh"

# Suppress macOS AppleDouble (._*) metadata files when archiving on macOS
COPYFILE_DISABLE=1 tar -czf "${TAR_PATH}.tmp" -C "$TMP_STAGE" "$PKG_STEM"
mv "${TAR_PATH}.tmp" "$TAR_PATH"

echo "Built release tarball: ${TAR_PATH} ($(wc -c < "$TAR_PATH" | tr -d ' ') bytes)"
