#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "main.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "system/linux/dcgm.h"

extern aconf *ac;

static void dcgm_metric_set(context_arg *carg, const char *metric_name, const char *help)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, help);
}

void dcgm_emit_globals(context_arg *carg, uint64_t gpu_count, int profiling_available)
{
	if (!carg)
		return;
	dcgm_metric_set(carg, "dcgm_gpu_count", "Number of DCGM-supported GPUs.");
	metric_add_auto("dcgm_gpu_count", &gpu_count, DATATYPE_UINT, carg);

	uint64_t prof = profiling_available ? 1 : 0;
	dcgm_metric_set(carg, "dcgm_profiling_available",
		"1 if DCGM profiling (PROF_*) watches are active, 0 if unsupported on this host.");
	metric_add_auto("dcgm_profiling_available", &prof, DATATYPE_UINT, carg);
}

static void dcgm_emit_gauge4(context_arg *carg, const char *name, const char *help,
	double value, const dcgm_device_snapshot *dev)
{
	dcgm_metric_set(carg, name, help);
	metric_add_labels4((char *)name, &value, DATATYPE_DOUBLE, carg,
		"name", (char *)dev->name, "uuid", (char *)dev->uuid,
		"serial", (char *)dev->serial, "index", (char *)dev->index);
}

void dcgm_emit_device(context_arg *carg, const dcgm_device_snapshot *dev)
{
	if (!carg || !dev)
		return;

	uint64_t one = 1;

	if (dev->have & DCGM_HAVE_DEVICE_INFO) {
		dcgm_metric_set(carg, "dcgm_device_info", "GPU identity labels from DCGM.");
		metric_add_labels5("dcgm_device_info", &one, DATATYPE_UINT, carg,
			"name", (char *)dev->name, "uuid", (char *)dev->uuid,
			"serial", (char *)dev->serial, "index", (char *)dev->index,
			"pci_bus_id", (char *)dev->pci_bus_id);
	}

	if (dev->have & DCGM_HAVE_GR_ENGINE)
		dcgm_emit_gauge4(carg, "dcgm_gr_engine_active_ratio",
			"Ratio of time the graphics/compute engine is active.",
			dev->gr_engine_active_ratio, dev);
	if (dev->have & DCGM_HAVE_SM_ACTIVE)
		dcgm_emit_gauge4(carg, "dcgm_sm_active_ratio",
			"Ratio of cycles an SM has at least one warp assigned.",
			dev->sm_active_ratio, dev);
	if (dev->have & DCGM_HAVE_SM_OCCUPANCY)
		dcgm_emit_gauge4(carg, "dcgm_sm_occupancy_ratio",
			"Ratio of warps resident on an SM vs theoretical maximum.",
			dev->sm_occupancy_ratio, dev);
	if (dev->have & DCGM_HAVE_TENSOR)
		dcgm_emit_gauge4(carg, "dcgm_tensor_active_ratio",
			"Ratio of cycles any tensor pipe is active.",
			dev->tensor_active_ratio, dev);
	if (dev->have & DCGM_HAVE_DRAM)
		dcgm_emit_gauge4(carg, "dcgm_dram_active_ratio",
			"Ratio of cycles the device memory interface is active.",
			dev->dram_active_ratio, dev);
	if (dev->have & DCGM_HAVE_FP64)
		dcgm_emit_gauge4(carg, "dcgm_fp64_active_ratio",
			"Ratio of cycles the FP64 pipe is active.",
			dev->fp64_active_ratio, dev);
	if (dev->have & DCGM_HAVE_FP32)
		dcgm_emit_gauge4(carg, "dcgm_fp32_active_ratio",
			"Ratio of cycles the FP32 pipe is active.",
			dev->fp32_active_ratio, dev);
	if (dev->have & DCGM_HAVE_FP16)
		dcgm_emit_gauge4(carg, "dcgm_fp16_active_ratio",
			"Ratio of cycles the FP16 pipe is active (excludes HMMA).",
			dev->fp16_active_ratio, dev);
	if (dev->have & DCGM_HAVE_PCIE_TX)
		dcgm_emit_gauge4(carg, "dcgm_pcie_tx_bytes",
			"PCIe transmit bytes during last profiling sample interval.",
			dev->pcie_tx_bytes, dev);
	if (dev->have & DCGM_HAVE_PCIE_RX)
		dcgm_emit_gauge4(carg, "dcgm_pcie_rx_bytes",
			"PCIe receive bytes during last profiling sample interval.",
			dev->pcie_rx_bytes, dev);
	if (dev->have & DCGM_HAVE_NVLINK_TX)
		dcgm_emit_gauge4(carg, "dcgm_nvlink_tx_bytes",
			"NVLink transmit bytes during last profiling sample interval.",
			dev->nvlink_tx_bytes, dev);
	if (dev->have & DCGM_HAVE_NVLINK_RX)
		dcgm_emit_gauge4(carg, "dcgm_nvlink_rx_bytes",
			"NVLink receive bytes during last profiling sample interval.",
			dev->nvlink_rx_bytes, dev);
	if (dev->have & DCGM_HAVE_IMMA)
		dcgm_emit_gauge4(carg, "dcgm_tensor_imma_active_ratio",
			"Ratio of cycles the tensor IMMA pipe is active.",
			dev->tensor_imma_active_ratio, dev);
	if (dev->have & DCGM_HAVE_HMMA)
		dcgm_emit_gauge4(carg, "dcgm_tensor_hmma_active_ratio",
			"Ratio of cycles the tensor HMMA pipe is active.",
			dev->tensor_hmma_active_ratio, dev);
	if (dev->have & DCGM_HAVE_DFMA)
		dcgm_emit_gauge4(carg, "dcgm_tensor_dfma_active_ratio",
			"Ratio of cycles the tensor DFMA pipe is active.",
			dev->tensor_dfma_active_ratio, dev);
}

