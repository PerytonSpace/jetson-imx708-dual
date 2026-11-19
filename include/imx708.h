/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * imx708.h - Sony IMX708 camera sensor definitions (Raspberry Pi Camera Module 3 / 3 Wide)
 *
 * Copyright (C) 2022, Raspberry Pi Ltd
 * Copyright (c) 2023, RidgeRun <support@ridgerun.com>
 * Copyright (c) 2026, PerytonSpace / ARCh_Software_2027
 */

#ifndef __IMX708_H__
#define __IMX708_H__

/* IMX708 identification */
#define IMX708_CHIP_ID_ADDR_MSB			0x0016
#define IMX708_CHIP_ID_ADDR_LSB			0x0017
#define IMX708_CHIP_ID				0x0708

/* Streaming and reset */
#define IMX708_MODE_SELECT_ADDR			0x0100
#define IMX708_MODE_STANDBY			0x00
#define IMX708_MODE_STREAMING			0x01
#define IMX708_ORIENTATION_ADDR			0x0101
#define IMX708_SW_RESET_ADDR			0x0103
#define IMX708_GROUP_HOLD_ADDR			0x0104

/* Timing, exposure, and gain registers */
#define IMX708_FINE_INTEG_TIME_ADDR_MSB		0x0200
#define IMX708_FINE_INTEG_TIME_ADDR_LSB		0x0201
#define IMX708_COARSE_INTEG_TIME_ADDR_MSB	0x0202
#define IMX708_COARSE_INTEG_TIME_ADDR_LSB	0x0203
#define IMX708_ANALOG_GAIN_ADDR_MSB		0x0204
#define IMX708_ANALOG_GAIN_ADDR_LSB		0x0205
#define IMX708_DIGITAL_GAIN_ADDR_MSB		0x020E
#define IMX708_DIGITAL_GAIN_ADDR_LSB		0x020F
#define IMX708_FRAME_LENGTH_ADDR_MSB		0x0340
#define IMX708_FRAME_LENGTH_ADDR_LSB		0x0341
#define IMX708_LINE_LENGTH_ADDR_MSB		0x0342
#define IMX708_LINE_LENGTH_ADDR_LSB		0x0343
#define IMX708_LONG_EXP_SHIFT_ADDR		0x3100

/* PDAF shield pixel correction (SPC) gain LUT bases */
#define IMX708_REG_BASE_SPC_GAINS_L		0x7B10
#define IMX708_REG_BASE_SPC_GAINS_R		0x7C00
#define IMX708_SPC_GAIN_COUNT			54

/* Quad Bayer re-mosaic broken line correction (full-res mode only) */
#define IMX708_LPF_INTENSITY_EN_ADDR		0xC428
#define IMX708_LPF_INTENSITY_ENABLED		0x00
#define IMX708_LPF_INTENSITY_DISABLED		0x01
#define IMX708_LPF_INTENSITY_ADDR		0xC429

/*
 * Analog gain conversion:
 *   gain_x = 1024.0 / (1024 - reg_code)
 *   reg_code = 1024 - (1024 * gain_factor / val)
 *
 * Sony IMX708 datasheet valid range:
 *   code 112 -> ~1.123x (min)
 *   code 960 -> 16.000x (max)
 */
#define IMX708_ANALOG_GAIN_C0			1024
#define IMX708_ANA_GAIN_MIN			112
#define IMX708_ANA_GAIN_MAX			960
#define IMX708_SHIFT_8_BITS			8
#define IMX708_MASK_LSB_2_BITS			0x0003
#define IMX708_MASK_LSB_8_BITS			0x00FF

/*
 * Frame timing and exposure limits (Sony IMX708 / RPi libcamera cam_helper_imx708):
 *   Minimum margin between frame_length_lines and coarse_integration_time is 48 lines.
 *   Long exposure shift register (0x3100) supports 0..7 (frame length multiplier 1..128).
 */
#define IMX708_EXPOSURE_OFFSET			48
#define IMX708_MAX_FRAME_LENGTH			0xFFDC
#define IMX708_LONG_EXP_SHIFT_MAX		7

/*
 * Power-on wait after XCLR / CAM_PWDN de-assertion.
 * Datasheet T7 minimum is 8 ms; 20 ms leaves margin for the Pi Camera Module 3
 * onboard regulator sequencing and 24 MHz oscillator stabilization.
 */
#define IMX708_POWER_ON_DELAY_MIN_US		20000
#define IMX708_POWER_ON_DELAY_MAX_US		21000

/*
 * Per-mode internal sensor pixel rate and hardware line length (0x0342/0x0343).
 *
 * Note: All 3 modes output 10-bit RAW over 2 MIPI CSI-2 D-PHY lanes at a
 * 450 MHz link clock (900 Mbps/lane -> 180 Mpixels/s bus rate). The Device Tree
 * uses pix_clk_hz = 180000000 so Tegra NVCSI computes the exact 450 MHz D-PHY
 * clock, while DT line_length is scaled to preserve the identical line rate
 * (pix_clk_hz / dt_line_length == internal_pixel_rate / hw_line_length).
 */
#define IMX708_MODE0_PIXEL_RATE_HZ		595200000ULL
#define IMX708_MODE0_LINE_LENGTH_PCK		15648U		/* 0x3D20 */
#define IMX708_MODE0_MIN_FRAME_LENGTH		2650U		/* 2592 + 58 */
#define IMX708_MODE0_MIN_EXPOSURE_LINES		8U
#define IMX708_MODE0_EXPOSURE_STEP_LINES	1U

#define IMX708_MODE1_PIXEL_RATE_HZ		585600000ULL
#define IMX708_MODE1_LINE_LENGTH_PCK		7824U		/* 0x1E90 */
#define IMX708_MODE1_MIN_FRAME_LENGTH		1336U		/* 1296 + 40 */
#define IMX708_MODE1_MIN_EXPOSURE_LINES		4U
#define IMX708_MODE1_EXPOSURE_STEP_LINES	2U

#define IMX708_MODE2_PIXEL_RATE_HZ		566400000ULL
#define IMX708_MODE2_LINE_LENGTH_PCK		5216U		/* 0x1460 */
#define IMX708_MODE2_MIN_FRAME_LENGTH		904U		/* 864 + 40 */
#define IMX708_MODE2_MIN_EXPOSURE_LINES		4U
#define IMX708_MODE2_EXPOSURE_STEP_LINES	2U

#endif /* __IMX708_H__ */
