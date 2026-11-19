# Build and install the Sony IMX708 dual-CSI driver for NVIDIA Jetson Orin Nano DevKit (p3768-0000).
# Auto-detects JetPack 6/7 (L4T R36/R38) vs JetPack 5 (L4T R35) from /etc/nv_tegra_release.
#
# Usage:
#   make               - Build nv_imx708.ko and dual-CSI .dtbo for the running L4T release
#   make modules       - Build nv_imx708.ko only
#   make dtbo          - Compile the dual-CSI .dtbo overlay only
#   make dtbo-all      - Compile both R36+ and R35 .dtbo overlays
#   sudo make install  - Install kernel module and .dtbo (run scripts/apply_overlay.sh to activate DTB)
#   make clean         - Remove build artifacts

DRIVER_NAME := nv_imx708
PKG_NAME    := jetson-imx708-dual
PKG_VER     := $(shell tr -d '[:space:]' < VERSION)
KERNEL_VER  ?= $(shell uname -r)
KERNEL_SRC  ?= /lib/modules/$(KERNEL_VER)/build

NVIDIA_OOT_SRC ?= /usr/src/nvidia/nvidia-oot
NVIDIA_SRC     ?= /usr/src/nvidia

# Auto-detect L4T major release (defaults to 36 when /etc/nv_tegra_release is absent)
L4T_MAJOR ?= $(shell \
	if [ -r /etc/nv_tegra_release ]; then \
		sed -n 's/^[^R]*R\([0-9][0-9]*\).*/\1/p' /etc/nv_tegra_release | head -n 1; \
	else \
		echo 36; \
	fi)

ifeq ($(L4T_MAJOR),35)
DTS_VARIANT := r35
else
DTS_VARIANT := r36
endif

DTS_DIR        := dts
DTS_FILE       := $(DTS_DIR)/tegra234-p3767-camera-p3768-imx708-dual-$(DTS_VARIANT).dts
DTBO_FILE      := tegra234-p3767-camera-p3768-imx708-dual-$(DTS_VARIANT).dtbo
DTBO_INSTALLED := /boot/tegra234-p3767-camera-p3768-imx708-dual.dtbo
DIST_DIR       := dist

MODULE_INSTALL_DIR := /lib/modules/$(KERNEL_VER)/updates/drivers/media/i2c
PWD := $(CURDIR)

.PHONY: all modules dtbo dtbo-all install dkms-install dkms-uninstall deb tarball package test clean clean-modules info help

all: modules dtbo

info:
	@echo "Package        : $(PKG_NAME) $(PKG_VER)"
	@echo "Kernel version : $(KERNEL_VER)"
	@echo "Kernel headers : $(KERNEL_SRC)"
	@echo "L4T major      : R$(L4T_MAJOR) (overlay variant: $(DTS_VARIANT))"
	@echo "NVIDIA OOT src : $(NVIDIA_OOT_SRC)"
	@echo "DTS source     : $(DTS_FILE)"

modules:
	@echo "Building $(DRIVER_NAME).ko for kernel $(KERNEL_VER) (L4T R$(L4T_MAJOR))..."
	@if [ ! -d "$(KERNEL_SRC)" ]; then \
		echo "Error: Kernel headers not found at $(KERNEL_SRC)" >&2; \
		echo "Install with: sudo apt update && sudo apt install -y nvidia-l4t-kernel-headers" >&2; \
		exit 1; \
	fi
	@if [ "$(DTS_VARIANT)" = "r36" ]; then \
		if [ ! -d "$(NVIDIA_OOT_SRC)/include" ] || [ ! -f "$(NVIDIA_OOT_SRC)/Module.symvers" ]; then \
			echo "Error: NVIDIA OOT headers/Module.symvers not found at $(NVIDIA_OOT_SRC)" >&2; \
			echo "Install with: sudo apt update && sudo apt install -y nvidia-l4t-kernel-oot-headers" >&2; \
			exit 1; \
		fi; \
	fi
	$(MAKE) -C "$(KERNEL_SRC)" M="$(PWD)/src" \
		KERNEL_SRC="$(KERNEL_SRC)" \
		NVIDIA_OOT_SRC="$(NVIDIA_OOT_SRC)" \
		NVIDIA_SRC="$(NVIDIA_SRC)" \
		$(if $(wildcard $(NVIDIA_OOT_SRC)/Module.symvers),KBUILD_EXTRA_SYMBOLS="$(NVIDIA_OOT_SRC)/Module.symvers",) \
		modules

dtbo: $(DTBO_FILE)

dtbo-all: tegra234-p3767-camera-p3768-imx708-dual-r36.dtbo tegra234-p3767-camera-p3768-imx708-dual-r35.dtbo

tegra234-p3767-camera-p3768-imx708-dual-%.dtbo: $(DTS_DIR)/tegra234-p3767-camera-p3768-imx708-dual-%.dts
	@if ! command -v dtc >/dev/null 2>&1; then \
		echo "Error: dtc not found. Install with: sudo apt install -y device-tree-compiler" >&2; \
		exit 1; \
	fi
	@echo "Compiling Device Tree overlay $< -> $@..."
	dtc -@ -I dts -O dtb -Wno-unit_address_vs_reg -o "$@" "$<"