#ifdef __linux__

#include <uv.h>
#include "modules/modules.h"

/* Local DCGM ABI stubs — do not include DCGM headers / link -ldcgm. */
typedef int dcgmReturn_t;
typedef uintptr_t dcgmHandle_t;
typedef uintptr_t dcgmGpuGrp_t;
typedef uintptr_t dcgmFieldGrp_t;
typedef unsigned int dcgm_field_eid_t;

#define DCGM_ST_OK 0
#define DCGM_MAX_NUM_DEVICES 32
#define DCGM_MAX_STR_LENGTH 256
#define DCGM_MAX_BLOB_LENGTH 4096
#define DCGM_GROUP_ALL_GPUS ((dcgmGpuGrp_t)0x7fffffff)
#define DCGM_OPERATION_MODE_MANUAL 2
#define DCGM_GROUP_DEFAULT 0

#define DCGM_FT_DOUBLE ((unsigned short)'d')
#define DCGM_FT_INT64 ((unsigned short)'i')
#define DCGM_FT_STRING ((unsigned short)'s')

#define DCGM_FP64_BLANK 140737488355328.0
#define DCGM_INT64_BLANK 0x7ffffffffffffff0LL
#define DCGM_STR_BLANK "<<<NULL>>>"

#define DCGM_FI_DEV_GPU_NAME 50
#define DCGM_FI_DEV_BOARD_SERIAL 53
#define DCGM_FI_DEV_GPU_UUID 54
#define DCGM_FI_DEV_PCI_BUS_ID 57

#define DCGM_FI_PROF_GR_ENGINE_ACTIVE 1001
#define DCGM_FI_PROF_SM_ACTIVE 1002
#define DCGM_FI_PROF_SM_OCCUPANCY 1003
#define DCGM_FI_PROF_PIPE_TENSOR_ACTIVE 1004
#define DCGM_FI_PROF_DRAM_ACTIVE 1005
#define DCGM_FI_PROF_PIPE_FP64_ACTIVE 1006
#define DCGM_FI_PROF_PIPE_FP32_ACTIVE 1007
#define DCGM_FI_PROF_PIPE_FP16_ACTIVE 1008
#define DCGM_FI_PROF_PCIE_TX_BYTES 1009
#define DCGM_FI_PROF_PCIE_RX_BYTES 1010
#define DCGM_FI_PROF_NVLINK_TX_BYTES 1011
#define DCGM_FI_PROF_NVLINK_RX_BYTES 1012
#define DCGM_FI_PROF_PIPE_TENSOR_IMMA_ACTIVE 1013
#define DCGM_FI_PROF_PIPE_TENSOR_HMMA_ACTIVE 1014
#define DCGM_FI_PROF_PIPE_TENSOR_DFMA_ACTIVE 1015

