// SPDX-License-Identifier: GPL-2.0-only
/*
 * nv_imx708.c - Sony IMX708 Tegracam V4L2 driver for NVIDIA Jetson Orin Nano
 *               (Raspberry Pi Camera Module 3 / Camera Module 3 Wide, dual CSI)
 *
 * Copyright (c) 2021-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * Copyright (C) 2022, Raspberry Pi Ltd
 * Copyright (c) 2023, RidgeRun <support@ridgerun.com>
 * Copyright (c) 2026, PerytonSpace / ARCh_Software_2027
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>

#include <media/tegra_v4l2_camera.h>
#include <media/tegracam_core.h>

#include "imx708.h"
#include "imx708_mode_tbls.h"

/*
 * Quad Bayer re-mosaic broken line correction strength (full-resolution mode0 only).
 * Set 0 to disable, or 2..5 for increasing correction strength (matches Raspberry Pi driver).
 */
static int qbc_adjust = 2;
module_param(qbc_adjust, int, 0644);
MODULE_PARM_DESC(qbc_adjust, "Quad Bayer broken line correction strength [0, 2-5]");

static const struct of_device_id imx708_of_match[] = {
	{ .compatible = "sony,imx708" },
	{ }
};
MODULE_DEVICE_TABLE(of, imx708_of_match);

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

struct imx708_mode_info {
	u64 pixel_rate_hz;
	u32 line_length_pck;
	u32 min_frame_length;
	u32 exposure_lines_min;
	u32 exposure_lines_step;
	bool remosaic;
};

static const struct imx708_mode_info imx708_mode_infos[IMX708_MODE_COUNT] = {
	[IMX708_MODE_4608x2592_14FPS] = {
		.pixel_rate_hz       = IMX708_MODE0_PIXEL_RATE_HZ,
		.line_length_pck     = IMX708_MODE0_LINE_LENGTH_PCK,
		.min_frame_length    = IMX708_MODE0_MIN_FRAME_LENGTH,
		.exposure_lines_min  = IMX708_MODE0_MIN_EXPOSURE_LINES,
		.exposure_lines_step = IMX708_MODE0_EXPOSURE_STEP_LINES,
		.remosaic            = true,
	},
	[IMX708_MODE_2304x1296_56FPS] = {
		.pixel_rate_hz       = IMX708_MODE1_PIXEL_RATE_HZ,
		.line_length_pck     = IMX708_MODE1_LINE_LENGTH_PCK,
		.min_frame_length    = IMX708_MODE1_MIN_FRAME_LENGTH,
		.exposure_lines_min  = IMX708_MODE1_MIN_EXPOSURE_LINES,
		.exposure_lines_step = IMX708_MODE1_EXPOSURE_STEP_LINES,
		.remosaic            = false,
	},
	[IMX708_MODE_1536x864_120FPS] = {
		.pixel_rate_hz       = IMX708_MODE2_PIXEL_RATE_HZ,
		.line_length_pck     = IMX708_MODE2_LINE_LENGTH_PCK,
		.min_frame_length    = IMX708_MODE2_MIN_FRAME_LENGTH,
		.exposure_lines_min  = IMX708_MODE2_MIN_EXPOSURE_LINES,
		.exposure_lines_step = IMX708_MODE2_EXPOSURE_STEP_LINES,
		.remosaic            = false,
	},
};

struct imx708 {
	struct i2c_client *i2c_client;
	struct v4l2_subdev *subdev;
	struct gpio_desc *reset_gpiod;
	u16 fine_integ_time;
	u32 frame_length;
	u8 long_exp_shift;
	struct camera_common_data *s_data;
	struct tegracam_device *tc_dev;
};

static const struct regmap_config sensor_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	.use_single_rw = true,
#else
	.use_single_read = true,
	.use_single_write = true,
#endif
};

static inline const struct imx708_mode_info *
imx708_get_mode_info(const struct camera_common_data *s_data)
{
	if (s_data->mode >= 0 && s_data->mode < IMX708_MODE_COUNT)
		return &imx708_mode_infos[s_data->mode];
	return &imx708_mode_infos[IMX708_MODE_4608x2592_14FPS];
}

