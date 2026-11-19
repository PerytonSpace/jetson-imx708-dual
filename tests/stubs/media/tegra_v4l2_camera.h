#pragma once
#include <linux/types_stub.h>

#define TEGRA_CAMERA_CID_GAIN            1
#define TEGRA_CAMERA_CID_EXPOSURE        2
#define TEGRA_CAMERA_CID_FRAME_RATE      3
#define TEGRA_CAMERA_CID_SENSOR_MODE_ID  4

struct v4l2_frmsize_discrete { u32 width; u32 height; };
struct v4l2_subdev { void *priv; };
struct v4l2_subdev_fh { int dummy; };
struct v4l2_subdev_internal_ops {
	int (*open)(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh);
};
static inline void *v4l2_get_subdevdata(const struct v4l2_subdev *sd) { return sd->priv; }