#define DCGM_WATCH_UPDATE_USEC 1000000LL /* 1s */
#define DCGM_WATCH_MAX_KEEP_AGE 5.0
#define DCGM_WATCH_MAX_KEEP_SAMPLES 2

typedef struct {
	unsigned int version;
	unsigned short fieldId;
	unsigned short fieldType;
	int status;
	int64_t ts;
	union {
		int64_t i64;
		double dbl;
		char str[DCGM_MAX_STR_LENGTH];
		char blob[DCGM_MAX_BLOB_LENGTH];
	} value;
} dcgmFieldValue_v1;

#define MAKE_DCGM_VERSION(typeName, ver) ((unsigned int)(sizeof(typeName) | ((unsigned long)(ver) << 24U)))
#define dcgmFieldValue_version1 MAKE_DCGM_VERSION(dcgmFieldValue_v1, 1)

typedef struct dcgm_library {
	uv_lib_t lib;
	int loaded;
	int initialized;
	dcgmHandle_t handle;
	dcgmFieldGrp_t field_group_base;
	dcgmFieldGrp_t field_group_prof;
	int watching_base;
	int watching_prof;

	dcgmReturn_t (*dcgmInit)(void);
	dcgmReturn_t (*dcgmShutdown)(void);
	dcgmReturn_t (*dcgmStartEmbedded)(int opMode, dcgmHandle_t *pDcgmHandle);
	dcgmReturn_t (*dcgmStopEmbedded)(dcgmHandle_t pDcgmHandle);
	const char *(*errorString)(dcgmReturn_t result);
	dcgmReturn_t (*dcgmGetAllSupportedDevices)(dcgmHandle_t, unsigned int[DCGM_MAX_NUM_DEVICES], int *);
	dcgmReturn_t (*dcgmGetAllDevices)(dcgmHandle_t, unsigned int[DCGM_MAX_NUM_DEVICES], int *);
	dcgmReturn_t (*dcgmFieldGroupCreate)(dcgmHandle_t, int, unsigned short *, const char *, dcgmFieldGrp_t *);
	dcgmReturn_t (*dcgmFieldGroupDestroy)(dcgmHandle_t, dcgmFieldGrp_t);
	dcgmReturn_t (*dcgmWatchFields)(dcgmHandle_t, dcgmGpuGrp_t, dcgmFieldGrp_t, long long, double, int);
	dcgmReturn_t (*dcgmUnwatchFields)(dcgmHandle_t, dcgmGpuGrp_t, dcgmFieldGrp_t);
	dcgmReturn_t (*dcgmUpdateAllFields)(dcgmHandle_t, int);
	dcgmReturn_t (*dcgmGetLatestValuesForFields)(dcgmHandle_t, int, unsigned short *, unsigned int, dcgmFieldValue_v1 *);
} dcgm_library;

static dcgm_library g_dcgm;
static uv_work_t dcgm_work;
static volatile int dcgm_work_pending;

/* Identity / always-on fields — work on GeForce and datacenter GPUs. */
static const unsigned short dcgm_base_fields[] = {
	DCGM_FI_DEV_GPU_NAME,
	DCGM_FI_DEV_BOARD_SERIAL,
	DCGM_FI_DEV_GPU_UUID,
	DCGM_FI_DEV_PCI_BUS_ID,
};

/* Profiling (DCP) fields — require datacenter / Quadro / supported SKUs. */
static const unsigned short dcgm_prof_fields[] = {
	DCGM_FI_PROF_GR_ENGINE_ACTIVE,
	DCGM_FI_PROF_SM_ACTIVE,
	DCGM_FI_PROF_SM_OCCUPANCY,
	DCGM_FI_PROF_PIPE_TENSOR_ACTIVE,
	DCGM_FI_PROF_DRAM_ACTIVE,
	DCGM_FI_PROF_PIPE_FP64_ACTIVE,
	DCGM_FI_PROF_PIPE_FP32_ACTIVE,
	DCGM_FI_PROF_PIPE_FP16_ACTIVE,
	DCGM_FI_PROF_PCIE_TX_BYTES,
	DCGM_FI_PROF_PCIE_RX_BYTES,
	DCGM_FI_PROF_NVLINK_TX_BYTES,
	DCGM_FI_PROF_NVLINK_RX_BYTES,
	DCGM_FI_PROF_PIPE_TENSOR_IMMA_ACTIVE,
	DCGM_FI_PROF_PIPE_TENSOR_HMMA_ACTIVE,
	DCGM_FI_PROF_PIPE_TENSOR_DFMA_ACTIVE,
};