static inline void imx708_get_frame_length_regs(imx708_reg *regs,
						u32 frame_length)
{
	regs[0].addr = IMX708_FRAME_LENGTH_ADDR_MSB;
	regs[0].val  = (frame_length >> 8) & 0xFF;
	regs[1].addr = IMX708_FRAME_LENGTH_ADDR_LSB;
	regs[1].val  = frame_length & 0xFF;
}

static inline void imx708_get_coarse_integ_time_regs(imx708_reg *regs,
						     u32 coarse_time)
{
	regs[0].addr = IMX708_COARSE_INTEG_TIME_ADDR_MSB;
	regs[0].val  = (coarse_time >> 8) & 0xFF;
	regs[1].addr = IMX708_COARSE_INTEG_TIME_ADDR_LSB;
	regs[1].val  = coarse_time & 0xFF;
}

static inline void imx708_get_gain_regs(imx708_reg *regs, u16 gain_code)
{
	regs[0].addr = IMX708_ANALOG_GAIN_ADDR_MSB;
	regs[0].val  = (gain_code >> IMX708_SHIFT_8_BITS) & IMX708_MASK_LSB_2_BITS;
	regs[1].addr = IMX708_ANALOG_GAIN_ADDR_LSB;
	regs[1].val  = gain_code & IMX708_MASK_LSB_8_BITS;
}

static inline int imx708_read_reg(struct camera_common_data *s_data,
				  u16 addr, u8 *val)
{
	u32 reg_val = 0;
	int err;

	err = regmap_read(s_data->regmap, addr, &reg_val);
	if (!err)
		*val = reg_val & 0xFF;

	return err;
}

static inline int imx708_write_reg(struct camera_common_data *s_data,
				   u16 addr, u8 val)
{
	int err;

	err = regmap_write(s_data->regmap, addr, val);
	if (err)
		dev_err(s_data->dev, "%s: i2c write failed, 0x%04x = 0x%02x (%d)\n",
			__func__, addr, val, err);

	return err;
}

static int imx708_write_table(struct imx708 *priv, const imx708_reg table[])
{
	return regmap_util_write_table_8(priv->s_data->regmap, table, NULL, 0,
					 IMX708_TABLE_WAIT_MS,
					 IMX708_TABLE_END);
}

static int imx708_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	int err;

	dev_dbg(dev, "%s: group hold = %u\n", __func__, val);

	err = imx708_write_reg(s_data, IMX708_GROUP_HOLD_ADDR, val ? 0x01 : 0x00);
	if (err)
		dev_err(dev, "%s: group hold write failed (%d)\n", __func__, err);

	return err;
}

static int imx708_get_fine_integ_time(struct imx708 *priv, u16 *fine_time)
{
	struct camera_common_data *s_data = priv->s_data;
	u8 reg_val[2] = { 0 };
	int err;

	err = imx708_read_reg(s_data, IMX708_FINE_INTEG_TIME_ADDR_MSB, &reg_val[0]);
	if (err)
		return err;

	err = imx708_read_reg(s_data, IMX708_FINE_INTEG_TIME_ADDR_LSB, &reg_val[1]);
	if (err)
		return err;

	*fine_time = ((u16)reg_val[0] << 8) | reg_val[1];
	return 0;
}

static int imx708_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = s_data->dev;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	imx708_reg gain_regs[2];
	s64 gain_factor;
	s64 gain_code;
	int err = 0;
	unsigned int i;

	if (val <= 0)
		return -EINVAL;

	if (val < mode->control_properties.min_gain_val)
		val = mode->control_properties.min_gain_val;
	else if (val > mode->control_properties.max_gain_val)
		val = mode->control_properties.max_gain_val;

	gain_factor = mode->control_properties.gain_factor;
	if (gain_factor <= 0)
		gain_factor = 16;

	/*
	 * Sony IMX708 analog gain equation:
	 *   gain = 1024 / (1024 - code)
	 *   code = 1024 - (1024 * gain_factor / val)
	 */
	gain_code = IMX708_ANALOG_GAIN_C0 -
		    div64_s64(gain_factor * IMX708_ANALOG_GAIN_C0, val);

	if (gain_code < IMX708_ANA_GAIN_MIN)
		gain_code = IMX708_ANA_GAIN_MIN;
	else if (gain_code > IMX708_ANA_GAIN_MAX)
		gain_code = IMX708_ANA_GAIN_MAX;

	dev_dbg(dev, "%s: val=%lld (/%lld) -> reg_code=%lld\n",
		__func__, val, gain_factor, gain_code);

	imx708_get_gain_regs(gain_regs, (u16)gain_code);
	for (i = 0; i < ARRAY_SIZE(gain_regs); i++) {
		err = imx708_write_reg(s_data, gain_regs[i].addr, gain_regs[i].val);
		if (err) {
			dev_err(dev, "%s: gain write error (%d)\n", __func__, err);
			return err;
		}
	}

	return 0;
}

