#!/usr/bin/env python3
"""
Build a standards-compliant Debian DKMS binary package (.deb) for
jetson-imx708-dual-dkms on either Linux or macOS using only the Python stdlib.

Produces:
  dist/jetson-imx708-dual-dkms_<VERSION>_arm64.deb

The generated archive follows deb(5):
  !<arch>\n
    debian-binary   ("2.0\n")
    control.tar.gz  (./control, ./conffiles, ./md5sums, ./postinst, ./prerm)
    data.tar.gz     (DKMS source tree under /usr/src/, CLI tools, autoload conf, docs)
"""

from __future__ import annotations

import gzip
import hashlib
import io
import os
import re
import sys
import tarfile
import time
from pathlib import Path
from typing import Dict, List, Tuple


PACKAGE_NAME = "jetson-imx708-dual-dkms"
DKMS_NAME = "jetson-imx708-dual"
ARCH = "arm64"


def check_version_consistency(root: Path, version: str) -> None:
    checks = [
        (root / "dkms.conf", rf'PACKAGE_VERSION="{re.escape(version)}"'),
        (root / "debian/postinst", rf'PKG_VER="{re.escape(version)}"'),
        (root / "debian/prerm", rf'PKG_VER="{re.escape(version)}"'),
        (root / "debian/changelog", rf"^{re.escape(DKMS_NAME)}\s+\({re.escape(version)}-"),
        (root / "src/nv_imx708.c", rf'MODULE_VERSION\("{re.escape(version)}"\);'),
    ]
    for path, pattern in checks:
        text = path.read_text(encoding="utf-8")
        if not re.search(pattern, text, re.MULTILINE):
            raise SystemExit(
                f"Version mismatch in {path}: expected {version!r} matching {pattern}"
            )


def build_tar_gz(
    entries: List[Tuple[str, bytes | None, int]],
    mtime: int,
) -> bytes:
    """Create a deterministic GNU tar.gz with root:root ownership and ./ prefixes."""
    raw_tar = io.BytesIO()
    seen_dirs = set()

    with tarfile.open(fileobj=raw_tar, mode="w", format=tarfile.GNU_FORMAT) as tf:
        def ensure_dir(rel_dir: str) -> None:
            parts = [p for p in rel_dir.strip("/").split("/") if p]
            cur = "."
            if cur not in seen_dirs:
                ti = tarfile.TarInfo(name="./")
                ti.type = tarfile.DIRTYPE
                ti.mode = 0o755
                ti.uid = 0
                ti.gid = 0
                ti.uname = "root"
                ti.gname = "root"
                ti.mtime = mtime
                tf.addfile(ti)
                seen_dirs.add(cur)
            for part in parts:
                cur = f"{cur}/{part}"
                if cur not in seen_dirs:
                    ti = tarfile.TarInfo(name=cur + "/")
                    ti.type = tarfile.DIRTYPE
                    ti.mode = 0o755
                    ti.uid = 0
                    ti.gid = 0
                    ti.uname = "root"
                    ti.gname = "root"
                    ti.mtime = mtime
                    tf.addfile(ti)
                    seen_dirs.add(cur)

        ensure_dir("")
        for rel_path, content, mode in sorted(entries, key=lambda x: x[0]):
            rel_clean = rel_path.lstrip("/")
            if content is None:
                ensure_dir(rel_clean)
                continue
            parent = os.path.dirname(rel_clean)
            if parent:
                ensure_dir(parent)
            ti = tarfile.TarInfo(name=f"./{rel_clean}")
            ti.type = tarfile.REGTYPE
            ti.size = len(content)
            ti.mode = mode
            ti.uid = 0
            ti.gid = 0
            ti.uname = "root"
            ti.gname = "root"
            ti.mtime = mtime
            tf.addfile(ti, io.BytesIO(content))

    gz_buf = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=gz_buf, mtime=mtime) as gz:
        gz.write(raw_tar.getvalue())
    return gz_buf.getvalue()


