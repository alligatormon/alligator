#pragma once

#include <stdint.h>
#include "events/context_arg.h"

#define NVML_SNAP_NAME_LEN 96
#define NVML_SNAP_UUID_LEN 96
#define NVML_SNAP_SERIAL_LEN 96
#define NVML_SNAP_INDEX_LEN 16
#define NVML_SNAP_PCI_LEN 32
#define NVML_SNAP_BRAND_LEN 32
#define NVML_SNAP_PSTATE_LEN 16
#define NVML_SNAP_PROC_MAX 64
#define NVML_SNAP_PNAME_LEN 96
#define NVML_SNAP_PID_LEN 16
#define NVML_SNAP_PTYPE_LEN 16

enum {
	NVML_HAVE_UTIL_GPU = 1u << 0,
	NVML_HAVE_UTIL_MEM = 1u << 1,
	NVML_HAVE_UTIL_ENC = 1u << 2,
	NVML_HAVE_UTIL_DEC = 1u << 3,
	NVML_HAVE_MEMORY = 1u << 4,
	NVML_HAVE_TEMP_GPU = 1u << 5,
	NVML_HAVE_TEMP_MEM = 1u << 6,
	NVML_HAVE_POWER_USAGE = 1u << 7,
	NVML_HAVE_POWER_LIMIT = 1u << 8,
	NVML_HAVE_POWER_MIN = 1u << 9,
	NVML_HAVE_POWER_MAX = 1u << 10,
	NVML_HAVE_ENERGY = 1u << 11,
	NVML_HAVE_CLOCK_SM = 1u << 12,
	NVML_HAVE_CLOCK_MEM = 1u << 13,
	NVML_HAVE_CLOCK_GRAPHICS = 1u << 14,
	NVML_HAVE_CLOCK_VIDEO = 1u << 15,
	NVML_HAVE_FAN = 1u << 16,
	NVML_HAVE_PCIE_TX = 1u << 17,
	NVML_HAVE_PCIE_RX = 1u << 18,
	NVML_HAVE_PCIE_REPLAY = 1u << 19,
	NVML_HAVE_ECC_CORR = 1u << 20,
	NVML_HAVE_ECC_UNCORR = 1u << 21,
	NVML_HAVE_RETIRED_SBE = 1u << 22,
	NVML_HAVE_RETIRED_DBE = 1u << 23,
	NVML_HAVE_THROTTLE = 1u << 24,
	NVML_HAVE_MIG = 1u << 25,
	NVML_HAVE_PERSISTENCE = 1u << 26,
	NVML_HAVE_PSTATE = 1u << 27,
	NVML_HAVE_DEVICE_INFO = 1u << 28,
};

typedef struct nvml_process_snapshot {
	char pid[NVML_SNAP_PID_LEN];
	char process_name[NVML_SNAP_PNAME_LEN];
	char type[NVML_SNAP_PTYPE_LEN];
	double used_memory_bytes;
} nvml_process_snapshot;

typedef struct nvml_device_snapshot {
	char name[NVML_SNAP_NAME_LEN];
	char uuid[NVML_SNAP_UUID_LEN];
	char serial[NVML_SNAP_SERIAL_LEN];
	char index[NVML_SNAP_INDEX_LEN];
	char pci_bus_id[NVML_SNAP_PCI_LEN];
	char brand[NVML_SNAP_BRAND_LEN];
	char pstate[NVML_SNAP_PSTATE_LEN];
	uint32_t have;
	double util_gpu_percent;
	double util_memory_percent;
	double util_encoder_percent;
	double util_decoder_percent;
	double memory_free_bytes;
	double memory_used_bytes;
	double memory_total_bytes;
	double temperature_gpu_celsius;
	double temperature_memory_celsius;
	double power_usage_watt;
	double power_limit_watt;
	double power_limit_min_watt;
	double power_limit_max_watt;
	double energy_consumption_joules;
	double clocks_sm_mhz;
	double clocks_memory_mhz;
	double clocks_graphics_mhz;
	double clocks_video_mhz;
	double fan_speed_percent;
	double pcie_tx_bytes;
	double pcie_rx_bytes;
	uint64_t pcie_replay_total;
	uint64_t ecc_corrected_total;
	uint64_t ecc_uncorrected_total;
	uint64_t retired_pages_sbe_total;
	uint64_t retired_pages_dbe_total;
	uint64_t clocks_throttle_reasons;
	double mig_mode;
	double persistence_mode;
	int n_procs;
	nvml_process_snapshot procs[NVML_SNAP_PROC_MAX];
} nvml_device_snapshot;

/* Emit Prometheus metrics from a filled snapshot (used by scrape and unit tests). */
void nvml_emit_globals(context_arg *carg, uint64_t gpu_count, const char *driver_version);
void nvml_emit_device(context_arg *carg, const nvml_device_snapshot *dev);

#ifdef __linux__
void nvml_schedule_scrape(void);
void nvml_wait_idle(void);
#else
static inline void nvml_schedule_scrape(void) {}
static inline void nvml_wait_idle(void) {}
#endif