static int imx708_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct imx708 *priv = (struct imx708 *)tegracam_get_privdata(tc_dev);
	struct device *dev = tc_dev->dev;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	const struct imx708_mode_info *info = imx708_get_mode_info(s_data);
	imx708_reg fl_regs[2];
	u64 frame_length_u64;
	u32 reg_fl;
	u8 shift = 0;
	int err = 0;
	unsigned int i;

	if (val <= 0 || mode->control_properties.framerate_factor == 0)
		return -EINVAL;

	frame_length_u64 = div64_u64(
		info->pixel_rate_hz * (u64)mode->control_properties.framerate_factor,
		(u64)info->line_length_pck * (u64)val);

	if (frame_length_u64 < info->min_frame_length)
		frame_length_u64 = info->min_frame_length;

	while (frame_length_u64 > IMX708_MAX_FRAME_LENGTH &&
	       shift < IMX708_LONG_EXP_SHIFT_MAX) {
		frame_length_u64 >>= 1;
		shift++;
	}

	if (frame_length_u64 > IMX708_MAX_FRAME_LENGTH)
		frame_length_u64 = IMX708_MAX_FRAME_LENGTH;

	reg_fl = (u32)frame_length_u64;

	dev_dbg(dev, "%s: val=%llde-6 fps -> frame_length=%u (shift=%u)\n",
		__func__, val, reg_fl << shift, shift);

	imx708_get_frame_length_regs(fl_regs, reg_fl);
	for (i = 0; i < ARRAY_SIZE(fl_regs); i++) {
		err = imx708_write_reg(s_data, fl_regs[i].addr, fl_regs[i].val);
		if (err) {
			dev_err(dev, "%s: frame_length write error (%d)\n",
				__func__, err);
			return err;
		}
	}

	err = imx708_write_reg(s_data, IMX708_LONG_EXP_SHIFT_ADDR, shift);
	if (err) {
		dev_err(dev, "%s: long_exp_shift write error (%d)\n",
			__func__, err);
		return err;
	}

	priv->long_exp_shift = shift;
	priv->frame_length = reg_fl << shift;

	return 0;
}

