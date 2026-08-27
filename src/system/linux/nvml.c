#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "main.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "system/linux/nvml.h"

extern aconf *ac;

#define NVML_CLOCKS_THROTTLE_REASON_GPU_IDLE 0x0000000000000001ULL
#define NVML_CLOCKS_THROTTLE_REASON_SW_POWER_CAP 0x0000000000000004ULL
#define NVML_CLOCKS_THROTTLE_REASON_HW_THERMAL_SLOWDOWN 0x0000000000000040ULL
#define NVML_CLOCKS_THROTTLE_REASON_HW_POWER_BRAKE_SLOWDOWN 0x0000000000000080ULL

static void nvml_metric_set(context_arg *carg, const char *metric_name, const char *help)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, help);
}

void nvml_emit_globals(context_arg *carg, uint64_t gpu_count, const char *driver_version)
{
	if (!carg)
		return;

	nvml_metric_set(carg, "nvml_gpu_count", "Number of NVIDIA GPUs visible via NVML.");
	metric_add_auto("nvml_gpu_count", &gpu_count, DATATYPE_UINT, carg);

	if (driver_version && *driver_version) {
		uint64_t one = 1;
		nvml_metric_set(carg, "nvml_driver_version", "NVIDIA driver version reported by NVML.");
		metric_add_labels("nvml_driver_version", &one, DATATYPE_UINT, carg, "version", (char *)driver_version);
	}
}

static void nvml_emit_gauge4(context_arg *carg, const char *name, const char *help,
	double value, const nvml_device_snapshot *dev)
{
	nvml_metric_set(carg, name, help);
	metric_add_labels4((char *)name, &value, DATATYPE_DOUBLE, carg,
		"name", (char *)dev->name, "uuid", (char *)dev->uuid,
		"serial", (char *)dev->serial, "index", (char *)dev->index);
}

static void nvml_emit_uint4(context_arg *carg, const char *name, const char *help,
	uint64_t value, const nvml_device_snapshot *dev)
{
	nvml_metric_set(carg, name, help);
	metric_add_labels4((char *)name, &value, DATATYPE_UINT, carg,
		"name", (char *)dev->name, "uuid", (char *)dev->uuid,
		"serial", (char *)dev->serial, "index", (char *)dev->index);
}

