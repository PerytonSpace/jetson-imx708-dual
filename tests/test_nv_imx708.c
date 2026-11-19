#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Include the real kernel driver source to exercise static functions and mode tables */
#include "nv_imx708.c"

static u8 mock_regs[65536];
static int mock_i2c_fail = 0;
static int mock_gpio_val = 0;
static struct gpio_desc mock_gpiod = { .val = 0 };
static struct camera_common_data *mock_s_data_ptr = NULL;

struct gpio_desc *devm_gpiod_get_optional(struct device *dev, const char *con_id,
					  enum gpiod_flags flags)
{
	(void)dev;
	(void)con_id;
	(void)flags;
	mock_gpio_val = 0;
	return &mock_gpiod;
}

void gpiod_set_value_cansleep(struct gpio_desc *desc, int value)
{
	(void)desc;
	mock_gpio_val = value;
}

int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
	(void)map;
	if (mock_i2c_fail)
		return -EIO;
	*val = mock_regs[reg & 0xFFFF];
	return 0;
}

int regmap_write(struct regmap *map, unsigned int reg, unsigned int val)
{
	(void)map;
	if (mock_i2c_fail)
		return -EIO;
	mock_regs[reg & 0xFFFF] = (u8)(val & 0xFF);
	return 0;
}

int regmap_util_write_table_8(struct regmap *regmap, const struct reg_8 table[],
			      const struct reg_8 override_list[],
			      int num_override_regs, u16 wait_ms_addr, u16 end_addr)
{
	int i;

	(void)override_list;
	(void)num_override_regs;
	for (i = 0; ; i++) {
		int err;

		if (table[i].addr == end_addr)
			break;
		if (table[i].addr == wait_ms_addr)
			continue;
		err = regmap_write(regmap, table[i].addr, table[i].val);
		if (err)
			return err;
	}
	return 0;
}

int camera_common_regulator_get(struct device *dev, struct regulator **vreg,
				const char *name)
{
	(void)dev;
	(void)vreg;
	(void)name;
	return 0;
}

struct camera_common_data *to_camera_common_data(const struct device *dev)
{
	(void)dev;
	return mock_s_data_ptr;
}

int tegracam_device_register(struct tegracam_device *tc_dev)
{
	static struct camera_common_data s_data;
	static struct camera_common_power_rail pw;
	static struct regmap rm;

	memset(&s_data, 0, sizeof(s_data));
	memset(&pw, 0, sizeof(pw));
	s_data.dev = tc_dev->dev;
	s_data.power = &pw;
	s_data.regmap = &rm;
	s_data.pdata = tc_dev->sensor_ops->parse_dt(tc_dev);
	tc_dev->s_data = &s_data;
	mock_s_data_ptr = &s_data;
	return tc_dev->sensor_ops->power_get(tc_dev);
}

void tegracam_device_unregister(struct tegracam_device *tc_dev)
{
	tc_dev->sensor_ops->power_put(tc_dev);
}

int tegracam_v4l2subdev_register(struct tegracam_device *tc_dev, bool is_sensor)
{
	(void)tc_dev;
	(void)is_sensor;
	return 0;
}

void tegracam_v4l2subdev_unregister(struct tegracam_device *tc_dev)
{
	(void)tc_dev;
}

static u16 read16(u16 msb_addr)
{
	return ((u16)mock_regs[msb_addr] << 8) | mock_regs[msb_addr + 1];
}