#define DCGM_BASE_FIELD_COUNT (sizeof(dcgm_base_fields) / sizeof(dcgm_base_fields[0]))
#define DCGM_PROF_FIELD_COUNT (sizeof(dcgm_prof_fields) / sizeof(dcgm_prof_fields[0]))
#define DCGM_WATCH_FIELD_COUNT (DCGM_BASE_FIELD_COUNT + DCGM_PROF_FIELD_COUNT)

static int dcgm_fp64_is_blank(double val)
{
	return val >= DCGM_FP64_BLANK;
}

static int dcgm_int64_is_blank(int64_t val)
{
	return val >= DCGM_INT64_BLANK;
}

static int dcgm_str_is_blank(const char *val)
{
	return !val || !*val || !strcmp(val, DCGM_STR_BLANK) || strstr(val, "<<<");
}

static const char *dcgm_err(dcgm_library *d, dcgmReturn_t rc)
{
	if (d->errorString) {
		const char *s = d->errorString(rc);
		if (s && *s)
			return s;
	}
	return "unknown";
}

static int dcgm_dlsym(dcgm_library *d, const char *sym, void **out)
{
	if (uv_dlsym(&d->lib, sym, out)) {
		*out = NULL;
		return -1;
	}
	return 0;
}

static int dcgm_dlsym_required(dcgm_library *d, const char *sym, void **out)
{
	if (dcgm_dlsym(d, sym, out)) {
		carglog(ac->system_carg, L_ERROR, "dcgm: missing required symbol '%s': %s\n",
			sym, uv_dlerror(&d->lib));
		return -1;
	}
	return 0;
}

static int dcgm_load_library(dcgm_library *d, const char *path)
{
	memset(d, 0, sizeof(*d));
	if (uv_dlopen(path, &d->lib)) {
		carglog(ac->system_carg, L_ERROR, "dcgm: dlopen '%s': %s\n", path, uv_dlerror(&d->lib));
		return -1;
	}

	void *sym;
	if (dcgm_dlsym_required(d, "dcgmInit", &sym))
		goto fail;
	d->dcgmInit = (dcgmReturn_t (*)(void))sym;
	if (dcgm_dlsym_required(d, "dcgmShutdown", &sym))
		goto fail;
	d->dcgmShutdown = (dcgmReturn_t (*)(void))sym;
	if (dcgm_dlsym_required(d, "dcgmStartEmbedded", &sym))
		goto fail;
	d->dcgmStartEmbedded = (dcgmReturn_t (*)(int, dcgmHandle_t *))sym;
	if (dcgm_dlsym_required(d, "dcgmStopEmbedded", &sym))
		goto fail;
	d->dcgmStopEmbedded = (dcgmReturn_t (*)(dcgmHandle_t))sym;
	if (dcgm_dlsym_required(d, "errorString", &sym))
		goto fail;
	d->errorString = (const char *(*)(dcgmReturn_t))sym;
	if (dcgm_dlsym_required(d, "dcgmGetAllSupportedDevices", &sym))
		goto fail;
	d->dcgmGetAllSupportedDevices =
		(dcgmReturn_t (*)(dcgmHandle_t, unsigned int[DCGM_MAX_NUM_DEVICES], int *))sym;
	/* Optional: present in DCGM 2+/3+; used if supported-device list is empty. */
	if (!dcgm_dlsym(d, "dcgmGetAllDevices", &sym))
		d->dcgmGetAllDevices =
			(dcgmReturn_t (*)(dcgmHandle_t, unsigned int[DCGM_MAX_NUM_DEVICES], int *))sym;
	if (dcgm_dlsym_required(d, "dcgmFieldGroupCreate", &sym))
		goto fail;
	d->dcgmFieldGroupCreate =
		(dcgmReturn_t (*)(dcgmHandle_t, int, unsigned short *, const char *, dcgmFieldGrp_t *))sym;
	if (dcgm_dlsym_required(d, "dcgmFieldGroupDestroy", &sym))
		goto fail;
	d->dcgmFieldGroupDestroy = (dcgmReturn_t (*)(dcgmHandle_t, dcgmFieldGrp_t))sym;
	if (dcgm_dlsym_required(d, "dcgmWatchFields", &sym))
		goto fail;
	d->dcgmWatchFields =
		(dcgmReturn_t (*)(dcgmHandle_t, dcgmGpuGrp_t, dcgmFieldGrp_t, long long, double, int))sym;
	if (dcgm_dlsym_required(d, "dcgmUnwatchFields", &sym))
		goto fail;
	d->dcgmUnwatchFields = (dcgmReturn_t (*)(dcgmHandle_t, dcgmGpuGrp_t, dcgmFieldGrp_t))sym;
	if (dcgm_dlsym_required(d, "dcgmUpdateAllFields", &sym))
		goto fail;
	d->dcgmUpdateAllFields = (dcgmReturn_t (*)(dcgmHandle_t, int))sym;
	if (dcgm_dlsym_required(d, "dcgmGetLatestValuesForFields", &sym))
		goto fail;
	d->dcgmGetLatestValuesForFields =
		(dcgmReturn_t (*)(dcgmHandle_t, int, unsigned short *, unsigned int, dcgmFieldValue_v1 *))sym;

	d->loaded = 1;
	return 0;

fail:
	uv_dlclose(&d->lib);
	memset(d, 0, sizeof(*d));
	return -1;
}