void nvml_emit_device(context_arg *carg, const nvml_device_snapshot *dev)
{
	if (!carg || !dev)
		return;

	uint64_t one = 1;

	if (dev->have & NVML_HAVE_DEVICE_INFO) {
		nvml_metric_set(carg, "nvml_device_info", "NVIDIA GPU identity labels from NVML.");
		metric_add_labels6("nvml_device_info", &one, DATATYPE_UINT, carg,
			"name", (char *)dev->name, "uuid", (char *)dev->uuid,
			"serial", (char *)dev->serial, "index", (char *)dev->index,
			"pci_bus_id", (char *)dev->pci_bus_id, "brand", (char *)dev->brand);
	}

	if (dev->have & NVML_HAVE_UTIL_GPU)
		nvml_emit_gauge4(carg, "nvml_utilization_gpu_percent", "GPU utilization percent.",
			dev->util_gpu_percent, dev);
	if (dev->have & NVML_HAVE_UTIL_MEM)
		nvml_emit_gauge4(carg, "nvml_utilization_memory_percent", "Memory copy engine utilization percent.",
			dev->util_memory_percent, dev);
	if (dev->have & NVML_HAVE_UTIL_ENC)
		nvml_emit_gauge4(carg, "nvml_utilization_encoder_percent", "Encoder utilization percent.",
			dev->util_encoder_percent, dev);
	if (dev->have & NVML_HAVE_UTIL_DEC)
		nvml_emit_gauge4(carg, "nvml_utilization_decoder_percent", "Decoder utilization percent.",
			dev->util_decoder_percent, dev);

	if (dev->have & NVML_HAVE_MEMORY) {
		nvml_emit_gauge4(carg, "nvml_memory_free_bytes", "Framebuffer memory free in bytes.",
			dev->memory_free_bytes, dev);
		nvml_emit_gauge4(carg, "nvml_memory_used_bytes", "Framebuffer memory used in bytes.",
			dev->memory_used_bytes, dev);
		nvml_emit_gauge4(carg, "nvml_memory_total_bytes", "Framebuffer memory total in bytes.",
			dev->memory_total_bytes, dev);
	}

	if (dev->have & NVML_HAVE_TEMP_GPU)
		nvml_emit_gauge4(carg, "nvml_temperature_gpu_celsius", "GPU temperature in Celsius.",
			dev->temperature_gpu_celsius, dev);
	if (dev->have & NVML_HAVE_TEMP_MEM)
		nvml_emit_gauge4(carg, "nvml_temperature_memory_celsius", "Memory temperature in Celsius.",
			dev->temperature_memory_celsius, dev);

	if (dev->have & NVML_HAVE_POWER_USAGE)
		nvml_emit_gauge4(carg, "nvml_power_usage_watt", "Current GPU power draw in watts.",
			dev->power_usage_watt, dev);
	if (dev->have & NVML_HAVE_POWER_LIMIT)
		nvml_emit_gauge4(carg, "nvml_power_limit_watt", "Current power management limit in watts.",
			dev->power_limit_watt, dev);
	if (dev->have & NVML_HAVE_POWER_MIN)
		nvml_emit_gauge4(carg, "nvml_power_limit_min_watt", "Minimum power management limit in watts.",
			dev->power_limit_min_watt, dev);
	if (dev->have & NVML_HAVE_POWER_MAX)
		nvml_emit_gauge4(carg, "nvml_power_limit_max_watt", "Maximum power management limit in watts.",
			dev->power_limit_max_watt, dev);
	if (dev->have & NVML_HAVE_ENERGY)
		nvml_emit_gauge4(carg, "nvml_energy_consumption_joules", "Total energy consumption in joules since driver load.",
			dev->energy_consumption_joules, dev);

	if (dev->have & NVML_HAVE_CLOCK_SM)
		nvml_emit_gauge4(carg, "nvml_clocks_sm_mhz", "SM clock in MHz.", dev->clocks_sm_mhz, dev);
	if (dev->have & NVML_HAVE_CLOCK_MEM)
		nvml_emit_gauge4(carg, "nvml_clocks_memory_mhz", "Memory clock in MHz.", dev->clocks_memory_mhz, dev);
	if (dev->have & NVML_HAVE_CLOCK_GRAPHICS)
		nvml_emit_gauge4(carg, "nvml_clocks_graphics_mhz", "Graphics clock in MHz.", dev->clocks_graphics_mhz, dev);
	if (dev->have & NVML_HAVE_CLOCK_VIDEO)
		nvml_emit_gauge4(carg, "nvml_clocks_video_mhz", "Video clock in MHz.", dev->clocks_video_mhz, dev);

	if (dev->have & NVML_HAVE_FAN)
		nvml_emit_gauge4(carg, "nvml_fan_speed_percent", "Fan speed percent.",
			dev->fan_speed_percent, dev);

	if (dev->have & NVML_HAVE_PCIE_TX)
		nvml_emit_gauge4(carg, "nvml_pcie_tx_bytes", "PCIe TX throughput in bytes per second (last NVML sample interval).",
			dev->pcie_tx_bytes, dev);
	if (dev->have & NVML_HAVE_PCIE_RX)
		nvml_emit_gauge4(carg, "nvml_pcie_rx_bytes", "PCIe RX throughput in bytes per second (last NVML sample interval).",
			dev->pcie_rx_bytes, dev);
	if (dev->have & NVML_HAVE_PCIE_REPLAY)
		nvml_emit_uint4(carg, "nvml_pcie_replay_total", "PCIe replay counter.",
			dev->pcie_replay_total, dev);

	if (dev->have & NVML_HAVE_ECC_CORR)
		nvml_emit_uint4(carg, "nvml_ecc_corrected_total", "Corrected ECC error total (volatile).",
			dev->ecc_corrected_total, dev);
	if (dev->have & NVML_HAVE_ECC_UNCORR)
		nvml_emit_uint4(carg, "nvml_ecc_uncorrected_total", "Uncorrected ECC error total (volatile).",
			dev->ecc_uncorrected_total, dev);
	if (dev->have & NVML_HAVE_RETIRED_SBE)
		nvml_emit_uint4(carg, "nvml_retired_pages_sbe_total", "Retired pages due to single-bit ECC errors.",
			dev->retired_pages_sbe_total, dev);
	if (dev->have & NVML_HAVE_RETIRED_DBE)
		nvml_emit_uint4(carg, "nvml_retired_pages_dbe_total", "Retired pages due to double-bit ECC errors.",
			dev->retired_pages_dbe_total, dev);

	if (dev->have & NVML_HAVE_THROTTLE) {
		nvml_emit_uint4(carg, "nvml_clocks_throttle_reasons", "Current clocks throttle reasons bitmask.",
			dev->clocks_throttle_reasons, dev);
		double idle = (dev->clocks_throttle_reasons & NVML_CLOCKS_THROTTLE_REASON_GPU_IDLE) ? 1.0 : 0.0;
		double sw_power = (dev->clocks_throttle_reasons & NVML_CLOCKS_THROTTLE_REASON_SW_POWER_CAP) ? 1.0 : 0.0;
		double hw_thermal = (dev->clocks_throttle_reasons & NVML_CLOCKS_THROTTLE_REASON_HW_THERMAL_SLOWDOWN) ? 1.0 : 0.0;
		double hw_brake = (dev->clocks_throttle_reasons & NVML_CLOCKS_THROTTLE_REASON_HW_POWER_BRAKE_SLOWDOWN) ? 1.0 : 0.0;
		nvml_emit_gauge4(carg, "nvml_clocks_throttle_gpu_idle", "1 if GPU idle throttle reason is active.", idle, dev);
		nvml_emit_gauge4(carg, "nvml_clocks_throttle_sw_power_cap", "1 if software power cap throttle is active.", sw_power, dev);
		nvml_emit_gauge4(carg, "nvml_clocks_throttle_hw_thermal", "1 if hardware thermal throttle is active.", hw_thermal, dev);
		nvml_emit_gauge4(carg, "nvml_clocks_throttle_hw_power_brake", "1 if hardware power brake throttle is active.", hw_brake, dev);
	}

	if (dev->have & NVML_HAVE_MIG)
		nvml_emit_gauge4(carg, "nvml_mig_mode", "1 if MIG mode is enabled.",
			dev->mig_mode, dev);
	if (dev->have & NVML_HAVE_PERSISTENCE)
		nvml_emit_gauge4(carg, "nvml_persistence_mode", "1 if persistence mode is enabled.",
			dev->persistence_mode, dev);

	if ((dev->have & NVML_HAVE_PSTATE) && dev->pstate[0]) {
		nvml_metric_set(carg, "nvml_pstate", "GPU performance state.");
		metric_add_labels5("nvml_pstate", &one, DATATYPE_UINT, carg,
			"name", (char *)dev->name, "uuid", (char *)dev->uuid,
			"serial", (char *)dev->serial, "index", (char *)dev->index,
			"pstate", (char *)dev->pstate);
	}

	for (int i = 0; i < dev->n_procs; ++i) {
		const nvml_process_snapshot *p = &dev->procs[i];
		double mem = p->used_memory_bytes;
		nvml_metric_set(carg, "nvml_process_used_memory_bytes",
			"GPU memory used by a compute or graphics process in bytes.");
		metric_add_labels7("nvml_process_used_memory_bytes", &mem, DATATYPE_DOUBLE, carg,
			"name", (char *)dev->name, "uuid", (char *)dev->uuid,
			"serial", (char *)dev->serial, "index", (char *)dev->index,
			"pid", (char *)p->pid, "process_name", (char *)p->process_name,
			"type", (char *)p->type);
	}
}

