#pragma once
#include "types_stub.h"
struct clk { int dummy; };
static inline struct clk *devm_clk_get(struct device *dev, const char *id) { (void)dev; (void)id; return NULL; }
static inline int clk_set_parent(struct clk *clk, struct clk *parent) { (void)clk; (void)parent; return 0; }