install: all
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "Error: 'make install' must be run as root (use sudo make install)." >&2; \
		exit 1; \
	fi
	install -d "$(MODULE_INSTALL_DIR)"
	install -m 0644 "src/$(DRIVER_NAME).ko" "$(MODULE_INSTALL_DIR)/$(DRIVER_NAME).ko"
	install -m 0644 "$(DTBO_FILE)" "$(DTBO_INSTALLED)"
	printf '%s\n' "$(DRIVER_NAME)" > /etc/modules-load.d/nv_imx708.conf
	depmod -a "$(KERNEL_VER)"
	@echo ""
	@echo "Installed kernel module : $(MODULE_INSTALL_DIR)/$(DRIVER_NAME).ko"
	@echo "Installed DT overlay    : $(DTBO_INSTALLED)"
	@echo "Next step: run 'sudo ./scripts/apply_overlay.sh' to pre-merge the overlay into a safe boot entry, then reboot."

dkms-install:
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "Error: 'make dkms-install' must be run as root (use sudo make dkms-install)." >&2; \
		exit 1; \
	fi
	@if ! command -v dkms >/dev/null 2>&1; then \
		echo "Error: dkms not found. Install with: sudo apt install -y dkms" >&2; \
		exit 1; \
	fi
	install -d "/usr/src/$(PKG_NAME)-$(PKG_VER)/include" \
	           "/usr/src/$(PKG_NAME)-$(PKG_VER)/src" \
	           "/usr/src/$(PKG_NAME)-$(PKG_VER)/dts"
	install -m 0644 VERSION COPYING Makefile dkms.conf "/usr/src/$(PKG_NAME)-$(PKG_VER)/"
	install -m 0644 include/imx708.h "/usr/src/$(PKG_NAME)-$(PKG_VER)/include/"
	install -m 0644 src/Makefile src/imx708_mode_tbls.h src/nv_imx708.c "/usr/src/$(PKG_NAME)-$(PKG_VER)/src/"
	install -m 0644 dts/*.dts "/usr/src/$(PKG_NAME)-$(PKG_VER)/dts/"
	dkms status -m "$(PKG_NAME)" -v "$(PKG_VER)" 2>/dev/null | grep -q "$(PKG_NAME)" || \
		dkms add -m "$(PKG_NAME)" -v "$(PKG_VER)"
	dkms build -m "$(PKG_NAME)" -v "$(PKG_VER)" -k "$(KERNEL_VER)"
	dkms install -m "$(PKG_NAME)" -v "$(PKG_VER)" -k "$(KERNEL_VER)" --force
	printf '%s\n' "$(DRIVER_NAME)" > /etc/modules-load.d/nv_imx708.conf

dkms-uninstall:
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "Error: 'make dkms-uninstall' must be run as root (use sudo make dkms-uninstall)." >&2; \
		exit 1; \
	fi
	-dkms remove -m "$(PKG_NAME)" -v "$(PKG_VER)" --all
	rm -rf "/usr/src/$(PKG_NAME)-$(PKG_VER)"

deb:
	python3 packaging/build_deb.py "$(DIST_DIR)"

tarball:
	./packaging/build_tarball.sh "$(DIST_DIR)"

package: deb tarball
	@cd "$(DIST_DIR)" && \
		if command -v sha256sum >/dev/null 2>&1; then \
			sha256sum "$(PKG_NAME)-dkms_$(PKG_VER)_arm64.deb" "$(PKG_NAME)-$(PKG_VER).tar.gz" > SHA256SUMS; \
		else \
			shasum -a 256 "$(PKG_NAME)-dkms_$(PKG_VER)_arm64.deb" "$(PKG_NAME)-$(PKG_VER).tar.gz" > SHA256SUMS; \
		fi
	@echo "Checksums written to $(DIST_DIR)/SHA256SUMS:"
	@cat "$(DIST_DIR)/SHA256SUMS"

test:
	./tests/run_tests.sh

clean-modules:
	@if [ -d "$(KERNEL_SRC)" ]; then \
		$(MAKE) -C "$(KERNEL_SRC)" M="$(PWD)/src" clean 2>/dev/null || true; \
	fi
	rm -f src/*.o src/*.ko src/*.mod* src/.*.cmd src/modules.order src/Module.symvers
	rm -rf src/.tmp_versions

clean: clean-modules
	rm -f tegra234-p3767-camera-p3768-imx708-dual-*.dtbo
	rm -rf "$(DIST_DIR)"

help:
	@echo "Sony IMX708 Dual-CSI Driver ($(PKG_NAME) $(PKG_VER)) for Jetson Orin Nano DevKit"
	@echo ""
	@echo "Build & install targets (on Jetson):"
	@echo "  all            Build nv_imx708.ko and dual-CSI .dtbo (default)"
	@echo "  modules        Build nv_imx708.ko only"
	@echo "  dtbo           Compile dual-CSI .dtbo for detected L4T release (R$(L4T_MAJOR))"
	@echo "  dtbo-all       Compile both R36+ and R35 .dtbo overlays"
	@echo "  install        Install nv_imx708.ko and .dtbo directly (requires sudo)"
	@echo "  dkms-install   Install via DKMS so kernel upgrades auto-rebuild (requires sudo)"
	@echo "  dkms-uninstall Remove DKMS registration and source tree (requires sudo)"
	@echo ""
	@echo "Packaging targets (macOS or Linux):"
	@echo "  deb            Build dist/$(PKG_NAME)-dkms_$(PKG_VER)_arm64.deb"
	@echo "  tarball        Build dist/$(PKG_NAME)-$(PKG_VER).tar.gz"
	@echo "  package        Build .deb, .tar.gz, and dist/SHA256SUMS"
	@echo ""
	@echo "Utilities:"
	@echo "  test           Run full host C/DTS/extlinux/package test suite"
	@echo "  info           Show detected kernel/L4T build parameters"
	@echo "  clean          Remove build and packaging artifacts"
