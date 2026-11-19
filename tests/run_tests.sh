#!/usr/bin/env bash
# Run the complete host test suite for jetson-imx708-dual.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

echo "=== 1. Shell script syntax checks ==="
for s in scripts/*.sh debian/postinst debian/prerm packaging/*.sh tests/*.sh; do
  bash -n "$s"
  echo "  PASS: bash -n $s"
done

echo "=== 2. C driver ABI compile checks (Linux 5.10, 5.15, 6.8) ==="
COMPILERS=()
command -v gcc   >/dev/null 2>&1 && COMPILERS+=(gcc)
command -v clang >/dev/null 2>&1 && COMPILERS+=(clang)
if [[ ${#COMPILERS[@]} -eq 0 ]]; then
  echo "Error: neither gcc nor clang found." >&2
  exit 1
fi

for cc in "${COMPILERS[@]}"; do
  for ver in "((5<<16)+(10<<8)+192)" "((5<<16)+(15<<8)+148)" "((6<<16)+(8<<8)+12)"; do
    "$cc" -fsyntax-only -Wall -Wextra -Wno-unused-parameter -Werror \
      -DLINUX_VERSION_CODE="$ver" \
      -Itests/stubs -Iinclude -Isrc \
      src/nv_imx708.c
    echo "  PASS: $cc -Werror LINUX_VERSION_CODE=$ver"
  done
done

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
  aarch64-linux-gnu-gcc -fsyntax-only -Wall -Wextra -Wno-unused-parameter -Werror \
    -DLINUX_VERSION_CODE="((5<<16)+(15<<8)+148)" \
    -Itests/stubs -Iinclude -Isrc \
    src/nv_imx708.c
  echo "  PASS: aarch64-linux-gnu-gcc cross syntax check"
fi

echo "=== 3. C driver functional unit tests ==="
TMP_BIN=$(mktemp)
trap 'rm -f "$TMP_BIN"' EXIT
"${COMPILERS[0]}" -Wall -Wextra -Wno-unused-parameter -Werror \
  -DLINUX_VERSION_CODE="((5<<16)+(15<<8)+148)" \
  -Itests/stubs -Iinclude -Isrc \
  tests/test_nv_imx708.c -o "$TMP_BIN"
"$TMP_BIN"

echo "=== 4. Device Tree overlay structural, timing, and dtc/fdtoverlay tests ==="
python3 tests/test_dts.py

echo "=== 5. extlinux.conf apply/rollback tests ==="
python3 tests/test_extlinux.py

echo "=== 6. Packaging build & .deb integrity verification ==="
make package
python3 tests/test_deb.py

echo ""
echo "All tests passed."