static int imx708_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct imx708 *priv = (struct imx708 *)tegracam_get_privdata(tc_dev);
	struct device *dev = tc_dev->dev;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	const struct imx708_mode_info *info = imx708_get_mode_info(s_data);
	imx708_reg ct_regs[2];
	u32 frame_length;
	u32 max_coarse_time;
	u32 coarse_time;
	u32 reg_coarse;
	s64 fine_integ_us;
	u64 coarse_u64;
	int err = 0;
	unsigned int i;

	if (val < 0 || mode->control_properties.exposure_factor == 0)
		return -EINVAL;

	frame_length = priv->frame_length;
	if (frame_length < info->min_frame_length)
		frame_length = info->min_frame_length;

	if (frame_length > IMX708_EXPOSURE_OFFSET)
		max_coarse_time = frame_length - IMX708_EXPOSURE_OFFSET;
	else
		max_coarse_time = info->exposure_lines_min;

	fine_integ_us = (s64)div64_u64(
		(u64)priv->fine_integ_time * (u64)mode->control_properties.exposure_factor,
		info->pixel_rate_hz);

	if (val <= fine_integ_us) {
		coarse_time = info->exposure_lines_min;
	} else {
		coarse_u64 = div64_u64(
			(u64)(val - fine_integ_us) * info->pixel_rate_hz,
			(u64)mode->control_properties.exposure_factor *
			(u64)info->line_length_pck);
		if (coarse_u64 > max_coarse_time)
			coarse_time = max_coarse_time;
		else
			coarse_time = (u32)coarse_u64;
	}

	if (coarse_time < info->exposure_lines_min)
		coarse_time = info->exposure_lines_min;

	/* Enforce sensor exposure line step (1 for full-res, 2 for 2x2 binned modes) */
	if (info->exposure_lines_step > 1)
		coarse_time -= coarse_time % info->exposure_lines_step;

	if (coarse_time < info->exposure_lines_min)
		coarse_time = info->exposure_lines_min;

	reg_coarse = coarse_time >> priv->long_exp_shift;
	if (reg_coarse < 1)
		reg_coarse = 1;
	else if (reg_coarse > IMX708_MAX_FRAME_LENGTH - IMX708_EXPOSURE_OFFSET)
		reg_coarse = IMX708_MAX_FRAME_LENGTH - IMX708_EXPOSURE_OFFSET;

	dev_dbg(dev, "%s: val=%lld us -> coarse_time=%u lines (reg=%u, shift=%u)\n",
		__func__, val, coarse_time, reg_coarse, priv->long_exp_shift);

	imx708_get_coarse_integ_time_regs(ct_regs, reg_coarse);
	for (i = 0; i < ARRAY_SIZE(ct_regs); i++) {
		err = imx708_write_reg(s_data, ct_regs[i].addr, ct_regs[i].val);
		if (err) {
			dev_err(dev, "%s: coarse_time write error (%d)\n",
				__func__, err);
			return err;
		}
	}

	return 0;
}

static struct tegracam_ctrl_ops imx708_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_gain = imx708_set_gain,
	.set_exposure = imx708_set_exposure,
	.set_frame_rate = imx708_set_frame_rate,
	.set_group_hold = imx708_set_group_hold,
};

static int imx708_power_on(struct camera_common_data *s_data)
{
	struct imx708 *priv = (struct imx708 *)s_data->priv;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int err = 0;

	dev_dbg(dev, "%s: power on\n", __func__);
	if (pdata && pdata->power_on) {
		err = pdata->power_on(pw);
		if (err)
			dev_err(dev, "%s failed (%d)\n", __func__, err);
		else
			pw->state = SWITCH_ON;
		return err;
	}

	if (priv && priv->reset_gpiod)
		gpiod_set_value_cansleep(priv->reset_gpiod, 0);

	if (unlikely(!(pw->avdd || pw->iovdd || pw->dvdd)))
		goto skip_power_seqn;

	usleep_range(10, 20);

	if (pw->avdd) {
		err = regulator_enable(pw->avdd);
		if (err)
			goto imx708_avdd_fail;
	}

	if (pw->iovdd) {
		err = regulator_enable(pw->iovdd);
		if (err)
			goto imx708_iovdd_fail;
	}

	if (pw->dvdd) {
		err = regulator_enable(pw->dvdd);
		if (err)
			goto imx708_dvdd_fail;
	}

	usleep_range(10, 20);

skip_power_seqn:
	if (priv && priv->reset_gpiod)
		gpiod_set_value_cansleep(priv->reset_gpiod, 1);

	usleep_range(IMX708_POWER_ON_DELAY_MIN_US, IMX708_POWER_ON_DELAY_MAX_US);

	pw->state = SWITCH_ON;
	return 0;

imx708_dvdd_fail:
	if (pw->iovdd)
		regulator_disable(pw->iovdd);
imx708_iovdd_fail:
	if (pw->avdd)
		regulator_disable(pw->avdd);
imx708_avdd_fail:
	dev_err(dev, "%s failed (%d)\n", __func__, err);
	return -ENODEV;
}

static int imx708_power_off(struct camera_common_data *s_data)
{
	struct imx708 *priv = (struct imx708 *)s_data->priv;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int err = 0;

	dev_dbg(dev, "%s: power off\n", __func__);

	if (pdata && pdata->power_off) {
		err = pdata->power_off(pw);
		if (err) {
			dev_err(dev, "%s failed (%d)\n", __func__, err);
			return err;
		}
	} else {
		if (priv && priv->reset_gpiod)
			gpiod_set_value_cansleep(priv->reset_gpiod, 0);

		usleep_range(10, 20);

		if (pw->dvdd)
			regulator_disable(pw->dvdd);
		if (pw->iovdd)
			regulator_disable(pw->iovdd);
		if (pw->avdd)
			regulator_disable(pw->avdd);
	}

	pw->state = SWITCH_OFF;
	return 0;
}

