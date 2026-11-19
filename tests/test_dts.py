#!/usr/bin/env python3
"""Validate both R36+ and R35 dual-CSI Device Tree overlays, plus real dtc & fdtoverlay merges."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

MOCK_BASE_R36 = """
/dts-v1/;
/ {
    compatible = "nvidia,p3768-0000+p3767-0005-super", "nvidia,tegra234";
    tegra-capture-vi { status = "okay"; };
    tegra-camera-platform { compatible = "nvidia, tegra-camera-platform"; };
    bus@0 {
        #address-cells = <1>;
        #size-cells = <1>;
        gpio: gpio@2200000 { #gpio-cells = <2>; gpio-controller; };
        gpio_aon: gpio@c2f0000 { #gpio-cells = <2>; gpio-controller; };
        cam_i2c: i2c@3180000 { #address-cells = <1>; #size-cells = <0>; status = "okay"; };
        host1x@13e00000 {
            nvcsi@15a00000 { status = "okay"; };
        };
        cam_i2cmux {
            i2c@0 { rbpcv2_imx219_a@10 { status = "okay"; }; };
            i2c@1 { rbpcv2_imx219_c@10 { status = "okay"; }; };
        };
    };
};
"""

MOCK_BASE_R35 = """
/dts-v1/;
/ {
    compatible = "nvidia,p3768-0000+p3767-0005", "nvidia,tegra234";
    tegra-capture-vi { status = "okay"; };
    tegra-camera-platform { compatible = "nvidia, tegra-camera-platform"; };
    tegra_main_gpio: gpio@2200000 { #gpio-cells = <2>; gpio-controller; };
    tegra_aon_gpio: gpio@c2f0000 { #gpio-cells = <2>; gpio-controller; };
    cam_i2c: i2c@3180000 { #address-cells = <1>; #size-cells = <0>; status = "okay"; };
    host1x@13e00000 {
        nvcsi@15a00000 { status = "okay"; };
    };
    cam_i2cmux {
        i2c@0 { rbpcv2_imx219_a@10 { status = "okay"; }; };
        i2c@1 { rbpcv2_imx219_c@10 { status = "okay"; }; };
    };
};
"""


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def validate_dts(path: Path, variant: str) -> None:
    clean = strip_comments(path.read_text(encoding="utf-8"))
    assert "/dts-v1/;" in clean
    assert "/plugin/;" in clean
    assert clean.count("{") == clean.count("}")

    labels = set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*:", clean))
    refs = set(re.findall(r"&([A-Za-z_][A-Za-z0-9_]*)", clean))
    expected_ext = (
        {"gpio", "gpio_aon", "cam_i2c"}
        if variant == "r36"
        else {"tegra_main_gpio", "tegra_aon_gpio", "cam_i2c"}
    )
    assert (refs - labels) == expected_ext, f"{path}: unexpected external refs {refs - labels}"

    ep_pairs = [
        ("rpicam3_imx708_vi_in0", "rpicam3_imx708_csi_out0"),
        ("rpicam3_imx708_csi_in0", "rpicam3_imx708_out0"),
        ("rpicam3_imx708_vi_in1", "rpicam3_imx708_csi_out1"),
        ("rpicam3_imx708_csi_in1", "rpicam3_imx708_out1"),
    ]
    for a, b in ep_pairs:
        ma = re.search(rf"\b{a}\s*:[^{{]*\{{([^}}]*)\}}", clean)
        mb = re.search(rf"\b{b}\s*:[^{{]*\{{([^}}]*)\}}", clean)
        assert ma and mb, f"{path}: missing endpoint {a} or {b}"
        assert f"remote-endpoint = <&{b}>;" in ma.group(1)
        assert f"remote-endpoint = <&{a}>;" in mb.group(1)

    assert 'tegra_sinterface = "serial_b";' in clean
    assert 'tegra_sinterface = "serial_c";' in clean
    assert 'lane_polarity = "6";' in clean
    assert 'lane_polarity = "0";' in clean
    assert 'discontinuous_clk = "yes";' not in clean
    assert clean.count('discontinuous_clk = "no";') == 6

    hw = {
        "mode0": (4608, 2592, 595200000 / 15648),
        "mode1": (2304, 1296, 585600000 / 7824),
        "mode2": (1536, 864, 566400000 / 5216),
    }
    for mname, (ew, eh, hw_line_rate) in hw.items():
        blocks = re.findall(rf"\b{mname}\s*\{{([^}}]*)\}}", clean)
        assert len(blocks) == 2
        for blk in blocks:
            w = int(re.search(r'active_w\s*=\s*"(\d+)"', blk).group(1))
            h = int(re.search(r'active_h\s*=\s*"(\d+)"', blk).group(1))
            ll = int(re.search(r'line_length\s*=\s*"(\d+)"', blk).group(1))
            pclk = int(re.search(r'pix_clk_hz\s*=\s*"(\d+)"', blk).group(1))
            lanes = int(re.search(r'num_lanes\s*=\s*"(\d+)"', blk).group(1))
            bpp = int(re.search(r'csi_pixel_bit_depth\s*=\s*"(\d+)"', blk).group(1))
            assert (w, h) == (ew, eh)
            assert ll >= w
            assert ((pclk * bpp) // lanes) // 2 == 450_000_000
            assert abs((pclk / ll) - hw_line_rate) / hw_line_rate < 0.0005

    if variant == "r36":
        assert "bus@0 {" in clean
        assert 'sysfs-device-tree = "/sys/firmware/devicetree/base/bus@0/cam_i2cmux/i2c@0/rpicam3_imx708_a@1a";' in clean
        assert 'sysfs-device-tree = "/sys/firmware/devicetree/base/bus@0/cam_i2cmux/i2c@1/rpicam3_imx708_c@1a";' in clean
    else:
        assert "bus@0 {" not in clean
        assert 'proc-device-tree = "/proc/device-tree/cam_i2cmux/i2c@0/rpicam3_imx708_a@1a";' in clean
        assert 'proc-device-tree = "/proc/device-tree/cam_i2cmux/i2c@1/rpicam3_imx708_c@1a";' in clean

    # If dtc and fdtoverlay are available (e.g., on Linux/CI), compile and test actual overlay merge
    if shutil.which("dtc") and shutil.which("fdtoverlay"):
        with tempfile.TemporaryDirectory() as td:
            tdp = Path(td)
            base_dts = tdp / f"base-{variant}.dts"
            base_dtb = tdp / f"base-{variant}.dtb"
            dtbo = tdp / f"imx708-{variant}.dtbo"
            merged = tdp / f"merged-{variant}.dtb"
            base_dts.write_text(MOCK_BASE_R36 if variant == "r36" else MOCK_BASE_R35, encoding="utf-8")

            subprocess.run(
                ["dtc", "-@", "-I", "dts", "-O", "dtb", "-Wno-unit_address_vs_reg", "-o", str(base_dtb), str(base_dts)],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                ["dtc", "-@", "-I", "dts", "-O", "dtb", "-Wno-unit_address_vs_reg", "-o", str(dtbo), str(path)],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                ["fdtoverlay", "-i", str(base_dtb), "-o", str(merged), str(dtbo)],
                check=True,
                capture_output=True,
            )
            decompiled = subprocess.run(
                ["dtc", "-I", "dtb", "-O", "dts", str(merged)],
                check=True,
                capture_output=True,
                text=True,
            ).stdout
            assert "rpicam3_imx708_a@1a" in decompiled
            assert "rpicam3_imx708_c@1a" in decompiled
            assert 'status = "disabled"' in decompiled
            print(f"  PASS: {path.name} (structural + math + real dtc + fdtoverlay merge verified)")
    else:
        print(f"  PASS: {path.name} (structural + math verified; dtc/fdtoverlay not on host)")


def main() -> int:
    validate_dts(ROOT / "dts/tegra234-p3767-camera-p3768-imx708-dual-r36.dts", "r36")
    validate_dts(ROOT / "dts/tegra234-p3767-camera-p3768-imx708-dual-r35.dts", "r35")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