#ifdef __linux__

#include <uv.h>
#include "modules/modules.h"

/* Local NVML ABI stubs — do not include nvml.h / link libnvidia-ml. */
typedef int nvmlReturn_t;
typedef struct nvmlDevice_st *nvmlDevice_t;

#define NVML_SUCCESS 0
#define NVML_ERROR_NOT_SUPPORTED 3
#define NVML_ERROR_INSUFFICIENT_SIZE 7

#define NVML_TEMPERATURE_GPU 0
#define NVML_TEMPERATURE_MEMORY 1

#define NVML_CLOCK_GRAPHICS 0
#define NVML_CLOCK_SM 1
#define NVML_CLOCK_MEM 2
#define NVML_CLOCK_VIDEO 3

#define NVML_PCIE_UTIL_TX_BYTES 0
#define NVML_PCIE_UTIL_RX_BYTES 1

#define NVML_MEMORY_ERROR_TYPE_CORRECTED 0
#define NVML_MEMORY_ERROR_TYPE_UNCORRECTED 1
#define NVML_VOLATILE_ECC 0

#define NVML_PAGE_RETIREMENT_CAUSE_MULTIPLE_SINGLE_BIT_ECC_ERRORS 0
#define NVML_PAGE_RETIREMENT_CAUSE_DOUBLE_BIT_ECC_ERROR 1

#define NVML_FEATURE_DISABLED 0
#define NVML_FEATURE_ENABLED 1

#define NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE 16
#define NVML_DEVICE_PCI_BUS_ID_BUFFER_V2_SIZE 32
#define NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE 80

typedef struct {
	unsigned int gpu;
	unsigned int memory;
} nvmlUtilization_t;

typedef struct {
	unsigned long long total;
	unsigned long long free;
	unsigned long long used;
} nvmlMemory_t;

/* NVML requires version = sizeof(struct) | (2 << 24) before GetMemoryInfo_v2. */
typedef struct {
	unsigned int version;
	unsigned long long total;
	unsigned long long reserved;
	unsigned long long free;
	unsigned long long used;
} nvmlMemory_v2_t;

#define NVML_MEMORY_V2 ((unsigned int)(sizeof(nvmlMemory_v2_t) | (2U << 24)))

typedef struct {
	char busId[NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE];
	unsigned int domain;
	unsigned int bus;
	unsigned int device;
	unsigned int pciDeviceId;
	unsigned int pciSubSystemId;
} nvmlPciInfo_t;

typedef struct {
	char busIdLegacy[NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE];
	unsigned int domain;
	unsigned int bus;
	unsigned int device;
	unsigned int pciDeviceId;
	unsigned int pciSubSystemId;
	char busId[NVML_DEVICE_PCI_BUS_ID_BUFFER_V2_SIZE];
} nvmlPciInfo_v3_t;

typedef struct {
	unsigned int pid;
	unsigned long long usedGpuMemory;
	unsigned int gpuInstanceId;
	unsigned int computeInstanceId;
} nvmlProcessInfo_t;

typedef struct nvml_library {
	uv_lib_t lib;
	int loaded;
	int initialized;

	nvmlReturn_t (*nvmlInit)(void);
	nvmlReturn_t (*nvmlShutdown)(void);
	const char *(*nvmlErrorString)(nvmlReturn_t);
	nvmlReturn_t (*nvmlSystemGetDriverVersion)(char *, unsigned int);
	nvmlReturn_t (*nvmlDeviceGetCount)(unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetHandleByIndex)(unsigned int, nvmlDevice_t *);
	nvmlReturn_t (*nvmlDeviceGetName)(nvmlDevice_t, char *, unsigned int);
	nvmlReturn_t (*nvmlDeviceGetUUID)(nvmlDevice_t, char *, unsigned int);
	nvmlReturn_t (*nvmlDeviceGetSerial)(nvmlDevice_t, char *, unsigned int);
	nvmlReturn_t (*nvmlDeviceGetPciInfo_v3)(nvmlDevice_t, nvmlPciInfo_v3_t *);
	nvmlReturn_t (*nvmlDeviceGetPciInfo)(nvmlDevice_t, nvmlPciInfo_t *);
	nvmlReturn_t (*nvmlDeviceGetBrand)(nvmlDevice_t, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t *);
	nvmlReturn_t (*nvmlDeviceGetMemoryInfo_v2)(nvmlDevice_t, nvmlMemory_v2_t *);
	nvmlReturn_t (*nvmlDeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t *);
	nvmlReturn_t (*nvmlDeviceGetTemperature)(nvmlDevice_t, int, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetPowerUsage)(nvmlDevice_t, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetPowerManagementLimit)(nvmlDevice_t, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetPowerManagementLimitConstraints)(nvmlDevice_t, unsigned int *, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetTotalEnergyConsumption)(nvmlDevice_t, unsigned long long *);
	nvmlReturn_t (*nvmlDeviceGetClockInfo)(nvmlDevice_t, int, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetFanSpeed)(nvmlDevice_t, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetEncoderUtilization)(nvmlDevice_t, unsigned int *, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetDecoderUtilization)(nvmlDevice_t, unsigned int *, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetPcieThroughput)(nvmlDevice_t, int, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetPcieReplayCounter)(nvmlDevice_t, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetTotalEccErrors)(nvmlDevice_t, int, int, unsigned long long *);
	nvmlReturn_t (*nvmlDeviceGetRetiredPages)(nvmlDevice_t, int, unsigned int *, unsigned long long *);
	nvmlReturn_t (*nvmlDeviceGetCurrentClocksThrottleReasons)(nvmlDevice_t, unsigned long long *);
	nvmlReturn_t (*nvmlDeviceGetPerformanceState)(nvmlDevice_t, int *);
	nvmlReturn_t (*nvmlDeviceGetMigMode)(nvmlDevice_t, unsigned int *, unsigned int *);
	nvmlReturn_t (*nvmlDeviceGetPersistenceMode)(nvmlDevice_t, int *);
	nvmlReturn_t (*nvmlDeviceGetComputeRunningProcesses)(nvmlDevice_t, unsigned int *, nvmlProcessInfo_t *);
	nvmlReturn_t (*nvmlDeviceGetGraphicsRunningProcesses)(nvmlDevice_t, unsigned int *, nvmlProcessInfo_t *);
	nvmlReturn_t (*nvmlSystemGetProcessName)(unsigned int, char *, unsigned int);
} nvml_library;

