#pragma once
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#ifndef LINUX_VERSION_CODE
#define LINUX_VERSION_CODE KERNEL_VERSION(5, 15, 148)
#endif
