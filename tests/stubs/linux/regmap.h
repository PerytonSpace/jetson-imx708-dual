#pragma once
#include "types_stub.h"
enum regcache_type { REGCACHE_NONE, REGCACHE_RBTREE };
struct regmap { int dummy; };
struct regmap_config {
	int reg_bits;
	int val_bits;
	enum regcache_type cache_type;
	bool use_single_rw;
	bool use_single_read;
	bool use_single_write;
};
extern int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);
extern int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);