static void dcgm_shutdown_engine(dcgm_library *d)
{
	if (!d->initialized)
		return;
	if (d->watching_prof && d->field_group_prof) {
		d->dcgmUnwatchFields(d->handle, DCGM_GROUP_ALL_GPUS, d->field_group_prof);
		d->watching_prof = 0;
	}
	if (d->field_group_prof) {
		d->dcgmFieldGroupDestroy(d->handle, d->field_group_prof);
		d->field_group_prof = 0;
	}
	if (d->watching_base && d->field_group_base) {
		d->dcgmUnwatchFields(d->handle, DCGM_GROUP_ALL_GPUS, d->field_group_base);
		d->watching_base = 0;
	}
	if (d->field_group_base) {
		d->dcgmFieldGroupDestroy(d->handle, d->field_group_base);
		d->field_group_base = 0;
	}
	if (d->handle) {
		d->dcgmStopEmbedded(d->handle);
		d->handle = 0;
	}
	d->dcgmShutdown();
	d->initialized = 0;
}

static int dcgm_watch_field_group(dcgm_library *d, const unsigned short *src, unsigned int count,
	const char *name, dcgmFieldGrp_t *out_grp)
{
	unsigned short fields[DCGM_WATCH_FIELD_COUNT];
	dcgmFieldGrp_t grp = 0;

	if (count > DCGM_WATCH_FIELD_COUNT)
		return -1;
	memcpy(fields, src, count * sizeof(fields[0]));

	dcgmReturn_t rc = d->dcgmFieldGroupCreate(d->handle, (int)count, fields, name, &grp);
	if (rc != DCGM_ST_OK) {
		carglog(ac->system_carg, L_ERROR, "dcgm: FieldGroupCreate(%s) failed: %s\n",
			name, dcgm_err(d, rc));
		return -1;
	}

	rc = d->dcgmWatchFields(d->handle, DCGM_GROUP_ALL_GPUS, grp,
		DCGM_WATCH_UPDATE_USEC, DCGM_WATCH_MAX_KEEP_AGE, DCGM_WATCH_MAX_KEEP_SAMPLES);
	if (rc != DCGM_ST_OK) {
		carglog(ac->system_carg, L_ERROR, "dcgm: WatchFields(%s) failed: %s\n",
			name, dcgm_err(d, rc));
		d->dcgmFieldGroupDestroy(d->handle, grp);
		return -1;
	}

	*out_grp = grp;
	return 0;
}

