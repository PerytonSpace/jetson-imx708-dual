#pragma once
#include "../types_stub.h"
struct gpio_desc { int val; };
enum gpiod_flags { GPIOD_OUT_LOW = 0 };
extern struct gpio_desc *devm_gpiod_get_optional(struct device *dev, const char *con_id, enum gpiod_flags flags);
extern void gpiod_set_value_cansleep(struct gpio_desc *desc, int value);
