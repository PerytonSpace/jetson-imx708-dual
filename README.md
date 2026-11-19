# jetson-imx708-dual

[![CI](https://github.com/PerytonSpace/jetson-imx708-dual/actions/workflows/ci.yml/badge.svg)](https://github.com/PerytonSpace/jetson-imx708-dual/actions/workflows/ci.yml)
[![Release](https://github.com/PerytonSpace/jetson-imx708-dual/actions/workflows/release.yml/badge.svg)](https://github.com/PerytonSpace/jetson-imx708-dual/actions/workflows/release.yml)
[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](LICENSE)

Out-of-tree Linux kernel V4L2/Tegracam DKMS driver (`nv_imx708`) and dual-CSI Device Tree overlays for running two **Raspberry Pi Camera Module 3 Wide** (Sony IMX708) cameras simultaneously on the official **NVIDIA Jetson Orin Nano Developer Kit** (`p3768-0000` carrier board with `p3767-000x` modules, including Orin Nano 4GB, 8GB, and Super).

Auto-detects **JetPack 6.x (L4T R36.x)**, **JetPack 7.x (L4T R38.x)**, and **JetPack 5.1.x (L4T R35.x)** during installation.

## Dual-CSI Hardware Mapping (`p3768-0000` DevKit)

| Port | Connector | CSI Interface | `port-index` | `lane_polarity` | I2C Mux Channel | Sensor Addr | Reset GPIO | Video Node |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **CAM0** | J20 | CSI-B (`serial_b`, 2 lanes) | `1` | `6` | `cam_i2cmux/i2c@0` | `0x1a` | `CAM0_PWDN` (`PH.06`, 62) | `/dev/video0` |
| **CAM1** | J21 | CSI-C (`serial_c`, 2 lanes) | `2` | `0` | `cam_i2cmux/i2c@1` | `0x1a` | `CAM1_PWDN` (`PAC.00`, 160) | `/dev/video1` |

- **FPC cables**: Pi Camera Module 3 Wide has a 15-pin 1.0 mm connector; the Jetson Orin Nano DevKit has 22-pin 0.5 mm connectors. Use **15-pin to 22-pin CSI FPC cables**.
- **Power off the Jetson before plugging or unplugging CSI cables.** Insert the 22-pin end with metal contacts facing the carrier board / heatsink (blue stiffener facing outward).
- Both modules share `cam_i2cmux` (GPIO `PCC.03` = 19 on `gpio_aon`) so both sensors stay at I2C address `0x1a` without address collisions.
- If only one camera is connected, the empty port fails its IMX708 chip-ID probe (`0x0708` at `0x0016/0x0017`) cleanly while the connected port still registers normally.

## Supported Sensor Modes (10-bit RAW RGGB, 2-Lane 450 MHz D-PHY)

| Mode | Resolution | Framerate Range | FOV / Binning | Notes |
| --- | --- | --- | --- | --- |
| `mode0` | `4608 x 2592` | 2 – 14 fps | Full 120° Wide FOV (Quad Bayer re-mosaic) | Highest stills resolution; QBC broken-line correction (`qbc_adjust=2`) |
| `mode1` | `2304 x 1296` | 2 – 56 fps | Full 120° Wide FOV (2x2 binned) | **Recommended** for dual-camera 30 fps live video |
| `mode2` | `1536 x 864` | 2 – 120 fps | Center cropped (2x2 binned) | High-framerate mode |

## Quick Install

### Option A: Install from GitHub Releases (Recommended — DKMS `.deb`)

On your Jetson Orin Nano, download the latest `.deb` from [GitHub Releases](https://github.com/PerytonSpace/jetson-imx708-dual/releases):

```bash
VERSION=1.0.0
curl -LO "https://github.com/PerytonSpace/jetson-imx708-dual/releases/download/v${VERSION}/jetson-imx708-dual-dkms_${VERSION}_arm64.deb"
sudo apt update
sudo apt install -y "./jetson-imx708-dual-dkms_${VERSION}_arm64.deb"
sudo reboot
```

After reboot, verify both CSI ports and run a simultaneous capture test:

```bash
sudo imx708-dual-verify --capture 2304 1296
```

Uninstall cleanly at any time (removes DKMS registration, restores `DEFAULT primary` in `extlinux.conf`, and deletes the merged DTB and `.dtbo`):

```bash
sudo apt remove -y jetson-imx708-dual-dkms && sudo reboot
```

### Option B: Build from Source

```bash
git clone https://github.com/PerytonSpace/jetson-imx708-dual.git
cd jetson-imx708-dual
sudo ./scripts/install.sh
sudo reboot
```

Or build the `.deb` and `.tar.gz` packages locally on macOS or Linux:

```bash
make package
```

What both installation paths do:
1. Auto-detect the Jetson's L4T release (`/etc/nv_tegra_release`) and select the matching kernel headers and overlay (`r36` or `r35`).
2. Compile and install `nv_imx708.ko` (via DKMS in Option A so JetPack kernel upgrades auto-rebuild the module) and enable boot autoload in `/etc/modules-load.d/nv_imx708.conf`.
3. Pre-merge the dual-CSI `.dtbo` onto a **copy** of your stock base DTB via `fdtoverlay` (avoiding JetPack 6.x UEFI ignoring `OVERLAYS` in `extlinux.conf`) and add a new `LABEL imx708-dual` default boot entry while leaving `LABEL primary` and your stock DTB completely untouched.

If automatic base DTB detection cannot uniquely pick a stock DTB, specify it explicitly:

```bash
sudo imx708-dual-apply /boot/tegra234-p3767-camera-p3768-imx708-dual.dtbo /boot/dtb/<your-base>.dtb
```

## Capturing Frames & Controls

### Raw 10-bit Bayer Capture (`v4l2-ctl`)

```bash
# CAM0 (2304x1296 full wide FOV)
v4l2-ctl -d /dev/video0 \
  --set-fmt-video=width=2304,height=1296,pixelformat=RG10 \
  --set-ctrl bypass_mode=0 \
  --stream-mmap --stream-count=5 --stream-to=/tmp/cam0.raw

# CAM1 (2304x1296 full wide FOV)
v4l2-ctl -d /dev/video1 \
  --set-fmt-video=width=2304,height=1296,pixelformat=RG10 \
  --set-ctrl bypass_mode=0 \
  --stream-mmap --stream-count=5 --stream-to=/tmp/cam1.raw
```

### Exposure, Gain, and Framerate Controls

```bash
v4l2-ctl -d /dev/video0 --list-ctrls-menus
v4l2-ctl -d /dev/video0 -c frame_rate=30000000 -c exposure=15000 -c gain=32
```

- `gain`: fixed-point `1/16x` (`18` ≈ 1.125x minimum, `256` = 16.0x maximum).
- `exposure`: microseconds (`54` – `498000` in `mode1`).
- `frame_rate`: micro-fps (`30000000` = 30.0 fps).

### Notes on `nvarguscamerasrc` (ISP) and DW9807V Autofocus

- **NVIDIA Argus / ISP**: This driver registers full `tegracam` + `tegra-camera-platform` bindings (`sensor-id=0` for CAM0, `sensor-id=1` for CAM1). However, NVIDIA does not ship an official ISP calibration file for Sony IMX708 in JetPack, so `nvarguscamerasrc` may produce an uncalibrated tint or fail on JetPack 6.2 unless an IMX708 ISP override/tuning file is provided in `/var/nvidia/nvcam/settings/`. Raw V4L2 capture (`bypass_mode=0`) works directly without proprietary ISP blobs.
- **Autofocus actuator (DW9807V)**: Each Pi Camera Module 3 Wide includes a DW9807V VCM at I2C address `0x0c` on the same `cam_i2cmux` bus as its IMX708 (`0x1a`). At power-on rest the wide-angle lens sits near hyperfocal focus. Because the VCM shares the camera power rail (`CAMx_PWDN`), it responds on I2C while the camera is active/streaming.

## Development, Testing, and Releasing

Run the full host test suite locally (C ABI checks for Linux 5.10 / 5.15 / 6.8, C functional unit tests, DTS/timing/`dtc`/`fdtoverlay` checks, `extlinux.conf` tests, and `.deb` packaging verification):

```bash
make test
```

### Cutting a New Release

1. Bump the version consistently in `VERSION`, `dkms.conf`, `debian/changelog`, `debian/postinst`, `debian/prerm`, and `MODULE_VERSION(...)` in `src/nv_imx708.c`.
2. Run `make test` to verify version consistency and all checks.
3. Commit, tag, and push:
   ```bash
   git tag -a vX.Y.Z -m "Release vX.Y.Z"
   git push origin main vX.Y.Z
   ```
4. GitHub Actions ([`.github/workflows/release.yml`](.github/workflows/release.yml)) automatically runs the full verification suite, builds reproducible `.deb`, `.tar.gz`, compiled `.dtbo` overlays, and `SHA256SUMS`, and publishes the GitHub Release.

## License

Licensed under **GNU General Public License v2.0 only** (`GPL-2.0-only`). See [LICENSE](LICENSE) and [COPYING](COPYING).