static int imx708_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (unlikely(!pw))
		return -EFAULT;

	if (pw->dvdd)
		devm_regulator_put(pw->dvdd);
	if (pw->avdd)
		devm_regulator_put(pw->avdd);
	if (pw->iovdd)
		devm_regulator_put(pw->iovdd);

	pw->dvdd = NULL;
	pw->avdd = NULL;
	pw->iovdd = NULL;

	/* reset_gpiod is managed by devm_gpiod_get_optional() */
	return 0;
}

static int imx708_power_get(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct clk *parent;
	int err = 0;

	if (!pdata) {
		dev_err(dev, "pdata missing\n");
		return -EFAULT;
	}

	/* Optional external MCLK (Pi Camera Module 3 has an onboard 24 MHz oscillator) */
	if (pdata->mclk_name) {
		pw->mclk = devm_clk_get(dev, pdata->mclk_name);
		if (IS_ERR(pw->mclk)) {
			dev_err(dev, "unable to get clock %s\n", pdata->mclk_name);
			return PTR_ERR(pw->mclk);
		}

		if (pdata->parentclk_name) {
			parent = devm_clk_get(dev, pdata->parentclk_name);
			if (IS_ERR(parent))
				dev_err(dev, "unable to get parent clock %s\n",
					pdata->parentclk_name);
			else
				clk_set_parent(pw->mclk, parent);
		}
	}

	if (pdata->regulators.avdd)
		err |= camera_common_regulator_get(dev, &pw->avdd,
						   pdata->regulators.avdd);
	if (pdata->regulators.iovdd)
		err |= camera_common_regulator_get(dev, &pw->iovdd,
						   pdata->regulators.iovdd);
	if (pdata->regulators.dvdd)
		err |= camera_common_regulator_get(dev, &pw->dvdd,
						   pdata->regulators.dvdd);
	if (err)
		dev_err(dev, "%s: unable to get regulator(s)\n", __func__);

	pw->state = SWITCH_OFF;
	return err;
}

