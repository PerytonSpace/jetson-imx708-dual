#!/usr/bin/env bash
# Post-reboot verification for the dual-CSI IMX708 driver on NVIDIA Jetson Orin Nano.
#
# Usage:
#   ./scripts/verify.sh
#   ./scripts/verify.sh --capture [WIDTH] [HEIGHT]
#     Example: ./scripts/verify.sh --capture 2304 1296

set -euo pipefail

DO_CAPTURE=0
CAP_W=2304
CAP_H=1296
CAP_FRAMES=3

while [[ $# -gt 0 ]]; do
  case "$1" in
    --capture)
      DO_CAPTURE=1
      shift
      if [[ $# -ge 2 && "$1" =~ ^[0-9]+$ && "$2" =~ ^[0-9]+$ ]]; then
        CAP_W="$1"
        CAP_H="$2"
        shift 2
      fi
      ;;
    -h|--help)
      echo "Usage: $0 [--capture [WIDTH HEIGHT]]"
      echo "  Default capture test resolution: 2304 1296 (2x2 binned full wide FOV)"
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

PASS=0
WARN=0
FAIL=0

ok()   { echo "  [OK]   $*"; PASS=$((PASS + 1)); }
warn() { echo "  [WARN] $*"; WARN=$((WARN + 1)); }
fail() { echo "  [FAIL] $*"; FAIL=$((FAIL + 1)); }

echo "=== 1. Platform ==="
if [[ -r /proc/device-tree/model ]]; then
  echo "  Model  : $(tr -d '\0' < /proc/device-tree/model)"
fi
if [[ -r /etc/nv_tegra_release ]]; then
  echo "  L4T    : $(head -n 1 /etc/nv_tegra_release)"
fi
echo "  Kernel : $(uname -r)"

echo ""
echo "=== 2. Device Tree Nodes (CAM0 & CAM1) ==="
DT_ROOT="/proc/device-tree/bus@0/cam_i2cmux"
if [[ ! -d "$DT_ROOT" && -d "/proc/device-tree/cam_i2cmux" ]]; then
  DT_ROOT="/proc/device-tree/cam_i2cmux"
fi

for pair in "CAM0:i2c@0/rpicam3_imx708_a@1a" "CAM1:i2c@1/rpicam3_imx708_c@1a"; do
  label="${pair%%:*}"
  rel="${pair#*:}"
  node="${DT_ROOT}/${rel}"
  if [[ -d "$node" ]]; then
    compat=$(tr -d '\0' < "${node}/compatible" 2>/dev/null || echo "")
    status=$(tr -d '\0' < "${node}/status" 2>/dev/null || echo "okay")
    ok "${label} DT node present (${node}, compatible=${compat}, status=${status})"
  else
    fail "${label} DT node missing (${node}) - did you reboot into LABEL imx708-dual?"
  fi
done

echo ""
echo "=== 3. Kernel Module ==="
if lsmod | grep -q '^nv_imx708\b'; then
  ok "nv_imx708 module loaded"
else
  fail "nv_imx708 module not loaded (try 'sudo modprobe nv_imx708' and check dmesg)"
fi

if modinfo nv_imx708 >/dev/null 2>&1; then
  ver=$(modinfo -F version nv_imx708 2>/dev/null || true)
  fn=$(modinfo -F filename nv_imx708 2>/dev/null || true)
  ok "modinfo nv_imx708: version=${ver:-n/a} (${fn})"
else
  fail "modinfo nv_imx708 failed - module may not be installed for $(uname -r)"
fi

echo ""
echo "=== 4. I2C Mux Buses (0x1a = IMX708, 0x0c = DW9807 VCM) ==="
if command -v i2cdetect >/dev/null 2>&1; then
  mapfile -t MUX_BUSES < <(i2cdetect -l 2>/dev/null | awk '/mux/ {sub(/^i2c-/,"",$1); print $1}')
  if [[ ${#MUX_BUSES[@]} -eq 0 ]]; then
    warn "No cam_i2cmux I2C buses found via 'i2cdetect -l'"
  else
    for bus in "${MUX_BUSES[@]}"; do
      if [[ $(id -u) -eq 0 ]]; then
        scan=$(i2cdetect -y -r "$bus" 2>/dev/null || true)
        has_sensor=0
        has_vcm=0
        if grep '^10:' <<< "$scan" | grep -Eq '(^| )(1a|UU)( |$)'; then
          has_sensor=1
        fi
        if grep '^00:' <<< "$scan" | grep -Eq '(^| )(0c|UU)( |$)'; then
          has_vcm=1
        fi
        if [[ $has_sensor -eq 1 ]]; then
          ok "i2c-${bus}: IMX708 present at 0x1a (VCM 0x0c detected=${has_vcm})"
        else
          warn "i2c-${bus}: no device at 0x1a (normal if camera is idle/powered off between streams, or port empty)"
        fi
      else
        echo "  [INFO] Found mux bus i2c-${bus} (run with sudo to scan I2C addresses)"
      fi
    done
  fi
else
  warn "i2cdetect not installed (sudo apt install i2c-tools)"
fi

echo ""
echo "=== 5. Kernel Probe Log (dmesg) ==="
DMESG_OUT=$(dmesg 2>/dev/null | grep -iE 'imx708|tegra-camrtc-capture-vi' | tail -n 20 || true)
if [[ -n "$DMESG_OUT" ]]; then
  while IFS= read -r line; do
    echo "  $line"
  done <<< "$DMESG_OUT"
else
  warn "Unable to read dmesg or no imx708 entries found (try running with sudo)"
fi

echo ""
echo "=== 6. V4L2 Video Devices ==="
IMX708_DEVS=()
if command -v v4l2-ctl >/dev/null 2>&1; then
  for dev in /dev/video*; do
    [[ -e "$dev" ]] || continue
    info=$(v4l2-ctl -d "$dev" --info 2>/dev/null || true)
    if grep -qi "imx708" <<< "$info"; then
      card=$(grep -m1 "Card type" <<< "$info" | sed 's/.*: *//')
      ok "${dev}: ${card}"
      IMX708_DEVS+=("$dev")
      v4l2-ctl -d "$dev" --list-formats-ext 2>/dev/null | sed 's/^/    /'
    fi
  done
  if [[ ${#IMX708_DEVS[@]} -eq 2 ]]; then
    ok "Both CSI ports registered (2 IMX708 V4L2 devices)"
  elif [[ ${#IMX708_DEVS[@]} -eq 1 ]]; then
    warn "Only 1 IMX708 V4L2 device detected (${IMX708_DEVS[0]}) - check second ribbon cable if dual cameras are attached"
  else
    fail "No IMX708 /dev/video* devices detected"
  fi
else
  fail "v4l2-ctl not installed (sudo apt install v4l-utils)"
fi

if [[ $DO_CAPTURE -eq 1 && ${#IMX708_DEVS[@]} -gt 0 ]]; then
  echo ""
  echo "=== 7. Simultaneous Raw Capture Test (${CAP_W}x${CAP_H} RG10 x ${CAP_FRAMES} frames) ==="
  EXPECTED_BYTES=$((CAP_W * CAP_H * 2 * CAP_FRAMES))
  PIDS=()
  OUT_FILES=()
  for idx in "${!IMX708_DEVS[@]}"; do
    dev="${IMX708_DEVS[$idx]}"
    out="/tmp/imx708_verify_cam${idx}.raw"
    rm -f "$out"
    OUT_FILES+=("$out")
    timeout 15 v4l2-ctl -d "$dev" \
      --set-fmt-video="width=${CAP_W},height=${CAP_H},pixelformat=RG10" \
      --set-ctrl bypass_mode=0 \
      --stream-mmap \
      --stream-count="$CAP_FRAMES" \
      --stream-to="$out" >/dev/null 2>&1 &
    PIDS+=($!)
  done

  for idx in "${!PIDS[@]}"; do
    dev="${IMX708_DEVS[$idx]}"
    out="${OUT_FILES[$idx]}"
    if wait "${PIDS[$idx]}"; then
      actual=$(stat -c%s "$out" 2>/dev/null || echo 0)
      if [[ "$actual" -eq "$EXPECTED_BYTES" ]]; then
        ok "${dev}: captured ${CAP_FRAMES} frames (${actual} bytes -> ${out})"
      else
        fail "${dev}: unexpected capture size ${actual} bytes (expected ${EXPECTED_BYTES})"
      fi
    else
      fail "${dev}: capture timed out or failed (check dmesg for NVCSI/VI errors)"
    fi
  done
fi

echo ""
echo "=== Summary: ${PASS} passed, ${WARN} warning(s), ${FAIL} failed ==="
[[ $FAIL -eq 0 ]]