static int dcgm_ensure_ready(dcgm_library *d)
{
	if (d->initialized)
		return 0;

	module_t *mod = alligator_ht_search(ac->modules, module_compare, "dcgm", tommy_strhash_u32(0, "dcgm"));
	if (!mod || !mod->path || !*mod->path) {
		carglog(ac->system_carg, L_INFO, "dcgm: modules.dcgm is not configured, skipping scrape\n");
		return -1;
	}

	if (dcgm_load_library(d, mod->path))
		return -1;

	dcgmReturn_t rc = d->dcgmInit();
	if (rc != DCGM_ST_OK) {
		carglog(ac->system_carg, L_ERROR, "dcgm: dcgmInit failed: %s\n", dcgm_err(d, rc));
		uv_dlclose(&d->lib);
		memset(d, 0, sizeof(*d));
		return -1;
	}

	rc = d->dcgmStartEmbedded(DCGM_OPERATION_MODE_MANUAL, &d->handle);
	if (rc != DCGM_ST_OK) {
		carglog(ac->system_carg, L_ERROR, "dcgm: dcgmStartEmbedded failed: %s\n", dcgm_err(d, rc));
		d->dcgmShutdown();
		uv_dlclose(&d->lib);
		memset(d, 0, sizeof(*d));
		return -1;
	}

	/* Base identity watches must succeed or DCGM is unusable. */
	if (dcgm_watch_field_group(d, dcgm_base_fields, (unsigned int)DCGM_BASE_FIELD_COUNT,
			"alligator_dcgm_base", &d->field_group_base)) {
		d->dcgmStopEmbedded(d->handle);
		d->dcgmShutdown();
		uv_dlclose(&d->lib);
		memset(d, 0, sizeof(*d));
		return -1;
	}
	d->watching_base = 1;

	/*
	 * PROF_* (DCP) fails on GeForce / unsupported SKUs with "module not loaded".
	 * Keep identity metrics and mark profiling unavailable instead of aborting.
	 */
	if (dcgm_watch_field_group(d, dcgm_prof_fields, (unsigned int)DCGM_PROF_FIELD_COUNT,
			"alligator_dcgm_prof", &d->field_group_prof) == 0) {
		d->watching_prof = 1;
		carglog(ac->system_carg, L_INFO, "dcgm: profiling watches enabled\n");
	} else {
		d->watching_prof = 0;
		d->field_group_prof = 0;
		carglog(ac->system_carg, L_INFO,
			"dcgm: profiling unavailable on this host (common on GeForce); emitting identity only\n");
	}

	d->initialized = 1;
	carglog(ac->system_carg, L_INFO, "dcgm: embedded engine ready (library %s, prof=%d)\n",
		mod->path, d->watching_prof);
	return 0;
}

static int dcgm_apply_double(const dcgmFieldValue_v1 *fv, double *out)
{
	if (fv->status != DCGM_ST_OK)
		return 0;
	if (fv->fieldType == DCGM_FT_DOUBLE) {
		if (dcgm_fp64_is_blank(fv->value.dbl))
			return 0;
		*out = fv->value.dbl;
		return 1;
	}
	if (fv->fieldType == DCGM_FT_INT64) {
		if (dcgm_int64_is_blank(fv->value.i64))
			return 0;
		*out = (double)fv->value.i64;
		return 1;
	}
	return 0;
}

static int dcgm_apply_string(const dcgmFieldValue_v1 *fv, char *out, size_t out_sz)
{
	if (fv->status != DCGM_ST_OK || fv->fieldType != DCGM_FT_STRING)
		return 0;
	if (dcgm_str_is_blank(fv->value.str))
		return 0;
	strlcpy(out, fv->value.str, out_sz);
	return 1;
}

