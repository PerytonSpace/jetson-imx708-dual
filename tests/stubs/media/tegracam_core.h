#pragma once
#include <media/camera_common.h>

extern int tegracam_device_register(struct tegracam_device *tc_dev);
extern void tegracam_device_unregister(struct tegracam_device *tc_dev);
extern int tegracam_v4l2subdev_register(struct tegracam_device *tc_dev, bool is_sensor);
extern void tegracam_v4l2subdev_unregister(struct tegracam_device *tc_dev);

static inline void tegracam_set_privdata(struct tegracam_device *tc_dev, void *priv)
{
	tc_dev->priv = priv;
	tc_dev->s_data->priv = priv;
}
static inline void *tegracam_get_privdata(struct tegracam_device *tc_dev)
{
	return tc_dev->priv;
}
