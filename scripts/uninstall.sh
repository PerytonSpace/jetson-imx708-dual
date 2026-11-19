#!/usr/bin/env bash
# Remove the IMX708 dual-CSI kernel module, overlay, merged DTB, and boot entry.
# Restores DEFAULT primary in /boot/extlinux/extlinux.conf.
#
# Usage:
#   sudo ./scripts/uninstall.sh

set -euo pipefail

if [[ $(id -u) -ne 0 ]]; then
  echo "Error: run this script with sudo." >&2
  exit 1
fi

KERNEL_VER=$(uname -r)
EXTLINUX="/boot/extlinux/extlinux.conf"
LABEL_NAME="imx708-dual"

echo "=== Uninstalling IMX708 Dual-CSI Driver ==="

if [[ -f "$EXTLINUX" ]] && command -v python3 >/dev/null 2>&1; then
  python3 - "$EXTLINUX" "$LABEL_NAME" <<'PY'
import os, re, sys

extlinux_path, label_name = sys.argv[1], sys.argv[2]
lines = open(extlinux_path, "r", encoding="utf-8").read().splitlines()

header = []
blocks = []
current = None
for line in lines:
    if re.match(r"^\s*LABEL\s+", line, re.IGNORECASE):
        if current is not None:
            blocks.append(current)
        name = line.strip().split()[1]
        current = (name, [line])
    elif current is None:
        header.append(line)
    else:
        current[1].append(line)
if current is not None:
    blocks.append(current)

kept_blocks = [(n, b) for (n, b) in blocks if n != label_name]
fallback = "primary" if any(n == "primary" for n, _ in kept_blocks) else (kept_blocks[0][0] if kept_blocks else "primary")

new_header = []
for line in header:
    if re.match(r"^\s*DEFAULT\s+", line, re.IGNORECASE):
        cur_def = line.strip().split()[1] if len(line.strip().split()) > 1 else ""
        if cur_def == label_name:
            new_header.append(f"DEFAULT {fallback}")
        else:
            new_header.append(line)
    else:
        new_header.append(line)

out_lines = list(new_header)
for _, blines in kept_blocks:
    if out_lines and out_lines[-1].strip() != "":
        out_lines.append("")
    out_lines.extend(blines)

tmp_path = extlinux_path + ".tmp"
with open(tmp_path, "w", encoding="utf-8") as fh:
    fh.write("\n".join(out_lines) + "\n")
os.replace(tmp_path, extlinux_path)
PY
  echo "  Reverted    : $EXTLINUX (DEFAULT primary, removed LABEL ${LABEL_NAME})"
fi

rm -f "/lib/modules/${KERNEL_VER}/updates/drivers/media/i2c/nv_imx708.ko"
rm -f /etc/modules-load.d/nv_imx708.conf
rm -f /boot/tegra234-p3767-camera-p3768-imx708-dual.dtbo
rm -f /boot/dtb/*-imx708-dual.dtb /boot/*-imx708-dual.dtb
depmod -a "$KERNEL_VER"

echo "  Removed     : nv_imx708.ko, autoload config, .dtbo, and merged DTB(s)"
echo ""
echo "Uninstall complete. Reboot ('sudo reboot') to boot back into the stock DTB."
