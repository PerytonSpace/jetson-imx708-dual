#pragma once
#include <linux/types_stub.h>
#include <linux/regmap.h>
#include <media/tegra_v4l2_camera.h>

enum switch_state { SWITCH_OFF, SWITCH_ON };
struct reg_8 { u16 addr; u8 val; };

struct camera_common_power_rail {
	struct regulator *dvdd, *avdd, *iovdd;
	struct clk *mclk;
	unsigned int reset_gpio;
	bool state;
};
struct camera_common_regulators { const char *avdd, *dvdd, *iovdd; };
struct camera_common_pdata {
	const char *mclk_name, *parentclk_name;
	unsigned int reset_gpio;
	int (*power_on)(struct camera_common_power_rail *pw);
	int (*power_off)(struct camera_common_power_rail *pw);
	struct camera_common_regulators regulators;
	bool has_eeprom;
};
struct camera_common_frmfmt {
	struct v4l2_frmsize_discrete size;
	const int *framerates;
	int num_framerates;
	bool hdr_en;
	int mode;
};
struct sensor_control_properties {
	u32 gain_factor, framerate_factor, exposure_factor;
	u32 min_gain_val, max_gain_val;
};
struct sensor_image_properties { u32 width, height, line_length; };
struct sensor_signal_properties { struct { u64 val; } pixel_clock; };
struct sensor_mode_properties {
	struct sensor_signal_properties signal_properties;
	struct sensor_image_properties image_properties;
	struct sensor_control_properties control_properties;
};
struct sensor_properties {
	int num_modes;
	struct sensor_mode_properties sensor_modes[4];
};
struct tegracam_device;
struct camera_common_data {
	struct device *dev;
	struct camera_common_power_rail *power;
	struct v4l2_subdev subdev;
	struct sensor_properties sensor_props;
	struct regmap *regmap;
	struct camera_common_pdata *pdata;
	void *priv;
	int mode;
	int mode_prop_idx;
};
struct camera_common_sensor_ops {
	u32 numfrmfmts;
	const struct camera_common_frmfmt *frmfmt_table;
	int (*power_on)(struct camera_common_data *s_data);
	int (*power_off)(struct camera_common_data *s_data);
	int (*write_reg)(struct camera_common_data *s_data, u16 addr, u8 val);
	int (*read_reg)(struct camera_common_data *s_data, u16 addr, u8 *val);
	struct camera_common_pdata *(*parse_dt)(struct tegracam_device *tc_dev);
	int (*power_get)(struct tegracam_device *tc_dev);
	int (*power_put)(struct tegracam_device *tc_dev);
	int (*set_mode)(struct tegracam_device *tc_dev);
	int (*start_streaming)(struct tegracam_device *tc_dev);
	int (*stop_streaming)(struct tegracam_device *tc_dev);
};
struct tegracam_ctrl_ops {
	u32 numctrls;
	const u32 *ctrl_cid_list;
	int (*set_gain)(struct tegracam_device *tc_dev, s64 val);
	int (*set_exposure)(struct tegracam_device *tc_dev, s64 val);
	int (*set_frame_rate)(struct tegracam_device *tc_dev, s64 val);
	int (*set_group_hold)(struct tegracam_device *tc_dev, bool val);
};
struct tegracam_device {
	struct i2c_client *client;
	struct device *dev;
	char name[32];
	const struct regmap_config *dev_regmap_config;
	struct camera_common_sensor_ops *sensor_ops;
	const struct v4l2_subdev_internal_ops *v4l2sd_internal_ops;
	struct tegracam_ctrl_ops *tcctrl_ops;
	struct camera_common_data *s_data;
	void *priv;
};
extern int regmap_util_write_table_8(struct regmap *regmap, const struct reg_8 table[],
	const struct reg_8 override_list[], int num_override_regs, u16 wait_ms_addr, u16 end_addr);
extern int camera_common_regulator_get(struct device *dev, struct regulator **vreg, const char *name);
extern struct camera_common_data *to_camera_common_data(const struct device *dev);