static nvml_library g_nvml;
static uv_work_t nvml_work;
static volatile int nvml_work_pending;

static const char *nvml_brand_name(unsigned int brand)
{
	switch (brand) {
	case 1: return "Quadro";
	case 2: return "Tesla";
	case 3: return "NVS";
	case 4: return "GRID";
	case 5: return "GeForce";
	case 6: return "Titan";
	case 7: return "NVIDIA_vGPU";
	case 8: return "QuadroRTX";
	case 9: return "NVIDIA";
	case 10: return "GeForceRTX";
	case 11: return "TitanRTX";
	default: return "Unknown";
	}
}

static int nvml_dlsym(nvml_library *n, const char *sym, void **out)
{
	int r = uv_dlsym(&n->lib, sym, out);
	if (r) {
		*out = NULL;
		return -1;
	}
	return 0;
}

static int nvml_dlsym_required(nvml_library *n, const char *sym, void **out)
{
	if (nvml_dlsym(n, sym, out)) {
		carglog(ac->system_carg, L_ERROR, "nvml: missing required symbol '%s': %s\n",
			sym, uv_dlerror(&n->lib));
		return -1;
	}
	return 0;
}

static int nvml_load_library(nvml_library *n, const char *path)
{
	memset(n, 0, sizeof(*n));

	if (uv_dlopen(path, &n->lib)) {
		carglog(ac->system_carg, L_ERROR, "nvml: dlopen '%s': %s\n", path, uv_dlerror(&n->lib));
		return -1;
	}

	void *sym;
	if (!nvml_dlsym(n, "nvmlInit_v2", &sym))
		n->nvmlInit = (nvmlReturn_t (*)(void))sym;
	else if (nvml_dlsym_required(n, "nvmlInit", &sym))
		goto fail;
	else
		n->nvmlInit = (nvmlReturn_t (*)(void))sym;

	if (nvml_dlsym_required(n, "nvmlShutdown", &sym))
		goto fail;
	n->nvmlShutdown = (nvmlReturn_t (*)(void))sym;

	if (nvml_dlsym_required(n, "nvmlErrorString", &sym))
		goto fail;
	n->nvmlErrorString = (const char *(*)(nvmlReturn_t))sym;

	if (nvml_dlsym_required(n, "nvmlSystemGetDriverVersion", &sym))
		goto fail;
	n->nvmlSystemGetDriverVersion = (nvmlReturn_t (*)(char *, unsigned int))sym;

	if (!nvml_dlsym(n, "nvmlDeviceGetCount_v2", &sym))
		n->nvmlDeviceGetCount = (nvmlReturn_t (*)(unsigned int *))sym;
	else if (nvml_dlsym_required(n, "nvmlDeviceGetCount", &sym))
		goto fail;
	else
		n->nvmlDeviceGetCount = (nvmlReturn_t (*)(unsigned int *))sym;

	if (!nvml_dlsym(n, "nvmlDeviceGetHandleByIndex_v2", &sym))
		n->nvmlDeviceGetHandleByIndex = (nvmlReturn_t (*)(unsigned int, nvmlDevice_t *))sym;
	else if (nvml_dlsym_required(n, "nvmlDeviceGetHandleByIndex", &sym))
		goto fail;
	else
		n->nvmlDeviceGetHandleByIndex = (nvmlReturn_t (*)(unsigned int, nvmlDevice_t *))sym;

	if (nvml_dlsym_required(n, "nvmlDeviceGetName", &sym))
		goto fail;
	n->nvmlDeviceGetName = (nvmlReturn_t (*)(nvmlDevice_t, char *, unsigned int))sym;

	if (nvml_dlsym_required(n, "nvmlDeviceGetUUID", &sym))
		goto fail;
	n->nvmlDeviceGetUUID = (nvmlReturn_t (*)(nvmlDevice_t, char *, unsigned int))sym;

	if (nvml_dlsym_required(n, "nvmlDeviceGetSerial", &sym))
		goto fail;
	n->nvmlDeviceGetSerial = (nvmlReturn_t (*)(nvmlDevice_t, char *, unsigned int))sym;

	if (!nvml_dlsym(n, "nvmlDeviceGetPciInfo_v3", &sym))
		n->nvmlDeviceGetPciInfo_v3 = (nvmlReturn_t (*)(nvmlDevice_t, nvmlPciInfo_v3_t *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetPciInfo", &sym))
		n->nvmlDeviceGetPciInfo = (nvmlReturn_t (*)(nvmlDevice_t, nvmlPciInfo_t *))sym;

	if (!nvml_dlsym(n, "nvmlDeviceGetBrand", &sym))
		n->nvmlDeviceGetBrand = (nvmlReturn_t (*)(nvmlDevice_t, unsigned int *))sym;

	if (nvml_dlsym_required(n, "nvmlDeviceGetUtilizationRates", &sym))
		goto fail;
	n->nvmlDeviceGetUtilizationRates = (nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t *))sym;

	if (!nvml_dlsym(n, "nvmlDeviceGetMemoryInfo_v2", &sym))
		n->nvmlDeviceGetMemoryInfo_v2 = (nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_v2_t *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetMemoryInfo", &sym))
		n->nvmlDeviceGetMemoryInfo = (nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t *))sym;
	if (!n->nvmlDeviceGetMemoryInfo_v2 && !n->nvmlDeviceGetMemoryInfo) {
		carglog(ac->system_carg, L_ERROR, "nvml: neither nvmlDeviceGetMemoryInfo_v2 nor nvmlDeviceGetMemoryInfo found\n");
		goto fail;
	}

	if (nvml_dlsym_required(n, "nvmlDeviceGetTemperature", &sym))
		goto fail;
	n->nvmlDeviceGetTemperature = (nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int *))sym;

	if (nvml_dlsym_required(n, "nvmlDeviceGetPowerUsage", &sym))
		goto fail;
	n->nvmlDeviceGetPowerUsage = (nvmlReturn_t (*)(nvmlDevice_t, unsigned int *))sym;

	if (!nvml_dlsym(n, "nvmlDeviceGetPowerManagementLimit", &sym))
		n->nvmlDeviceGetPowerManagementLimit = (nvmlReturn_t (*)(nvmlDevice_t, unsigned int *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetPowerManagementLimitConstraints", &sym))
		n->nvmlDeviceGetPowerManagementLimitConstraints =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned int *, unsigned int *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetTotalEnergyConsumption", &sym))
		n->nvmlDeviceGetTotalEnergyConsumption =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned long long *))sym;

	if (nvml_dlsym_required(n, "nvmlDeviceGetClockInfo", &sym))
		goto fail;
	n->nvmlDeviceGetClockInfo = (nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int *))sym;

	if (!nvml_dlsym(n, "nvmlDeviceGetFanSpeed", &sym))
		n->nvmlDeviceGetFanSpeed = (nvmlReturn_t (*)(nvmlDevice_t, unsigned int *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetEncoderUtilization", &sym))
		n->nvmlDeviceGetEncoderUtilization =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned int *, unsigned int *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetDecoderUtilization", &sym))
		n->nvmlDeviceGetDecoderUtilization =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned int *, unsigned int *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetPcieThroughput", &sym))
		n->nvmlDeviceGetPcieThroughput = (nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetPcieReplayCounter", &sym))
		n->nvmlDeviceGetPcieReplayCounter = (nvmlReturn_t (*)(nvmlDevice_t, unsigned int *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetTotalEccErrors", &sym))
		n->nvmlDeviceGetTotalEccErrors =
			(nvmlReturn_t (*)(nvmlDevice_t, int, int, unsigned long long *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetRetiredPages", &sym))
		n->nvmlDeviceGetRetiredPages =
			(nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int *, unsigned long long *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetCurrentClocksThrottleReasons", &sym))
		n->nvmlDeviceGetCurrentClocksThrottleReasons =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned long long *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetPerformanceState", &sym))
		n->nvmlDeviceGetPerformanceState = (nvmlReturn_t (*)(nvmlDevice_t, int *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetMigMode", &sym))
		n->nvmlDeviceGetMigMode =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned int *, unsigned int *))sym;
	if (!nvml_dlsym(n, "nvmlDeviceGetPersistenceMode", &sym))
		n->nvmlDeviceGetPersistenceMode = (nvmlReturn_t (*)(nvmlDevice_t, int *))sym;

	if (!nvml_dlsym(n, "nvmlDeviceGetComputeRunningProcesses_v3", &sym))
		n->nvmlDeviceGetComputeRunningProcesses =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned int *, nvmlProcessInfo_t *))sym;
	else if (!nvml_dlsym(n, "nvmlDeviceGetComputeRunningProcesses", &sym))
		n->nvmlDeviceGetComputeRunningProcesses =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned int *, nvmlProcessInfo_t *))sym;

	if (!nvml_dlsym(n, "nvmlDeviceGetGraphicsRunningProcesses_v3", &sym))
		n->nvmlDeviceGetGraphicsRunningProcesses =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned int *, nvmlProcessInfo_t *))sym;
	else if (!nvml_dlsym(n, "nvmlDeviceGetGraphicsRunningProcesses", &sym))
		n->nvmlDeviceGetGraphicsRunningProcesses =
			(nvmlReturn_t (*)(nvmlDevice_t, unsigned int *, nvmlProcessInfo_t *))sym;

	if (!nvml_dlsym(n, "nvmlSystemGetProcessName", &sym))
		n->nvmlSystemGetProcessName = (nvmlReturn_t (*)(unsigned int, char *, unsigned int))sym;

	n->loaded = 1;
	return 0;

fail:
	uv_dlclose(&n->lib);
	memset(n, 0, sizeof(*n));
	return -1;
}

static const char *nvml_err(nvml_library *n, nvmlReturn_t rc)
{
	if (n->nvmlErrorString)
		return n->nvmlErrorString(rc);
	return "unknown";
}

static void nvml_fill_processes(nvml_library *n, nvmlDevice_t dev, nvml_device_snapshot *snap,
	nvmlReturn_t (*getter)(nvmlDevice_t, unsigned int *, nvmlProcessInfo_t *), const char *type)
{
	if (!getter)
		return;

	unsigned int count = 0;
	nvmlReturn_t rc = getter(dev, &count, NULL);
	if (rc == NVML_SUCCESS && count == 0)
		return;
	if (rc != NVML_ERROR_INSUFFICIENT_SIZE && rc != NVML_SUCCESS)
		return;
	if (count == 0)
		return;
	if (count > NVML_SNAP_PROC_MAX)
		count = NVML_SNAP_PROC_MAX;

	nvmlProcessInfo_t *infos = calloc(count, sizeof(*infos));
	if (!infos)
		return;

	unsigned int got = count;
	rc = getter(dev, &got, infos);
	if (rc != NVML_SUCCESS) {
		free(infos);
		return;
	}

	for (unsigned int i = 0; i < got && snap->n_procs < NVML_SNAP_PROC_MAX; ++i) {
		nvml_process_snapshot *p = &snap->procs[snap->n_procs++];
		snprintf(p->pid, sizeof(p->pid), "%u", infos[i].pid);
		strlcpy(p->type, type, sizeof(p->type));
		p->used_memory_bytes = (double)infos[i].usedGpuMemory;
		strlcpy(p->process_name, "unknown", sizeof(p->process_name));
		if (n->nvmlSystemGetProcessName) {
			char pname[NVML_SNAP_PNAME_LEN];
			if (n->nvmlSystemGetProcessName(infos[i].pid, pname, sizeof(pname)) == NVML_SUCCESS)
				strlcpy(p->process_name, pname, sizeof(p->process_name));
		}
	}
	free(infos);
}

static void nvml_fill_device(nvml_library *n, nvmlDevice_t dev, unsigned int index, nvml_device_snapshot *snap)
{
	memset(snap, 0, sizeof(*snap));
	snprintf(snap->index, sizeof(snap->index), "%u", index);
	strlcpy(snap->name, "unknown", sizeof(snap->name));
	strlcpy(snap->uuid, "unknown", sizeof(snap->uuid));
	strlcpy(snap->serial, "unknown", sizeof(snap->serial));
	strlcpy(snap->pci_bus_id, "unknown", sizeof(snap->pci_bus_id));
	strlcpy(snap->brand, "Unknown", sizeof(snap->brand));

	char buf[128];
	if (n->nvmlDeviceGetName(dev, buf, sizeof(buf)) == NVML_SUCCESS)
		strlcpy(snap->name, buf, sizeof(snap->name));
	if (n->nvmlDeviceGetUUID(dev, buf, sizeof(buf)) == NVML_SUCCESS)
		strlcpy(snap->uuid, buf, sizeof(snap->uuid));
	if (n->nvmlDeviceGetSerial(dev, buf, sizeof(buf)) == NVML_SUCCESS)
		strlcpy(snap->serial, buf, sizeof(snap->serial));

	if (n->nvmlDeviceGetPciInfo_v3) {
		nvmlPciInfo_v3_t pci;
		memset(&pci, 0, sizeof(pci));
		if (n->nvmlDeviceGetPciInfo_v3(dev, &pci) == NVML_SUCCESS)
			strlcpy(snap->pci_bus_id, pci.busId[0] ? pci.busId : pci.busIdLegacy, sizeof(snap->pci_bus_id));
	} else if (n->nvmlDeviceGetPciInfo) {
		nvmlPciInfo_t pci;
		memset(&pci, 0, sizeof(pci));
		if (n->nvmlDeviceGetPciInfo(dev, &pci) == NVML_SUCCESS)
			strlcpy(snap->pci_bus_id, pci.busId, sizeof(snap->pci_bus_id));
	}

	if (n->nvmlDeviceGetBrand) {
		unsigned int brand = 0;
		if (n->nvmlDeviceGetBrand(dev, &brand) == NVML_SUCCESS)
			strlcpy(snap->brand, nvml_brand_name(brand), sizeof(snap->brand));
	}
	snap->have |= NVML_HAVE_DEVICE_INFO;

	nvmlUtilization_t util = {0};
	if (n->nvmlDeviceGetUtilizationRates(dev, &util) == NVML_SUCCESS) {
		snap->util_gpu_percent = util.gpu;
		snap->util_memory_percent = util.memory;
		snap->have |= NVML_HAVE_UTIL_GPU | NVML_HAVE_UTIL_MEM;
	}

	{
		int got_memory = 0;
		if (n->nvmlDeviceGetMemoryInfo_v2) {
			nvmlMemory_v2_t mem = {0};
			mem.version = NVML_MEMORY_V2;
			nvmlReturn_t mrc = n->nvmlDeviceGetMemoryInfo_v2(dev, &mem);
			if (mrc == NVML_SUCCESS) {
				snap->memory_free_bytes = (double)mem.free;
				snap->memory_used_bytes = (double)mem.used;
				snap->memory_total_bytes = (double)mem.total;
				snap->have |= NVML_HAVE_MEMORY;
				got_memory = 1;
			} else {
				carglog(ac->system_carg, L_DEBUG,
					"nvml: GetMemoryInfo_v2 failed for index %s: %s, trying v1\n",
					snap->index, nvml_err(n, mrc));
			}
		}
		if (!got_memory && n->nvmlDeviceGetMemoryInfo) {
			nvmlMemory_t mem = {0};
			nvmlReturn_t mrc = n->nvmlDeviceGetMemoryInfo(dev, &mem);
			if (mrc == NVML_SUCCESS) {
				snap->memory_free_bytes = (double)mem.free;
				snap->memory_used_bytes = (double)mem.used;
				snap->memory_total_bytes = (double)mem.total;
				snap->have |= NVML_HAVE_MEMORY;
			} else {
				carglog(ac->system_carg, L_DEBUG,
					"nvml: GetMemoryInfo failed for index %s: %s\n",
					snap->index, nvml_err(n, mrc));
			}
		}
	}

	unsigned int temp = 0;
	if (n->nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
		snap->temperature_gpu_celsius = temp;
		snap->have |= NVML_HAVE_TEMP_GPU;
	}
	if (n->nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_MEMORY, &temp) == NVML_SUCCESS) {
		snap->temperature_memory_celsius = temp;
		snap->have |= NVML_HAVE_TEMP_MEM;
	}

	unsigned int mw = 0;
	if (n->nvmlDeviceGetPowerUsage(dev, &mw) == NVML_SUCCESS) {
		snap->power_usage_watt = mw / 1000.0;
		snap->have |= NVML_HAVE_POWER_USAGE;
	}
	if (n->nvmlDeviceGetPowerManagementLimit &&
	    n->nvmlDeviceGetPowerManagementLimit(dev, &mw) == NVML_SUCCESS) {
		snap->power_limit_watt = mw / 1000.0;
		snap->have |= NVML_HAVE_POWER_LIMIT;
	}
	if (n->nvmlDeviceGetPowerManagementLimitConstraints) {
		unsigned int pmin = 0, pmax = 0;
		if (n->nvmlDeviceGetPowerManagementLimitConstraints(dev, &pmin, &pmax) == NVML_SUCCESS) {
			snap->power_limit_min_watt = pmin / 1000.0;
			snap->power_limit_max_watt = pmax / 1000.0;
			snap->have |= NVML_HAVE_POWER_MIN | NVML_HAVE_POWER_MAX;
		}
	}
	if (n->nvmlDeviceGetTotalEnergyConsumption) {
		unsigned long long mj = 0;
		if (n->nvmlDeviceGetTotalEnergyConsumption(dev, &mj) == NVML_SUCCESS) {
			snap->energy_consumption_joules = mj / 1000.0;
			snap->have |= NVML_HAVE_ENERGY;
		}
	}

	unsigned int clock = 0;
	if (n->nvmlDeviceGetClockInfo(dev, NVML_CLOCK_SM, &clock) == NVML_SUCCESS) {
		snap->clocks_sm_mhz = clock;
		snap->have |= NVML_HAVE_CLOCK_SM;
	}
	if (n->nvmlDeviceGetClockInfo(dev, NVML_CLOCK_MEM, &clock) == NVML_SUCCESS) {
		snap->clocks_memory_mhz = clock;
		snap->have |= NVML_HAVE_CLOCK_MEM;
	}
	if (n->nvmlDeviceGetClockInfo(dev, NVML_CLOCK_GRAPHICS, &clock) == NVML_SUCCESS) {
		snap->clocks_graphics_mhz = clock;
		snap->have |= NVML_HAVE_CLOCK_GRAPHICS;
	}
	if (n->nvmlDeviceGetClockInfo(dev, NVML_CLOCK_VIDEO, &clock) == NVML_SUCCESS) {
		snap->clocks_video_mhz = clock;
		snap->have |= NVML_HAVE_CLOCK_VIDEO;
	}

	if (n->nvmlDeviceGetFanSpeed) {
		unsigned int fan = 0;
		if (n->nvmlDeviceGetFanSpeed(dev, &fan) == NVML_SUCCESS) {
			snap->fan_speed_percent = fan;
			snap->have |= NVML_HAVE_FAN;
		}
	}

	if (n->nvmlDeviceGetEncoderUtilization) {
		unsigned int util_pct = 0, period = 0;
		if (n->nvmlDeviceGetEncoderUtilization(dev, &util_pct, &period) == NVML_SUCCESS) {
			snap->util_encoder_percent = util_pct;
			snap->have |= NVML_HAVE_UTIL_ENC;
		}
	}
	if (n->nvmlDeviceGetDecoderUtilization) {
		unsigned int util_pct = 0, period = 0;
		if (n->nvmlDeviceGetDecoderUtilization(dev, &util_pct, &period) == NVML_SUCCESS) {
			snap->util_decoder_percent = util_pct;
			snap->have |= NVML_HAVE_UTIL_DEC;
		}
	}

	if (n->nvmlDeviceGetPcieThroughput) {
		unsigned int kbps = 0;
		if (n->nvmlDeviceGetPcieThroughput(dev, NVML_PCIE_UTIL_TX_BYTES, &kbps) == NVML_SUCCESS) {
			snap->pcie_tx_bytes = (double)kbps * 1024.0;
			snap->have |= NVML_HAVE_PCIE_TX;
		}
		if (n->nvmlDeviceGetPcieThroughput(dev, NVML_PCIE_UTIL_RX_BYTES, &kbps) == NVML_SUCCESS) {
			snap->pcie_rx_bytes = (double)kbps * 1024.0;
			snap->have |= NVML_HAVE_PCIE_RX;
		}
	}
	if (n->nvmlDeviceGetPcieReplayCounter) {
		unsigned int replay = 0;
		if (n->nvmlDeviceGetPcieReplayCounter(dev, &replay) == NVML_SUCCESS) {
			snap->pcie_replay_total = replay;
			snap->have |= NVML_HAVE_PCIE_REPLAY;
		}
	}

	if (n->nvmlDeviceGetTotalEccErrors) {
		unsigned long long ecc = 0;
		if (n->nvmlDeviceGetTotalEccErrors(dev, NVML_MEMORY_ERROR_TYPE_CORRECTED, NVML_VOLATILE_ECC, &ecc) == NVML_SUCCESS) {
			snap->ecc_corrected_total = ecc;
			snap->have |= NVML_HAVE_ECC_CORR;
		}
		if (n->nvmlDeviceGetTotalEccErrors(dev, NVML_MEMORY_ERROR_TYPE_UNCORRECTED, NVML_VOLATILE_ECC, &ecc) == NVML_SUCCESS) {
			snap->ecc_uncorrected_total = ecc;
			snap->have |= NVML_HAVE_ECC_UNCORR;
		}
	}

	if (n->nvmlDeviceGetRetiredPages) {
		unsigned int count = 0;
		nvmlReturn_t rc = n->nvmlDeviceGetRetiredPages(dev,
			NVML_PAGE_RETIREMENT_CAUSE_MULTIPLE_SINGLE_BIT_ECC_ERRORS, &count, NULL);
		if (rc == NVML_SUCCESS || rc == NVML_ERROR_INSUFFICIENT_SIZE) {
			snap->retired_pages_sbe_total = count;
			snap->have |= NVML_HAVE_RETIRED_SBE;
		}
		count = 0;
		rc = n->nvmlDeviceGetRetiredPages(dev,
			NVML_PAGE_RETIREMENT_CAUSE_DOUBLE_BIT_ECC_ERROR, &count, NULL);
		if (rc == NVML_SUCCESS || rc == NVML_ERROR_INSUFFICIENT_SIZE) {
			snap->retired_pages_dbe_total = count;
			snap->have |= NVML_HAVE_RETIRED_DBE;
		}
	}

	if (n->nvmlDeviceGetCurrentClocksThrottleReasons) {
		unsigned long long reasons = 0;
		if (n->nvmlDeviceGetCurrentClocksThrottleReasons(dev, &reasons) == NVML_SUCCESS) {
			snap->clocks_throttle_reasons = reasons;
			snap->have |= NVML_HAVE_THROTTLE;
		}
	}

	if (n->nvmlDeviceGetPerformanceState) {
		int pstate = 0;
		if (n->nvmlDeviceGetPerformanceState(dev, &pstate) == NVML_SUCCESS) {
			snprintf(snap->pstate, sizeof(snap->pstate), "P%d", pstate);
			snap->have |= NVML_HAVE_PSTATE;
		}
	}

	if (n->nvmlDeviceGetMigMode) {
		unsigned int current = 0, pending = 0;
		if (n->nvmlDeviceGetMigMode(dev, &current, &pending) == NVML_SUCCESS) {
			snap->mig_mode = (current == NVML_FEATURE_ENABLED) ? 1.0 : 0.0;
			snap->have |= NVML_HAVE_MIG;
		}
	}
	if (n->nvmlDeviceGetPersistenceMode) {
		int mode = 0;
		if (n->nvmlDeviceGetPersistenceMode(dev, &mode) == NVML_SUCCESS) {
			snap->persistence_mode = (mode == NVML_FEATURE_ENABLED) ? 1.0 : 0.0;
			snap->have |= NVML_HAVE_PERSISTENCE;
		}
	}

	nvml_fill_processes(n, dev, snap, n->nvmlDeviceGetComputeRunningProcesses, "compute");
	nvml_fill_processes(n, dev, snap, n->nvmlDeviceGetGraphicsRunningProcesses, "graphics");
}