static struct camera_common_pdata *imx708_parse_dt(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *np = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	int err;

	if (!np)
		return NULL;

	match = of_match_device(imx708_of_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(dev, sizeof(*board_priv_pdata), GFP_KERNEL);
	if (!board_priv_pdata)
		return NULL;

	err = of_property_read_string(np, "mclk", &board_priv_pdata->mclk_name);
	if (err)
		dev_dbg(dev, "mclk not present; using onboard 24 MHz oscillator\n");

	err = of_property_read_string(np, "avdd-reg",
				      &board_priv_pdata->regulators.avdd);
	err |= of_property_read_string(np, "iovdd-reg",
				       &board_priv_pdata->regulators.iovdd);
	err |= of_property_read_string(np, "dvdd-reg",
				       &board_priv_pdata->regulators.dvdd);
	if (err)
		dev_dbg(dev, "regulator properties not present; assuming onboard rails\n");

	board_priv_pdata->has_eeprom = of_property_read_bool(np, "has-eeprom");

	return board_priv_pdata;
}

static int imx708_init_pdaf_gains(struct imx708 *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	u8 val = 0;
	int err;
	unsigned int i;

	err = imx708_read_reg(s_data, IMX708_REG_BASE_SPC_GAINS_L, &val);
	if (err || val != 0x40)
		return err;

	for (i = 0; i < IMX708_SPC_GAIN_COUNT; i++) {
		err = imx708_write_reg(s_data, IMX708_REG_BASE_SPC_GAINS_L + i,
				       imx708_pdaf_gains[0][i % 9]);
		if (err)
			return err;
	}

	for (i = 0; i < IMX708_SPC_GAIN_COUNT; i++) {
		err = imx708_write_reg(s_data, IMX708_REG_BASE_SPC_GAINS_R + i,
				       imx708_pdaf_gains[1][i % 9]);
		if (err)
			return err;
	}

	return 0;
}

static int imx708_set_mode(struct tegracam_device *tc_dev)
{
	struct imx708 *priv = (struct imx708 *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	const struct imx708_mode_info *info;
	int err;

	if (s_data->mode < 0 || s_data->mode >= IMX708_MODE_COUNT) {
		dev_err(tc_dev->dev, "unsupported mode index %d\n", s_data->mode);
		return -EINVAL;
	}

	err = imx708_write_table(priv, mode_table[IMX708_MODE_COMMON]);
	if (err) {
		dev_err(tc_dev->dev, "failed writing common table (%d)\n", err);
		return err;
	}

	err = imx708_init_pdaf_gains(priv);
	if (err) {
		dev_err(tc_dev->dev, "failed initializing PDAF SPC gains (%d)\n", err);
		return err;
	}

	err = imx708_write_table(priv, mode_table[s_data->mode]);
	if (err) {
		dev_err(tc_dev->dev, "failed writing mode %d (%d)\n",
			s_data->mode, err);
		return err;
	}

	info = imx708_get_mode_info(s_data);
	priv->long_exp_shift = 0;
	priv->frame_length = info->min_frame_length;

	/* Quad Bayer broken-line correction (full-resolution mode only) */
	if (info->remosaic && qbc_adjust > 0) {
		u8 intensity = (qbc_adjust > 5) ? 5 : (u8)qbc_adjust;

		err = imx708_write_reg(s_data, IMX708_LPF_INTENSITY_ADDR, intensity);
		if (err)
			return err;
		err = imx708_write_reg(s_data, IMX708_LPF_INTENSITY_EN_ADDR,
				       IMX708_LPF_INTENSITY_ENABLED);
		if (err)
			return err;
	} else {
		err = imx708_write_reg(s_data, IMX708_LPF_INTENSITY_EN_ADDR,
				       IMX708_LPF_INTENSITY_DISABLED);
		if (err)
			return err;
	}

	return 0;
}

static int imx708_start_streaming(struct tegracam_device *tc_dev)
{
	struct imx708 *priv = (struct imx708 *)tegracam_get_privdata(tc_dev);

	dev_dbg(tc_dev->dev, "%s: mode=%d\n", __func__, tc_dev->s_data->mode);
	return imx708_write_table(priv, mode_table[IMX708_START_STREAM]);
}

static int imx708_stop_streaming(struct tegracam_device *tc_dev)
{
	struct imx708 *priv = (struct imx708 *)tegracam_get_privdata(tc_dev);

	dev_dbg(tc_dev->dev, "%s:\n", __func__);
	return imx708_write_table(priv, mode_table[IMX708_STOP_STREAM]);
}

static struct camera_common_sensor_ops imx708_common_ops = {
	.numfrmfmts = ARRAY_SIZE(imx708_frmfmt),
	.frmfmt_table = imx708_frmfmt,
	.power_on = imx708_power_on,
	.power_off = imx708_power_off,
	.write_reg = imx708_write_reg,
	.read_reg = imx708_read_reg,
	.parse_dt = imx708_parse_dt,
	.power_get = imx708_power_get,
	.power_put = imx708_power_put,
	.set_mode = imx708_set_mode,
	.start_streaming = imx708_start_streaming,
	.stop_streaming = imx708_stop_streaming,
};

static int imx708_board_setup(struct imx708 *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct device *dev = s_data->dev;
	u8 reg_val[2] = { 0 };
	u16 chip_id;
	int err;

	err = imx708_power_on(s_data);
	if (err) {
		dev_err(dev, "error during power on sensor (%d)\n", err);
		return err;
	}

	/* Verify Sony IMX708 chip ID (0x0016/0x0017 == 0x0708) */
	err = imx708_read_reg(s_data, IMX708_CHIP_ID_ADDR_MSB, &reg_val[0]);
	if (err) {
		dev_err(dev, "%s: i2c read chip id MSB failed (%d)\n",
			__func__, err);
		goto err_reg_probe;
	}
	err = imx708_read_reg(s_data, IMX708_CHIP_ID_ADDR_LSB, &reg_val[1]);
	if (err) {
		dev_err(dev, "%s: i2c read chip id LSB failed (%d)\n",
			__func__, err);
		goto err_reg_probe;
	}

	chip_id = ((u16)reg_val[0] << 8) | reg_val[1];
	if (chip_id != IMX708_CHIP_ID) {
		dev_err(dev, "%s: unexpected chip id 0x%04x (expected 0x%04x)\n",
			__func__, chip_id, IMX708_CHIP_ID);
		err = -ENODEV;
		goto err_reg_probe;
	}

	dev_info(dev, "verified Sony IMX708 chip id 0x%04x\n", chip_id);

	err = imx708_get_fine_integ_time(priv, &priv->fine_integ_time);
	if (err) {
		dev_warn(dev, "%s: fine integration time read failed (%d), defaulting to 0\n",
			 __func__, err);
		priv->fine_integ_time = 0;
		err = 0;
	}

err_reg_probe:
	imx708_power_off(s_data);
	return err;
}

static int imx708_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);
	return 0;
}

