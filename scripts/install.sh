#!/usr/bin/env bash
# End-to-end installer for the Sony IMX708 dual-CSI driver on NVIDIA Jetson Orin Nano DevKit.
#
# Usage:
#   sudo ./scripts/install.sh [--no-apt] [--no-apply] [--base-dtb /boot/dtb/...]

set -euo pipefail

NO_APT=0
NO_APPLY=0
BASE_DTB=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-apt)   NO_APT=1; shift ;;
    --no-apply) NO_APPLY=1; shift ;;
    --base-dtb)
      if [[ $# -lt 2 ]]; then
        echo "Error: --base-dtb requires a path argument." >&2
        exit 2
      fi
      BASE_DTB="$2"
      shift 2
      ;;
    -h|--help)
      echo "Usage: sudo $0 [--no-apt] [--no-apply] [--base-dtb PATH]"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
  esac
done

if [[ $(id -u) -ne 0 ]]; then
  echo "Error: run this script with sudo." >&2
  exit 1
fi

ARCH=$(uname -m)
if [[ "$ARCH" != "aarch64" ]]; then
  echo "Error: expected aarch64 (NVIDIA Jetson), got '$ARCH'." >&2
  echo "Sync this folder to the Jetson first using ./scripts/sync_to_jetson.sh <ssh-host>." >&2
  exit 1
fi

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DRIVER_DIR=$(cd "$SCRIPT_DIR/.." && pwd)

echo "=== Sony IMX708 Dual-CSI Driver Installer (Jetson Orin Nano) ==="
if [[ -r /proc/device-tree/model ]]; then
  echo "  Board  : $(tr -d '\0' < /proc/device-tree/model)"
fi
if [[ -r /etc/nv_tegra_release ]]; then
  echo "  L4T    : $(head -n 1 /etc/nv_tegra_release)"
fi
echo "  Kernel : $(uname -r)"

L4T_MAJOR="36"
if [[ -r /etc/nv_tegra_release ]]; then
  DETECTED=$(sed -n 's/^# R\([0-9][0-9]*\).*/\1/p' /etc/nv_tegra_release | head -n 1)
  [[ -n "$DETECTED" ]] && L4T_MAJOR="$DETECTED"
fi

if [[ $NO_APT -eq 0 ]]; then
  echo ""
  echo "--- Step 1/4: Checking build dependencies ---"
  PKGS=(build-essential device-tree-compiler v4l-utils i2c-tools nvidia-l4t-kernel-headers)
  if [[ "$L4T_MAJOR" -ge 36 ]]; then
    PKGS+=(nvidia-l4t-kernel-oot-headers)
  fi
  apt-get update -qq
  apt-get install -y "${PKGS[@]}"
fi

echo ""
echo "--- Step 2/4: Building nv_imx708.ko and dual-CSI overlay ---"
cd "$DRIVER_DIR"
make clean
make info
make all

echo ""
echo "--- Step 3/4: Installing kernel module and .dtbo ---"
make install

if [[ $NO_APPLY -eq 0 ]]; then
  echo ""
  echo "--- Step 4/4: Applying dual-CSI overlay to boot DTB ---"
  if [[ -n "$BASE_DTB" ]]; then
    "$SCRIPT_DIR/apply_overlay.sh" /boot/tegra234-p3767-camera-p3768-imx708-dual.dtbo "$BASE_DTB"
  else
    "$SCRIPT_DIR/apply_overlay.sh" /boot/tegra234-p3767-camera-p3768-imx708-dual.dtbo
  fi
else
  echo ""
  echo "--- Step 4/4: Skipped (--no-apply) ---"
  echo "Run 'sudo ./scripts/apply_overlay.sh' when ready to activate the overlay."
fi
