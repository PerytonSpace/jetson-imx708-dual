#pragma once
#include "types_stub.h"
static inline void *devm_kzalloc(struct device *dev, size_t size, int flags) { (void)dev; (void)flags; return calloc(1, size); }