def ar_member(name: str, data: bytes, mtime: int) -> bytes:
    """Format one GNU/Debian ar member according to deb(5)."""
    name_bytes = name.encode("ascii")
    if len(name_bytes) > 16:
        raise ValueError(f"ar member name too long: {name}")
    header = (
        f"{name:<16}"
        f"{mtime:<12d}"
        f"{0:<6d}"
        f"{0:<6d}"
        f"{'100644':<8}"
        f"{len(data):<10d}"
        "`\n"
    ).encode("ascii")
    assert len(header) == 60
    padding = b"\n" if (len(data) % 2) == 1 else b""
    return header + data + padding


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    check_version_consistency(root, version)

    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else (root / "dist")
    out_dir.mkdir(parents=True, exist_ok=True)
    deb_path = out_dir / f"{PACKAGE_NAME}_{version}_{ARCH}.deb"

    mtime = int(os.environ.get("SOURCE_DATE_EPOCH", str(int(time.time()))))
    src_base = f"usr/src/{DKMS_NAME}-{version}"

    # Collect payload files for data.tar.gz: (dest_rel_path, bytes, mode)
    file_map: Dict[str, Tuple[Path, int]] = {
        f"{src_base}/VERSION": (root / "VERSION", 0o644),
        f"{src_base}/COPYING": (root / "COPYING", 0o644),
        f"{src_base}/Makefile": (root / "Makefile", 0o644),
        f"{src_base}/dkms.conf": (root / "dkms.conf", 0o644),
        f"{src_base}/include/imx708.h": (root / "include/imx708.h", 0o644),
        f"{src_base}/src/Makefile": (root / "src/Makefile", 0o644),
        f"{src_base}/src/imx708_mode_tbls.h": (root / "src/imx708_mode_tbls.h", 0o644),
        f"{src_base}/src/nv_imx708.c": (root / "src/nv_imx708.c", 0o644),
        f"{src_base}/dts/tegra234-p3767-camera-p3768-imx708-dual-r36.dts": (
            root / "dts/tegra234-p3767-camera-p3768-imx708-dual-r36.dts",
            0o644,
        ),
        f"{src_base}/dts/tegra234-p3767-camera-p3768-imx708-dual-r35.dts": (
            root / "dts/tegra234-p3767-camera-p3768-imx708-dual-r35.dts",
            0o644,
        ),
        "usr/sbin/imx708-dual-apply": (root / "scripts/apply_overlay.sh", 0o755),
        "usr/bin/imx708-dual-verify": (root / "scripts/verify.sh", 0o755),
        f"usr/share/doc/{PACKAGE_NAME}/README.md": (root / "README.md", 0o644),
        f"usr/share/doc/{PACKAGE_NAME}/copyright": (root / "debian/copyright", 0o644),
    }

    data_entries: List[Tuple[str, bytes | None, int]] = []
    md5_lines: List[str] = []
    total_bytes = 0

    for dest_rel, (src_path, mode) in sorted(file_map.items()):
        blob = src_path.read_bytes()
        data_entries.append((dest_rel, blob, mode))
        md5_lines.append(f"{hashlib.md5(blob).hexdigest()}  {dest_rel}")
        total_bytes += len(blob)

    # Generated conffile (/etc/modules-load.d/nv_imx708.conf)
    autoload_rel = "etc/modules-load.d/nv_imx708.conf"
    autoload_bytes = b"nv_imx708\n"
    data_entries.append((autoload_rel, autoload_bytes, 0o644))
    total_bytes += len(autoload_bytes)

    # Compressed changelog.Debian.gz per Debian Policy
    changelog_gz = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=changelog_gz, mtime=mtime) as gz:
        gz.write((root / "debian/changelog").read_bytes())
    changelog_rel = f"usr/share/doc/{PACKAGE_NAME}/changelog.Debian.gz"
    changelog_bytes = changelog_gz.getvalue()
    data_entries.append((changelog_rel, changelog_bytes, 0o644))
    md5_lines.append(f"{hashlib.md5(changelog_bytes).hexdigest()}  {changelog_rel}")
    total_bytes += len(changelog_bytes)

    installed_kb = max(1, (total_bytes + 1023) // 1024)

    control_text = (
        f"Package: {PACKAGE_NAME}\n"
        f"Version: {version}-1\n"
        f"Architecture: {ARCH}\n"
        f"Maintainer: PerytonSpace <perception@perytonspace.invalid>\n"
        f"Homepage: https://github.com/PerytonSpace/jetson-imx708-dual\n"
        f"Installed-Size: {installed_kb}\n"
        f"Section: kernel\n"
        f"Priority: optional\n"
        f"Depends: dkms, build-essential, device-tree-compiler, v4l-utils, i2c-tools, python3\n"
        f"Recommends: nvidia-l4t-kernel-headers, nvidia-l4t-kernel-oot-headers\n"
        f"Description: Dual-CSI Sony IMX708 (Pi Camera Module 3 Wide) DKMS driver for Jetson Orin Nano\n"
        f" Out-of-tree Linux kernel V4L2/Tegracam driver (nv_imx708) and dual-CSI Device\n"
        f" Tree overlays for running two Raspberry Pi Camera Module 3 Wide (Sony IMX708)\n"
        f" sensors on the NVIDIA Jetson Orin Nano Developer Kit (p3768-0000).\n"
        f" .\n"
        f" Supports 4608x2592@14fps, 2304x1296@56fps (2x2 binned full wide FOV), and\n"
        f" 1536x864@120fps across JetPack 6.x (L4T R36.x), JetPack 7.x (L4T R38.x), and\n"
        f" JetPack 5.1.x (L4T R35.x). Rebuilds automatically via DKMS on kernel upgrades.\n"
    ).encode("utf-8")

    conffiles_bytes = f"/{autoload_rel}\n".encode("utf-8")
    md5sums_bytes = ("\n".join(sorted(md5_lines)) + "\n").encode("utf-8")
    postinst_bytes = (root / "debian/postinst").read_bytes()
    prerm_bytes = (root / "debian/prerm").read_bytes()

    control_entries: List[Tuple[str, bytes | None, int]] = [
        ("control", control_text, 0o644),
        ("conffiles", conffiles_bytes, 0o644),
        ("md5sums", md5sums_bytes, 0o644),
        ("postinst", postinst_bytes, 0o755),
        ("prerm", prerm_bytes, 0o755),
    ]

    debian_binary = b"2.0\n"
    control_tar_gz = build_tar_gz(control_entries, mtime=mtime)
    data_tar_gz = build_tar_gz(data_entries, mtime=mtime)

    deb_bytes = (
        b"!<arch>\n"
        + ar_member("debian-binary", debian_binary, mtime)
        + ar_member("control.tar.gz", control_tar_gz, mtime)
        + ar_member("data.tar.gz", data_tar_gz, mtime)
    )

    tmp_deb = deb_path.with_suffix(".deb.tmp")
    tmp_deb.write_bytes(deb_bytes)
    os.replace(tmp_deb, deb_path)

    print(f"Built Debian DKMS package: {deb_path} ({len(deb_bytes)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
