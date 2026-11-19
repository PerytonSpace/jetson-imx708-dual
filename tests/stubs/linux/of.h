#pragma once
#include "types_stub.h"
static inline int of_property_read_string(const struct device_node *np, const char *prop, const char **out) { (void)np; (void)prop; *out = NULL; return -EINVAL; }
static inline bool of_property_read_bool(const struct device_node *np, const char *prop) { (void)np; (void)prop; return false; }