static void dcgm_apply_field_value(dcgm_device_snapshot *snap, unsigned short field_id,
	const dcgmFieldValue_v1 *fv)
{
	double dbl = 0;

	switch (field_id) {
	case DCGM_FI_DEV_GPU_NAME:
		if (dcgm_apply_string(fv, snap->name, sizeof(snap->name)))
			snap->have |= DCGM_HAVE_DEVICE_INFO;
		break;
	case DCGM_FI_DEV_BOARD_SERIAL:
		dcgm_apply_string(fv, snap->serial, sizeof(snap->serial));
		snap->have |= DCGM_HAVE_DEVICE_INFO;
		break;
	case DCGM_FI_DEV_GPU_UUID:
		if (dcgm_apply_string(fv, snap->uuid, sizeof(snap->uuid)))
			snap->have |= DCGM_HAVE_DEVICE_INFO;
		break;
	case DCGM_FI_DEV_PCI_BUS_ID:
		dcgm_apply_string(fv, snap->pci_bus_id, sizeof(snap->pci_bus_id));
		snap->have |= DCGM_HAVE_DEVICE_INFO;
		break;
	case DCGM_FI_PROF_GR_ENGINE_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->gr_engine_active_ratio = dbl;
			snap->have |= DCGM_HAVE_GR_ENGINE;
		}
		break;
	case DCGM_FI_PROF_SM_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->sm_active_ratio = dbl;
			snap->have |= DCGM_HAVE_SM_ACTIVE;
		}
		break;
	case DCGM_FI_PROF_SM_OCCUPANCY:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->sm_occupancy_ratio = dbl;
			snap->have |= DCGM_HAVE_SM_OCCUPANCY;
		}
		break;
	case DCGM_FI_PROF_PIPE_TENSOR_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->tensor_active_ratio = dbl;
			snap->have |= DCGM_HAVE_TENSOR;
		}
		break;
	case DCGM_FI_PROF_DRAM_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->dram_active_ratio = dbl;
			snap->have |= DCGM_HAVE_DRAM;
		}
		break;
	case DCGM_FI_PROF_PIPE_FP64_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->fp64_active_ratio = dbl;
			snap->have |= DCGM_HAVE_FP64;
		}
		break;
	case DCGM_FI_PROF_PIPE_FP32_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->fp32_active_ratio = dbl;
			snap->have |= DCGM_HAVE_FP32;
		}
		break;
	case DCGM_FI_PROF_PIPE_FP16_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->fp16_active_ratio = dbl;
			snap->have |= DCGM_HAVE_FP16;
		}
		break;
	case DCGM_FI_PROF_PCIE_TX_BYTES:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->pcie_tx_bytes = dbl;
			snap->have |= DCGM_HAVE_PCIE_TX;
		}
		break;
	case DCGM_FI_PROF_PCIE_RX_BYTES:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->pcie_rx_bytes = dbl;
			snap->have |= DCGM_HAVE_PCIE_RX;
		}
		break;
	case DCGM_FI_PROF_NVLINK_TX_BYTES:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->nvlink_tx_bytes = dbl;
			snap->have |= DCGM_HAVE_NVLINK_TX;
		}
		break;
	case DCGM_FI_PROF_NVLINK_RX_BYTES:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->nvlink_rx_bytes = dbl;
			snap->have |= DCGM_HAVE_NVLINK_RX;
		}
		break;
	case DCGM_FI_PROF_PIPE_TENSOR_IMMA_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->tensor_imma_active_ratio = dbl;
			snap->have |= DCGM_HAVE_IMMA;
		}
		break;
	case DCGM_FI_PROF_PIPE_TENSOR_HMMA_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->tensor_hmma_active_ratio = dbl;
			snap->have |= DCGM_HAVE_HMMA;
		}
		break;
	case DCGM_FI_PROF_PIPE_TENSOR_DFMA_ACTIVE:
		if (dcgm_apply_double(fv, &dbl)) {
			snap->tensor_dfma_active_ratio = dbl;
			snap->have |= DCGM_HAVE_DFMA;
		}
		break;
	default:
		break;
	}
}

static void dcgm_fetch_fields(dcgm_library *d, unsigned int gpu_id, const unsigned short *src,
	unsigned int count, dcgm_device_snapshot *snap)
{
	dcgmFieldValue_v1 values[DCGM_WATCH_FIELD_COUNT];
	unsigned short fields[DCGM_WATCH_FIELD_COUNT];
	unsigned int i;

	if (!count || count > DCGM_WATCH_FIELD_COUNT)
		return;

	memset(values, 0, sizeof(values[0]) * count);
	for (i = 0; i < count; ++i)
		values[i].version = dcgmFieldValue_version1;
	memcpy(fields, src, count * sizeof(fields[0]));

	dcgmReturn_t rc = d->dcgmGetLatestValuesForFields(d->handle, (int)gpu_id, fields, count, values);
	if (rc != DCGM_ST_OK) {
		carglog(ac->system_carg, L_DEBUG, "dcgm: GetLatestValuesForFields gpu=%u count=%u failed: %s\n",
			gpu_id, count, dcgm_err(d, rc));
		return;
	}

	for (i = 0; i < count; ++i) {
		const dcgmFieldValue_v1 *fv = &values[i];
		unsigned short field_id = fv->fieldId ? fv->fieldId : fields[i];
		dcgm_apply_field_value(snap, field_id, fv);
	}
}

