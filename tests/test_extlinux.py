#!/usr/bin/env python3
"""Test extlinux.conf update and rollback logic in scripts/apply_overlay.sh, uninstall.sh, and debian/prerm."""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SAMPLES = {
    "jp62_with_fdt": """TIMEOUT 30
DEFAULT primary

MENU TITLE L4T boot options

LABEL primary
      MENU LABEL primary kernel
      LINUX /boot/Image
      FDT /boot/dtb/kernel_tegra234-p3768-0000+p3767-0005-nv-super.dtb
      INITRD /boot/initrd
      APPEND ${cbootargs} root=PARTUUID=1234 rw rootwait
""",
    "jp60_without_fdt_with_overlays": """TIMEOUT 30
DEFAULT primary

LABEL primary
      MENU LABEL primary kernel
      LINUX /boot/Image
      INITRD /boot/initrd
      OVERLAYS /boot/old-cam.dtbo
      APPEND ${cbootargs} root=/dev/mmcblk0p1 rw rootwait
""",
}


def extract_py(path: Path, marker: str) -> str:
    text = path.read_text(encoding="utf-8")
    m = re.search(rf"{re.escape(marker)} <<'PY'\n(.*?)\nPY", text, re.S)
    assert m, f"Could not extract Python block from {path}"
    return m.group(1)


def main() -> int:
    apply_py = extract_py(
        ROOT / "scripts/apply_overlay.sh",
        'python3 - "$EXTLINUX" "$LABEL_NAME" "$MERGED_DTB"',
    )
    uninst_py = extract_py(
        ROOT / "scripts/uninstall.sh",
        'python3 - "$EXTLINUX" "$LABEL_NAME"',
    )
    prerm_py = extract_py(
        ROOT / "debian/prerm",
        'python3 - "${EXTLINUX}" "${LABEL_NAME}"',
    )

    merged = "/boot/dtb/kernel_tegra234-p3768-0000+p3767-0005-nv-super-imx708-dual.dtb"

    for rollback_name, rollback_py in [("uninstall.sh", uninst_py), ("debian/prerm", prerm_py)]:
        for name, content in SAMPLES.items():
            with tempfile.TemporaryDirectory() as td:
                conf = Path(td) / "extlinux.conf"
                conf.write_text(content, encoding="utf-8")

                # Apply twice to confirm idempotency
                for _ in range(2):
                    subprocess.run(
                        [sys.executable, "-c", apply_py, str(conf), "imx708-dual", merged],
                        check=True,
                    )
                out = conf.read_text(encoding="utf-8")
                assert re.search(r"(?m)^DEFAULT imx708-dual$", out)
                assert len(re.findall(r"(?m)^LABEL primary$", out)) == 1
                assert len(re.findall(r"(?m)^LABEL imx708-dual$", out)) == 1
                assert f"FDT {merged}" in out
                assert "OVERLAYS" not in out.split("LABEL imx708-dual", 1)[1]

                # Roll back cleanly
                subprocess.run(
                    [sys.executable, "-c", rollback_py, str(conf), "imx708-dual"],
                    check=True,
                )
                rev = conf.read_text(encoding="utf-8")
                assert re.search(r"(?m)^DEFAULT primary$", rev)
                assert len(re.findall(r"(?m)^LABEL primary$", rev)) == 1
                assert len(re.findall(r"(?m)^LABEL imx708-dual$", rev)) == 0
                print(f"  PASS: {name} + {rollback_name} (idempotent apply + clean rollback)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
