#!/usr/bin/env bash
# Pre-merge the compiled IMX708 dual-CSI overlay (.dtbo) into a copy of the stock
# Jetson Orin Nano base DTB and add a safe boot entry to /boot/extlinux/extlinux.conf.
#
# Why pre-merge instead of OVERLAYS / jetson-io?
#   On JetPack 6.x (L4T R36.x) UEFI boot, extlinux.conf OVERLAYS directives are
#   frequently ignored. Pre-merging onto a copy of the stock base DTB with fdtoverlay
#   and adding a new LABEL (leaving LABEL primary untouched) works reliably across
#   JetPack 6.0 / 6.1 / 6.2 and JetPack 5.1.x.
#
# Usage:
#   sudo ./scripts/apply_overlay.sh [OVERLAY_DTBO] [BASE_DTB]

set -euo pipefail

if [[ $(id -u) -ne 0 ]]; then
  echo "Error: run this script with sudo." >&2
  exit 1
fi

OVERLAY_DTBO="${1:-/boot/tegra234-p3767-camera-p3768-imx708-dual.dtbo}"
BASE_DTB="${2:-}"
EXTLINUX="/boot/extlinux/extlinux.conf"
EXTLINUX_BAK="/boot/extlinux/extlinux.conf.pre-imx708.bak"
LABEL_NAME="imx708-dual"

for tool in fdtoverlay dtc python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Error: required tool '$tool' not found." >&2
    echo "Install with: sudo apt update && sudo apt install -y device-tree-compiler python3" >&2
    exit 1
  fi
done

if [[ ! -f "$OVERLAY_DTBO" ]]; then
  echo "Error: overlay file not found: $OVERLAY_DTBO" >&2
  echo "Build and install first with: make && sudo make install" >&2
  exit 1
fi

if [[ ! -f "$EXTLINUX" ]]; then
  echo "Error: extlinux config not found: $EXTLINUX" >&2
  exit 1
fi