static const struct v4l2_subdev_internal_ops imx708_subdev_internal_ops = {
	.open = imx708_open,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int imx708_probe(struct i2c_client *client)
#else
static int imx708_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct tegracam_device *tc_dev;
	struct imx708 *priv;
	int err;

	dev_info(dev, "probing IMX708 sensor at I2C addr 0x%02x\n", client->addr);

	if (!IS_ENABLED(CONFIG_OF) || !dev->of_node)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	tc_dev = devm_kzalloc(dev, sizeof(*tc_dev), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	priv->reset_gpiod = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpiod)) {
		err = PTR_ERR(priv->reset_gpiod);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "failed to get reset-gpios (%d)\n", err);
		return err;
	}

	priv->i2c_client = tc_dev->client = client;
	priv->frame_length = IMX708_MODE0_MIN_FRAME_LENGTH;
	tc_dev->dev = dev;
	strscpy(tc_dev->name, "imx708", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &sensor_regmap_config;
	tc_dev->sensor_ops = &imx708_common_ops;
	tc_dev->v4l2sd_internal_ops = &imx708_subdev_internal_ops;
	tc_dev->tcctrl_ops = &imx708_ctrl_ops;

	err = tegracam_device_register(tc_dev);
	if (err) {
		dev_err(dev, "tegra camera driver registration failed (%d)\n", err);
		return err;
	}

	priv->tc_dev = tc_dev;
	priv->s_data = tc_dev->s_data;
	priv->subdev = &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void *)priv);

	err = imx708_board_setup(priv);
	if (err) {
		dev_err(dev, "board setup failed (%d)\n", err);
		goto err_tegracam_unreg;
	}

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
		dev_err(dev, "tegra camera subdev registration failed (%d)\n", err);
		goto err_tegracam_unreg;
	}

	dev_info(dev, "detected IMX708 sensor on bus %d addr 0x%02x\n",
		 client->adapter->nr, client->addr);
	return 0;

err_tegracam_unreg:
	tegracam_device_unregister(tc_dev);
	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static void imx708_remove(struct i2c_client *client)
#else
static int imx708_remove(struct i2c_client *client)
#endif
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct imx708 *priv;

	if (!s_data) {
		dev_err(&client->dev, "camera common data is NULL\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
		return -EINVAL;
#else
		return;
#endif
	}

	priv = (struct imx708 *)s_data->priv;
	if (priv && priv->tc_dev) {
		tegracam_v4l2subdev_unregister(priv->tc_dev);
		tegracam_device_unregister(priv->tc_dev);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

static const struct i2c_device_id imx708_id[] = {
	{ "imx708", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, imx708_id);

static struct i2c_driver imx708_i2c_driver = {
	.driver = {
		.name = "imx708",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(imx708_of_match),
	},
	.probe = imx708_probe,
	.remove = imx708_remove,
	.id_table = imx708_id,
};

module_i2c_driver(imx708_i2c_driver);

MODULE_DESCRIPTION("Tegracam V4L2 driver for Sony IMX708 (RPi Camera Module 3 / 3 Wide)");
MODULE_AUTHOR("PerytonSpace / ARCh_Software_2027");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.0");
