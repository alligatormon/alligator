#pragma once

#include <stdint.h>
#include "events/context_arg.h"

#define AMDGPU_SNAP_NAME_LEN 96
#define AMDGPU_SNAP_INDEX_LEN 16
#define AMDGPU_SNAP_PCI_LEN 32
#define AMDGPU_SNAP_UID_LEN 48
#define AMDGPU_SNAP_VBIOS_LEN 64
#define AMDGPU_SNAP_SENSOR_LEN 32
#define AMDGPU_SNAP_TEMP_MAX 8

enum {
	AMDGPU_HAVE_DEVICE_INFO = 1u << 0,
	AMDGPU_HAVE_UTIL_GPU = 1u << 1,
	AMDGPU_HAVE_UTIL_MEM = 1u << 2,
	AMDGPU_HAVE_VRAM = 1u << 3,
	AMDGPU_HAVE_GTT = 1u << 4,
	AMDGPU_HAVE_VIS_VRAM = 1u << 5,
	AMDGPU_HAVE_CLOCK_SCLK = 1u << 6,
	AMDGPU_HAVE_CLOCK_MCLK = 1u << 7,
	AMDGPU_HAVE_POWER_AVG = 1u << 8,
	AMDGPU_HAVE_POWER_CAP = 1u << 9,
	AMDGPU_HAVE_FAN = 1u << 10,
};

typedef struct amdgpu_temp_snapshot {
	char sensor[AMDGPU_SNAP_SENSOR_LEN];
	double celsius;
} amdgpu_temp_snapshot;

typedef struct amdgpu_device_snapshot {
	char name[AMDGPU_SNAP_NAME_LEN];
	char index[AMDGPU_SNAP_INDEX_LEN];
	char pci[AMDGPU_SNAP_PCI_LEN];
	char unique_id[AMDGPU_SNAP_UID_LEN];
	char vbios[AMDGPU_SNAP_VBIOS_LEN];
	uint32_t have;
	double util_gpu_percent;
	double util_memory_percent;
	double vram_total_bytes;
	double vram_used_bytes;
	double vram_free_bytes;
	double gtt_total_bytes;
	double gtt_used_bytes;
	double vis_vram_total_bytes;
	double vis_vram_used_bytes;
	double clocks_sclk_mhz;
	double clocks_mclk_mhz;
	double power_average_watt;
	double power_cap_watt;
	double fan_speed_rpm;
	int n_temps;
	amdgpu_temp_snapshot temps[AMDGPU_SNAP_TEMP_MAX];
} amdgpu_device_snapshot;

void amdgpu_emit_globals(context_arg *carg, uint64_t gpu_count);
void amdgpu_emit_device(context_arg *carg, const amdgpu_device_snapshot *dev);
void amdgpu_scrape(void);