int main(void)
{
	struct i2c_adapter adap = { .nr = 9 };
	struct device_node np = { .name = "rpicam3_imx708_a@1a" };
	struct i2c_client client = { .addr = 0x1a, .adapter = &adap, .dev = { .of_node = &np } };
	struct camera_common_data *s_data;
	struct imx708 *priv;
	struct tegracam_device *tc_dev;
	const u16 exp_w[3] = { 4608, 2304, 1536 };
	const u16 exp_h[3] = { 2592, 1296, 864 };
	const u16 exp_ll[3] = { 15648, 7824, 5216 };
	int m;

	/* 1. Reject non-IMX708 sensor ID (e.g., IMX477 at same 0x1a addr) */
	memset(mock_regs, 0, sizeof(mock_regs));
	mock_regs[0x0016] = 0x04;
	mock_regs[0x0017] = 0x77;
	assert(imx708_probe(&client, NULL) == -ENODEV);
	assert(mock_gpio_val == 0);
	printf("  PASS: wrong chip ID (0x0477) rejected with -ENODEV\n");

	/* 2. Reject empty/unresponsive second CSI port gracefully */
	mock_i2c_fail = 1;
	assert(imx708_probe(&client, NULL) == -EIO);
	assert(mock_gpio_val == 0);
	mock_i2c_fail = 0;
	printf("  PASS: unresponsive CSI port rejected cleanly with power-off\n");

	/* 3. Probe valid IMX708 (0x0708) */
	mock_regs[0x0016] = 0x07;
	mock_regs[0x0017] = 0x08;
	mock_regs[0x0200] = 0x02;
	mock_regs[0x0201] = 0x00;
	assert(imx708_probe(&client, NULL) == 0);
	assert(mock_gpio_val == 0);
	s_data = mock_s_data_ptr;
	priv = (struct imx708 *)s_data->priv;
	tc_dev = priv->tc_dev;
	assert(priv->fine_integ_time == 0x0200);
	printf("  PASS: valid IMX708 (0x0708) probe succeeded\n");

	s_data->sensor_props.num_modes = 3;
	for (m = 0; m < 3; m++) {
		s_data->sensor_props.sensor_modes[m].control_properties.gain_factor = 16;
		s_data->sensor_props.sensor_modes[m].control_properties.min_gain_val = 18;
		s_data->sensor_props.sensor_modes[m].control_properties.max_gain_val = 256;
		s_data->sensor_props.sensor_modes[m].control_properties.framerate_factor = 1000000;
		s_data->sensor_props.sensor_modes[m].control_properties.exposure_factor = 1000000;
		s_data->sensor_props.sensor_modes[m].signal_properties.pixel_clock.val = 180000000ULL;
	}

	/* 4. Verify all 3 mode register tables, QBC, and PDAF SPC gain LUTs */
	for (m = 0; m < 3; m++) {
		memset(mock_regs, 0, sizeof(mock_regs));
		mock_regs[IMX708_REG_BASE_SPC_GAINS_L] = 0x40;
		s_data->mode = m;
		s_data->mode_prop_idx = m;
		assert(imx708_set_mode(tc_dev) == 0);
		assert(read16(0x034C) == exp_w[m]);
		assert(read16(0x034E) == exp_h[m]);
		assert(read16(0x0342) == exp_ll[m]);
		assert(read16(0x030E) == 0x012C);
		assert(mock_regs[0x0114] == 0x01);
		assert(mock_regs[0x0136] == 0x18 && mock_regs[0x0137] == 0x00);
		assert(mock_regs[IMX708_REG_BASE_SPC_GAINS_L] == 0x4C);
		assert(mock_regs[IMX708_REG_BASE_SPC_GAINS_R] == 0x35);
		if (m == 0) {
			assert(mock_regs[IMX708_LPF_INTENSITY_EN_ADDR] == IMX708_LPF_INTENSITY_ENABLED);
			assert(mock_regs[IMX708_LPF_INTENSITY_ADDR] == 2);
		} else {
			assert(mock_regs[IMX708_LPF_INTENSITY_EN_ADDR] == IMX708_LPF_INTENSITY_DISABLED);
		}
		printf("  PASS: mode%d (%ux%u, line_length=0x%04x, 450MHz link, QBC=%s)\n",
		       m, exp_w[m], exp_h[m], exp_ll[m], m == 0 ? "on" : "off");
	}

	/* 5. Analog gain equation: gain = 1024 / (1024 - code) */
	s_data->mode = 1;
	s_data->mode_prop_idx = 1;
	assert(imx708_set_gain(tc_dev, 0) == -EINVAL);
	assert(imx708_set_gain(tc_dev, 18) == 0);
	assert(read16(0x0204) == 114);
	assert(imx708_set_gain(tc_dev, 32) == 0);
	assert(read16(0x0204) == 512);
	assert(imx708_set_gain(tc_dev, 256) == 0);
	assert(read16(0x0204) == 960);
	assert(imx708_set_gain(tc_dev, 1000) == 0);
	assert(read16(0x0204) == 960);
	printf("  PASS: analog gain math verified\n");

	/* 6. Framerate and long-exposure shift */
	assert(imx708_set_frame_rate(tc_dev, 30000000) == 0);
	assert(read16(0x0340) == 2494 && mock_regs[0x3100] == 0);
	assert(imx708_set_frame_rate(tc_dev, 56000000) == 0);
	assert(read16(0x0340) == 1336 && mock_regs[0x3100] == 0);
	assert(imx708_set_frame_rate(tc_dev, 500000) == 0);
	assert(mock_regs[0x3100] == 2 && read16(0x0340) == 37423);
	assert(imx708_set_frame_rate(tc_dev, 30000000) == 0);
	printf("  PASS: frame_rate & long_exp_shift math verified\n");

	/* 7. Exposure clamping, 48-line margin, and 2-line binning step */
	assert(imx708_set_exposure(tc_dev, 10000) == 0);
	assert(read16(0x0202) >= 746 && read16(0x0202) <= 750 && (read16(0x0202) % 2) == 0);
	assert(imx708_set_exposure(tc_dev, 999999) == 0);
	assert(read16(0x0202) == 2446);
	assert(imx708_set_exposure(tc_dev, 0) == 0);
	assert(read16(0x0202) == 4);
	printf("  PASS: exposure math, 48-line offset, and step alignment verified\n");

	/* 8. Start/stop streaming & group hold */
	assert(imx708_set_group_hold(tc_dev, true) == 0 && mock_regs[0x0104] == 1);
	assert(imx708_set_group_hold(tc_dev, false) == 0 && mock_regs[0x0104] == 0);
	assert(imx708_start_streaming(tc_dev) == 0 && mock_regs[0x0100] == 1);
	assert(imx708_stop_streaming(tc_dev) == 0 && mock_regs[0x0100] == 0);
	assert(imx708_remove(&client) == 0);
	printf("  PASS: streaming, group hold, and remove verified\n");

	return 0;
}
