#pragma once

#include <stdint.h>
#include "events/context_arg.h"

#define DCGM_SNAP_NAME_LEN 96
#define DCGM_SNAP_UUID_LEN 96
#define DCGM_SNAP_SERIAL_LEN 96
#define DCGM_SNAP_INDEX_LEN 16
#define DCGM_SNAP_PCI_LEN 32

enum {
	DCGM_HAVE_DEVICE_INFO = 1u << 0,
	DCGM_HAVE_GR_ENGINE = 1u << 1,
	DCGM_HAVE_SM_ACTIVE = 1u << 2,
	DCGM_HAVE_SM_OCCUPANCY = 1u << 3,
	DCGM_HAVE_TENSOR = 1u << 4,
	DCGM_HAVE_DRAM = 1u << 5,
	DCGM_HAVE_FP64 = 1u << 6,
	DCGM_HAVE_FP32 = 1u << 7,
	DCGM_HAVE_FP16 = 1u << 8,
	DCGM_HAVE_PCIE_TX = 1u << 9,
	DCGM_HAVE_PCIE_RX = 1u << 10,
	DCGM_HAVE_NVLINK_TX = 1u << 11,
	DCGM_HAVE_NVLINK_RX = 1u << 12,
	DCGM_HAVE_IMMA = 1u << 13,
	DCGM_HAVE_HMMA = 1u << 14,
	DCGM_HAVE_DFMA = 1u << 15,
};

typedef struct dcgm_device_snapshot {
	char name[DCGM_SNAP_NAME_LEN];
	char uuid[DCGM_SNAP_UUID_LEN];
	char serial[DCGM_SNAP_SERIAL_LEN];
	char index[DCGM_SNAP_INDEX_LEN];
	char pci_bus_id[DCGM_SNAP_PCI_LEN];
	uint32_t have;
	double gr_engine_active_ratio;
	double sm_active_ratio;
	double sm_occupancy_ratio;
	double tensor_active_ratio;
	double dram_active_ratio;
	double fp64_active_ratio;
	double fp32_active_ratio;
	double fp16_active_ratio;
	double pcie_tx_bytes;
	double pcie_rx_bytes;
	double nvlink_tx_bytes;
	double nvlink_rx_bytes;
	double tensor_imma_active_ratio;
	double tensor_hmma_active_ratio;
	double tensor_dfma_active_ratio;
} dcgm_device_snapshot;

void dcgm_emit_globals(context_arg *carg, uint64_t gpu_count, int profiling_available);
void dcgm_emit_device(context_arg *carg, const dcgm_device_snapshot *dev);

#ifdef __linux__
void dcgm_schedule_scrape(void);
void dcgm_wait_idle(void);
#else
static inline void dcgm_schedule_scrape(void) {}
static inline void dcgm_wait_idle(void) {}
#endif
