# SPDX-License-Identifier: GPL-2.0-only
# Out-of-tree Kbuild Makefile for nv_imx708 (Sony IMX708 / Pi Camera Module 3 Wide)
# Supports JetPack 6.x (L4T R36.x, nvidia-oot), JetPack 7.x (L4T R38.x), and JetPack 5.x (L4T R35.x).

obj-m := nv_imx708.o

NVIDIA_OOT_SRC ?= /usr/src/nvidia/nvidia-oot
NVIDIA_SRC     ?= /usr/src/nvidia

# Include paths for NVIDIA Tegracam / V4L2 camera headers:
# - JetPack 6.x / 7.x (L4T R36+): /usr/src/nvidia/nvidia-oot/include
# - JetPack 5.x (L4T R35):        $(KERNEL_SRC)/nvidia/include (or nvidia-oot fallback)
ccflags-y := -I$(src)/../include

ifneq ($(wildcard $(NVIDIA_OOT_SRC)/include),)
ccflags-y += -I$(NVIDIA_OOT_SRC)/include
ccflags-y += -I$(NVIDIA_OOT_SRC)/include/media
endif

ifneq ($(wildcard $(NVIDIA_SRC)/include),)
ccflags-y += -I$(NVIDIA_SRC)/include
endif

ifdef KERNEL_SRC
ifneq ($(wildcard $(KERNEL_SRC)/nvidia/include),)
ccflags-y += -I$(KERNEL_SRC)/nvidia/include
endif
ifneq ($(wildcard $(KERNEL_SRC)/../nvidia/include),)
ccflags-y += -I$(KERNEL_SRC)/../nvidia/include
endif
endif

# Resolve exported tegracam/camera_common symbols from NVIDIA OOT modules (L4T R36+)
ifneq ($(wildcard $(NVIDIA_OOT_SRC)/Module.symvers),)
KBUILD_EXTRA_SYMBOLS ?= $(NVIDIA_OOT_SRC)/Module.symvers
endif