detect_base_dtb() {
  # 1. Prefer the FDT explicitly listed under LABEL primary in extlinux.conf
  local from_extlinux
  from_extlinux=$(python3 - "$EXTLINUX" <<'PY'
import re, sys
text = open(sys.argv[1], "r", encoding="utf-8", errors="ignore").read()
in_primary = False
for line in text.splitlines():
    s = line.strip()
    if re.match(r"^LABEL\s+", s, re.IGNORECASE):
        in_primary = (s.split()[1] == "primary")
    elif in_primary and re.match(r"^FDT\s+", s, re.IGNORECASE):
        path = s.split(None, 1)[1].strip()
        if not path.endswith("-imx708-dual.dtb"):
            print(path)
            break
PY
)
  if [[ -n "$from_extlinux" && -f "$from_extlinux" ]]; then
    echo "$from_extlinux"
    return 0
  fi

  # 2. Match /proc/device-tree/compatible against stock p3768+p3767 DTBs in /boot/dtb and /boot
  local compat_str=""
  if [[ -r /proc/device-tree/compatible ]]; then
    compat_str=$(tr '\0' '\n' < /proc/device-tree/compatible | head -n 1 || true)
  fi

  local is_super=0
  [[ "$compat_str" == *"-super"* ]] && is_super=1

  local sku=""
  if [[ "$compat_str" =~ p3768-0000\+p3767-([0-9]{4}) ]]; then
    sku="${BASH_REMATCH[1]}"
  fi

  local -a candidates=()
  local dir f
  for dir in /boot/dtb /boot; do
    [[ -d "$dir" ]] || continue
    for f in "$dir"/*tegra234-p3768-0000+p3767-*.dtb; do
      [[ -f "$f" ]] || continue
      [[ "$f" == *"-imx708-dual.dtb" ]] && continue
      [[ "$f" == *".backup" ]] && continue
      candidates+=("$f")
    done
  done

  if [[ -n "$sku" ]]; then
    local -a matched=()
    for f in "${candidates[@]}"; do
      if [[ "$f" == *"p3767-${sku}"* ]]; then
        if [[ $is_super -eq 1 && "$f" == *"-super.dtb" ]]; then
          matched+=("$f")
        elif [[ $is_super -eq 0 && "$f" != *"-super.dtb" ]]; then
          matched+=("$f")
        fi
      fi
    done
    if [[ ${#matched[@]} -ge 1 ]]; then
      echo "${matched[0]}"
      return 0
    fi
  fi

  if [[ ${#candidates[@]} -eq 1 ]]; then
    echo "${candidates[0]}"
    return 0
  fi

  echo "Error: could not uniquely determine stock base DTB." >&2
  echo "Detected compatible: ${compat_str:-<unknown>}" >&2
  if [[ ${#candidates[@]} -gt 0 ]]; then
    echo "Candidates found:" >&2
    printf '  %s\n' "${candidates[@]}" >&2
  fi
  echo "Re-run passing BASE_DTB explicitly:" >&2
  echo "  sudo $0 $OVERLAY_DTBO /boot/dtb/<your-base>.dtb" >&2
  return 1
}

if [[ -z "$BASE_DTB" ]]; then
  BASE_DTB=$(detect_base_dtb)
fi

if [[ ! -f "$BASE_DTB" ]]; then
  echo "Error: base DTB not found: $BASE_DTB" >&2
  exit 1
fi

if [[ "$BASE_DTB" == *"-imx708-dual.dtb" ]]; then
  echo "Error: refusing to merge overlay on top of an already-merged DTB ($BASE_DTB)." >&2
  echo "Pass the stock NVIDIA base DTB instead." >&2
  exit 1
fi

BASE_DIR=$(dirname "$BASE_DTB")
BASE_NAME=$(basename "$BASE_DTB" .dtb)
MERGED_DTB="${BASE_DIR}/${BASE_NAME}-imx708-dual.dtb"
TMP_DTB="${MERGED_DTB}.tmp"

echo "=== Applying IMX708 Dual-CSI Device Tree Overlay ==="
echo "  Base DTB    : $BASE_DTB"
echo "  Overlay     : $OVERLAY_DTBO"
echo "  Merged DTB  : $MERGED_DTB"

rm -f "$TMP_DTB"
fdtoverlay -i "$BASE_DTB" -o "$TMP_DTB" "$OVERLAY_DTBO"

# Verify both CAM0 and CAM1 IMX708 nodes exist in the merged DTB
MERGED_DTS=$(dtc -I dtb -O dts "$TMP_DTB" 2>/dev/null)
if ! grep -q "rpicam3_imx708_a@1a" <<< "$MERGED_DTS" || \
   ! grep -q "rpicam3_imx708_c@1a" <<< "$MERGED_DTS"; then
  rm -f "$TMP_DTB"
  echo "Error: merged DTB verification failed (missing CAM0 or CAM1 imx708 node)." >&2
  exit 1
fi

mv "$TMP_DTB" "$MERGED_DTB"
chmod 0644 "$MERGED_DTB"
echo "  Verified    : both CAM0 (rpicam3_imx708_a@1a) and CAM1 (rpicam3_imx708_c@1a) present"

if [[ ! -f "$EXTLINUX_BAK" ]]; then
  cp -a "$EXTLINUX" "$EXTLINUX_BAK"
  echo "  Backup      : saved original extlinux.conf to $EXTLINUX_BAK"
fi

python3 - "$EXTLINUX" "$LABEL_NAME" "$MERGED_DTB" <<'PY'
import os, re, sys

extlinux_path, label_name, merged_dtb = sys.argv[1], sys.argv[2], sys.argv[3]
lines = open(extlinux_path, "r", encoding="utf-8").read().splitlines()

# Parse header lines before first LABEL and split into label blocks
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

primary_block = None
for name, blines in blocks:
    if name == "primary":
        primary_block = blines
        break

if primary_block is None:
    if not blocks:
        sys.exit("Error: no LABEL entries found in extlinux.conf")
    primary_block = blocks[0][1]

# Build new label block cloned from primary, replacing FDT and stripping OVERLAYS
new_block = [
    f"LABEL {label_name}",
    "      MENU LABEL IMX708 Dual CSI (Pi Camera Module 3 Wide)",
]
has_fdt = False
for line in primary_block[1:]:
    stripped = line.strip()
    if re.match(r"^MENU\s+LABEL\b", stripped, re.IGNORECASE):
        continue
    if re.match(r"^OVERLAYS\b", stripped, re.IGNORECASE):
        continue
    if re.match(r"^FDT\b", stripped, re.IGNORECASE):
        new_block.append(f"      FDT {merged_dtb}")
        has_fdt = True
        continue
    new_block.append(line)

if not has_fdt:
    # Insert FDT right after INITRD or LINUX
    inserted = False
    rebuilt = []
    for line in new_block:
        rebuilt.append(line)
        if not inserted and re.match(r"^\s*INITRD\b", line, re.IGNORECASE):
            rebuilt.append(f"      FDT {merged_dtb}")
            inserted = True
    if not inserted:
        rebuilt.append(f"      FDT {merged_dtb}")
    new_block = rebuilt

# Remove any existing block with the same label_name
kept_blocks = [(n, b) for (n, b) in blocks if n != label_name]
kept_blocks.append((label_name, new_block))

# Set DEFAULT to label_name
new_header = []
saw_default = False
for line in header:
    if re.match(r"^\s*DEFAULT\s+", line, re.IGNORECASE):
        new_header.append(f"DEFAULT {label_name}")
        saw_default = True
    else:
        new_header.append(line)
if not saw_default:
    new_header.append(f"DEFAULT {label_name}")

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

echo "  Updated     : $EXTLINUX (DEFAULT ${LABEL_NAME}; LABEL primary preserved as fallback)"
echo ""
echo "Done. Power-cycle or reboot the Jetson ('sudo reboot'), then run:"
echo "  ./scripts/verify.sh"
