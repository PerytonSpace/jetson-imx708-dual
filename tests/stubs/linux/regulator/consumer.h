#pragma once
#include "../types_stub.h"
struct regulator { int enabled; };
static inline int regulator_enable(struct regulator *r) { r->enabled = 1; return 0; }
static inline int regulator_disable(struct regulator *r) { r->enabled = 0; return 0; }
static inline void devm_regulator_put(struct regulator *r) { (void)r; }
