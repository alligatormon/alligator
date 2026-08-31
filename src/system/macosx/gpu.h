#pragma once

#include <stdint.h>
#include "events/context_arg.h"

#define MACOS_GPU_SNAP_NAME_LEN 96
#define MACOS_GPU_SNAP_CLASS_LEN 64
#define MACOS_GPU_SNAP_INDEX_LEN 16
#define MACOS_GPU_SNAP_COMPAT_LEN 64

enum {
	MACOS_GPU_HAVE_DEVICE_INFO = 1u << 0,
	MACOS_GPU_HAVE_UTIL_DEVICE = 1u << 1,
	MACOS_GPU_HAVE_UTIL_RENDERER = 1u << 2,
	MACOS_GPU_HAVE_UTIL_TILER = 1u << 3,
	MACOS_GPU_HAVE_MEM_ALLOC = 1u << 4,
	MACOS_GPU_HAVE_MEM_IN_USE = 1u << 5,
	MACOS_GPU_HAVE_MEM_IN_USE_DRIVER = 1u << 6,
	MACOS_GPU_HAVE_MEM_DEV_ALLOC = 1u << 7,
	MACOS_GPU_HAVE_MEM_DEV_IN_USE = 1u << 8,
	MACOS_GPU_HAVE_CORE_COUNT = 1u << 9,
	MACOS_GPU_HAVE_RECOVERY = 1u << 10,
};

typedef struct macos_gpu_device_snapshot {
	char name[MACOS_GPU_SNAP_NAME_LEN];
	char class_name[MACOS_GPU_SNAP_CLASS_LEN];
	char index[MACOS_GPU_SNAP_INDEX_LEN];
	char compat[MACOS_GPU_SNAP_COMPAT_LEN];
	uint32_t have;
	double util_device_percent;
	double util_renderer_percent;
	double util_tiler_percent;
	double memory_alloc_bytes;
	double memory_in_use_bytes;
	double memory_in_use_driver_bytes;
	double memory_device_alloc_bytes;
	double memory_device_in_use_bytes;
	double core_count;
	double recovery_count;
} macos_gpu_device_snapshot;

void macos_gpu_emit_globals(context_arg *carg, uint64_t gpu_count);
void macos_gpu_emit_device(context_arg *carg, const macos_gpu_device_snapshot *dev);

#ifdef __APPLE__
void macos_gpu_scrape(void);
#else
static inline void macos_gpu_scrape(void) {}
#endif
