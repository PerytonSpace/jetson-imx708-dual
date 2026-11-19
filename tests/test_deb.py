#!/usr/bin/env python3
"""Validate generated Debian DKMS .deb archive against deb(5), md5sums, and dpkg-deb."""

from __future__ import annotations

import hashlib
import io
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    deb_path = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        ROOT / "dist" / f"jetson-imx708-dual-dkms_{version}_arm64.deb"
    )
    assert deb_path.is_file(), f"Package not found: {deb_path}"

    raw = deb_path.read_bytes()
    assert raw[:8] == b"!<arch>\n", "Invalid ar magic"

    pos = 8
    members = []
    while pos < len(raw):
        hdr = raw[pos : pos + 60]
        assert len(hdr) == 60 and hdr[58:60] == b"`\n"
        name = hdr[0:16].decode("ascii").strip()
        size = int(hdr[48:58].decode("ascii").strip())
        data = raw[pos + 60 : pos + 60 + size]
        assert len(data) == size
        members.append((name, data))
        pos += 60 + size + (size % 2)

    assert [m[0] for m in members] == ["debian-binary", "control.tar.gz", "data.tar.gz"]
    assert members[0][1] == b"2.0\n"

    ctrl_files = {}
    with tarfile.open(fileobj=io.BytesIO(members[1][1]), mode="r:gz") as tf:
        for ti in tf.getmembers():
            assert ti.uid == 0 and ti.gid == 0 and ti.uname == "root" and ti.gname == "root"
            assert ti.name == "." or ti.name.startswith("./")
            if ti.isfile():
                ctrl_files[ti.name] = (tf.extractfile(ti).read(), ti.mode)

    assert set(ctrl_files.keys()) == {
        "./control",
        "./conffiles",
        "./md5sums",
        "./postinst",
        "./prerm",
    }
    assert ctrl_files["./postinst"][1] == 0o755
    assert ctrl_files["./prerm"][1] == 0o755
    assert ctrl_files["./control"][1] == 0o644

    data_files = {}
    with tarfile.open(fileobj=io.BytesIO(members[2][1]), mode="r:gz") as tf:
        for ti in tf.getmembers():
            assert ti.uid == 0 and ti.gid == 0 and ti.uname == "root" and ti.gname == "root"
            assert ti.name == "." or ti.name.startswith("./")
            assert "/._" not in ti.name
            if ti.isfile():
                data_files[ti.name[2:]] = (tf.extractfile(ti).read(), ti.mode)

    md5_lines = [l for l in ctrl_files["./md5sums"][0].decode().splitlines() if l.strip()]
    assert len(md5_lines) >= 15
    for line in md5_lines:
        digest, rel = line.split("  ", 1)
        assert rel in data_files, f"md5sums entry missing: {rel}"
        assert hashlib.md5(data_files[rel][0]).hexdigest() == digest

    for cf in ctrl_files["./conffiles"][0].decode().splitlines():
        cf = cf.strip().lstrip("/")
        assert cf in data_files, f"conffile missing: {cf}"

    assert data_files["usr/sbin/imx708-dual-apply"][1] == 0o755
    assert data_files["usr/bin/imx708-dual-verify"][1] == 0o755

    if shutil.which("dpkg-deb"):
        subprocess.run(["dpkg-deb", "--info", str(deb_path)], check=True, capture_output=True)
        subprocess.run(["dpkg-deb", "--contents", str(deb_path)], check=True, capture_output=True)
        with tempfile.TemporaryDirectory() as td:
            subprocess.run(["dpkg-deb", "--extract", str(deb_path), td], check=True)
            assert (Path(td) / "usr/sbin/imx708-dual-apply").is_file()
            assert (Path(td) / "usr/bin/imx708-dual-verify").is_file()
            assert (Path(td) / f"usr/src/jetson-imx708-dual-{version}/dkms.conf").is_file()
        print(f"  PASS: {deb_path.name} (deb(5) + md5sums + dpkg-deb --info/--contents/--extract verified)")
    else:
        print(f"  PASS: {deb_path.name} (deb(5) + md5sums verified; dpkg-deb not on host)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
