#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/version.h>

/* Match Linux kernel include/asm-generic/int-ll64.h (u64/s64 are always long long) */
typedef uint8_t            u8;
typedef uint16_t           u16;
typedef uint32_t           u32;
typedef unsigned long long u64;
typedef int8_t             s8;
typedef int16_t            s16;
typedef int32_t            s32;
typedef signed long long   s64;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define likely(x)     __builtin_expect(!!(x), 1)
#define unlikely(x)   __builtin_expect(!!(x), 0)
#define GFP_KERNEL    0
#define THIS_MODULE   NULL
#define IS_ENABLED(x) 1
#define EPROBE_DEFER  517

static inline void *ERR_PTR(long err) { return (void *)(intptr_t)err; }
static inline long PTR_ERR(const void *ptr) { return (long)(intptr_t)ptr; }
static inline bool IS_ERR(const void *ptr) {
	return (unsigned long)(uintptr_t)ptr >= (unsigned long)-4095L;
}

struct device_node { const char *name; };
struct device { struct device_node *of_node; void *drvdata; };
struct i2c_adapter { int nr; };
struct i2c_client {
	unsigned short addr;
	struct device dev;
	struct i2c_adapter *adapter;
	void *drvdata;
};
struct i2c_device_id { char name[20]; unsigned long driver_data; };
struct of_device_id { const char *compatible; };
struct device_driver {
	const char *name;
	void *owner;
	const struct of_device_id *of_match_table;
};

struct i2c_driver {
	struct device_driver driver;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	int (*probe)(struct i2c_client *client);
#else
	int (*probe)(struct i2c_client *client, const struct i2c_device_id *id);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	void (*remove)(struct i2c_client *client);
#else
	int (*remove)(struct i2c_client *client);
#endif
	const struct i2c_device_id *id_table;
};

#define of_match_ptr(x)               (x)
#define MODULE_DEVICE_TABLE(type, n)
#define module_param(name, type, perm)
#define MODULE_PARM_DESC(name, desc)
#define MODULE_DESCRIPTION(x)
#define MODULE_AUTHOR(x)
#define MODULE_LICENSE(x)
#define MODULE_VERSION(x)
#define module_i2c_driver(x) static void * __attribute__((unused)) _unused_drv = &(x)

#define dev_err(dev, fmt, ...)  do { (void)(dev); fprintf(stderr, "ERR: " fmt, ##__VA_ARGS__); } while (0)
#define dev_warn(dev, fmt, ...) do { (void)(dev); fprintf(stderr, "WRN: " fmt, ##__VA_ARGS__); } while (0)
#define dev_info(dev, fmt, ...) do { (void)(dev); if (0) printf(fmt, ##__VA_ARGS__); } while (0)
#define dev_dbg(dev, fmt, ...)  do { (void)(dev); if (0) printf(fmt, ##__VA_ARGS__); } while (0)

static inline size_t strscpy(char *dst, const char *src, size_t count)
{
	size_t len;

	if (!count)
		return 0;
	len = strlen(src);
	if (len >= count)
		len = count - 1;
	memcpy(dst, src, len);
	dst[len] = '\0';
	return len;
}