static int nvml_ensure_ready(nvml_library *n)
{
	if (n->initialized)
		return 0;

	module_t *mod = alligator_ht_search(ac->modules, module_compare, "nvml", tommy_strhash_u32(0, "nvml"));
	if (!mod || !mod->path || !*mod->path) {
		carglog(ac->system_carg, L_INFO, "nvml: modules.nvml is not configured, skipping scrape\n");
		return -1;
	}

	if (nvml_load_library(n, mod->path))
		return -1;

	nvmlReturn_t rc = n->nvmlInit();
	if (rc != NVML_SUCCESS) {
		carglog(ac->system_carg, L_ERROR, "nvml: nvmlInit failed: %s\n", nvml_err(n, rc));
		uv_dlclose(&n->lib);
		memset(n, 0, sizeof(*n));
		return -1;
	}

	n->initialized = 1;
	carglog(ac->system_carg, L_INFO, "nvml: loaded library %s\n", mod->path);
	return 0;
}

static void nvml_scrape_sync(void)
{
	nvml_library *n = &g_nvml;
	if (nvml_ensure_ready(n))
		return;

	unsigned int count = 0;
	nvmlReturn_t rc = n->nvmlDeviceGetCount(&count);
	if (rc != NVML_SUCCESS) {
		carglog(ac->system_carg, L_ERROR, "nvml: DeviceGetCount failed: %s\n", nvml_err(n, rc));
		return;
	}

	char driver[NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE] = {0};
	n->nvmlSystemGetDriverVersion(driver, sizeof(driver));

	nvml_emit_globals(ac->system_carg, count, driver);

	for (unsigned int i = 0; i < count; ++i) {
		nvmlDevice_t dev = NULL;
		rc = n->nvmlDeviceGetHandleByIndex(i, &dev);
		if (rc != NVML_SUCCESS || !dev) {
			carglog(ac->system_carg, L_ERROR, "nvml: GetHandleByIndex(%u) failed: %s\n",
				i, nvml_err(n, rc));
			continue;
		}
		nvml_device_snapshot snap;
		nvml_fill_device(n, dev, i, &snap);
		nvml_emit_device(ac->system_carg, &snap);
	}
}

static void nvml_work_cb(uv_work_t *req)
{
	(void)req;
	nvml_scrape_sync();
}

static void nvml_after_work_cb(uv_work_t *req, int status)
{
	(void)req;
	(void)status;
	nvml_work_pending = 0;
}

void nvml_wait_idle(void)
{
	uv_loop_t *loop = uv_default_loop();
	if (!loop)
		return;
	while (nvml_work_pending)
		uv_run(loop, UV_RUN_ONCE);
}

void nvml_schedule_scrape(void)
{
	uv_loop_t *loop = uv_default_loop();
	if (!loop)
		return;
	if (nvml_work_pending)
		return;
	nvml_work_pending = 1;
	if (uv_queue_work(loop, &nvml_work, nvml_work_cb, nvml_after_work_cb)) {
		nvml_work_pending = 0;
		carglog(ac->system_carg, L_ERROR, "nvml: uv_queue_work failed\n");
	}
}

#endif /* __linux__ */