static void dcgm_fill_device(dcgm_library *d, unsigned int gpu_id, unsigned int index, dcgm_device_snapshot *snap)
{
	memset(snap, 0, sizeof(*snap));
	snprintf(snap->index, sizeof(snap->index), "%u", index);
	strlcpy(snap->name, "unknown", sizeof(snap->name));
	strlcpy(snap->uuid, "unknown", sizeof(snap->uuid));
	strlcpy(snap->serial, "unknown", sizeof(snap->serial));
	strlcpy(snap->pci_bus_id, "unknown", sizeof(snap->pci_bus_id));

	dcgm_fetch_fields(d, gpu_id, dcgm_base_fields, (unsigned int)DCGM_BASE_FIELD_COUNT, snap);
	if (d->watching_prof)
		dcgm_fetch_fields(d, gpu_id, dcgm_prof_fields, (unsigned int)DCGM_PROF_FIELD_COUNT, snap);
}

static void dcgm_scrape_sync(void)
{
	dcgm_library *d = &g_dcgm;
	if (dcgm_ensure_ready(d))
		return;

	dcgmReturn_t rc = d->dcgmUpdateAllFields(d->handle, 1);
	if (rc != DCGM_ST_OK) {
		carglog(ac->system_carg, L_ERROR, "dcgm: UpdateAllFields failed: %s\n", dcgm_err(d, rc));
		return;
	}

	unsigned int gpu_ids[DCGM_MAX_NUM_DEVICES] = {0};
	int count = 0;
	rc = d->dcgmGetAllSupportedDevices(d->handle, gpu_ids, &count);
	if (rc != DCGM_ST_OK) {
		carglog(ac->system_carg, L_ERROR, "dcgm: GetAllSupportedDevices failed: %s\n", dcgm_err(d, rc));
		return;
	}
	if (count <= 0 && d->dcgmGetAllDevices) {
		count = 0;
		rc = d->dcgmGetAllDevices(d->handle, gpu_ids, &count);
		if (rc != DCGM_ST_OK) {
			carglog(ac->system_carg, L_ERROR, "dcgm: GetAllDevices failed: %s\n", dcgm_err(d, rc));
			return;
		}
		carglog(ac->system_carg, L_INFO, "dcgm: using GetAllDevices (supported list empty), count=%d\n", count);
	}
	if (count < 0)
		count = 0;
	if (count > DCGM_MAX_NUM_DEVICES)
		count = DCGM_MAX_NUM_DEVICES;

	dcgm_emit_globals(ac->system_carg, (uint64_t)count, d->watching_prof);

	for (int i = 0; i < count; ++i) {
		dcgm_device_snapshot snap;
		dcgm_fill_device(d, gpu_ids[i], (unsigned int)i, &snap);
		dcgm_emit_device(ac->system_carg, &snap);
	}
}

static void dcgm_work_cb(uv_work_t *req)
{
	(void)req;
	dcgm_scrape_sync();
}

static void dcgm_after_work_cb(uv_work_t *req, int status)
{
	(void)req;
	(void)status;
	dcgm_work_pending = 0;
}

void dcgm_wait_idle(void)
{
	uv_loop_t *loop = uv_default_loop();
	if (!loop)
		return;
	/* Drain worker before teardown so scrape code does not use ac after free. */
	while (dcgm_work_pending)
		uv_run(loop, UV_RUN_ONCE);
	if (g_dcgm.initialized)
		dcgm_shutdown_engine(&g_dcgm);
}

void dcgm_schedule_scrape(void)
{
	uv_loop_t *loop = uv_default_loop();
	if (!loop)
		return;
	if (dcgm_work_pending)
		return;
	dcgm_work_pending = 1;
	if (uv_queue_work(loop, &dcgm_work, dcgm_work_cb, dcgm_after_work_cb)) {
		dcgm_work_pending = 0;
		carglog(ac->system_carg, L_ERROR, "dcgm: uv_queue_work failed\n");
	}
}

#endif /* __linux__ */
