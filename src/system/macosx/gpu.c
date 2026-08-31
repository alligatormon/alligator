#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "main.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "system/macosx/gpu.h"

extern aconf *ac;

static void macos_gpu_metric_set(context_arg *carg, const char *metric_name, const char *help)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, help);
}

void macos_gpu_emit_globals(context_arg *carg, uint64_t gpu_count)
{
	if (!carg)
		return;

	macos_gpu_metric_set(carg, "macos_gpu_gpu_count", "Number of GPUs visible via IOKit IOAccelerator.");
	metric_add_auto("macos_gpu_gpu_count", &gpu_count, DATATYPE_UINT, carg);
}

static void macos_gpu_emit_gauge3(context_arg *carg, const char *name, const char *help,
	double value, const macos_gpu_device_snapshot *dev)
{
	macos_gpu_metric_set(carg, name, help);
	metric_add_labels3((char *)name, &value, DATATYPE_DOUBLE, carg,
		"name", (char *)dev->name, "class", (char *)dev->class_name,
		"index", (char *)dev->index);
}

void macos_gpu_emit_device(context_arg *carg, const macos_gpu_device_snapshot *dev)
{
	if (!carg || !dev)
		return;

	uint64_t one = 1;

	if (dev->have & MACOS_GPU_HAVE_DEVICE_INFO) {
		macos_gpu_metric_set(carg, "macos_gpu_device_info",
			"GPU identity labels from IOKit IOAccelerator.");
		metric_add_labels4("macos_gpu_device_info", &one, DATATYPE_UINT, carg,
			"name", (char *)dev->name, "class", (char *)dev->class_name,
			"index", (char *)dev->index, "compat", (char *)dev->compat);
	}

	if (dev->have & MACOS_GPU_HAVE_UTIL_DEVICE)
		macos_gpu_emit_gauge3(carg, "macos_gpu_utilization_device_percent",
			"Device utilization percent from IOAccelerator PerformanceStatistics.",
			dev->util_device_percent, dev);
	if (dev->have & MACOS_GPU_HAVE_UTIL_RENDERER)
		macos_gpu_emit_gauge3(carg, "macos_gpu_utilization_renderer_percent",
			"Renderer utilization percent from IOAccelerator PerformanceStatistics.",
			dev->util_renderer_percent, dev);
	if (dev->have & MACOS_GPU_HAVE_UTIL_TILER)
		macos_gpu_emit_gauge3(carg, "macos_gpu_utilization_tiler_percent",
			"Tiler utilization percent from IOAccelerator PerformanceStatistics.",
			dev->util_tiler_percent, dev);

	if (dev->have & MACOS_GPU_HAVE_MEM_ALLOC)
		macos_gpu_emit_gauge3(carg, "macos_gpu_memory_alloc_bytes",
			"Allocated unified system GPU memory in bytes.",
			dev->memory_alloc_bytes, dev);
	if (dev->have & MACOS_GPU_HAVE_MEM_IN_USE)
		macos_gpu_emit_gauge3(carg, "macos_gpu_memory_in_use_bytes",
			"In-use unified system GPU memory in bytes.",
			dev->memory_in_use_bytes, dev);
	if (dev->have & MACOS_GPU_HAVE_MEM_IN_USE_DRIVER)
		macos_gpu_emit_gauge3(carg, "macos_gpu_memory_in_use_driver_bytes",
			"In-use unified system GPU memory held by the driver in bytes.",
			dev->memory_in_use_driver_bytes, dev);

	if (dev->have & MACOS_GPU_HAVE_MEM_DEV_ALLOC)
		macos_gpu_emit_gauge3(carg, "macos_gpu_memory_device_alloc_bytes",
			"Allocated dedicated GPU device memory in bytes.",
			dev->memory_device_alloc_bytes, dev);
	if (dev->have & MACOS_GPU_HAVE_MEM_DEV_IN_USE)
		macos_gpu_emit_gauge3(carg, "macos_gpu_memory_device_in_use_bytes",
			"In-use dedicated GPU device memory in bytes.",
			dev->memory_device_in_use_bytes, dev);

	if (dev->have & MACOS_GPU_HAVE_CORE_COUNT)
		macos_gpu_emit_gauge3(carg, "macos_gpu_core_count",
			"GPU core count from IOKit.",
			dev->core_count, dev);
	if (dev->have & MACOS_GPU_HAVE_RECOVERY)
		macos_gpu_emit_gauge3(carg, "macos_gpu_recovery_count",
			"GPU recovery/restart count from IOAccelerator PerformanceStatistics.",
			dev->recovery_count, dev);
}

#ifdef __APPLE__

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

static int macos_gpu_cf_copy_string(CFTypeRef val, char *dst, size_t dstlen)
{
	if (!val || !dst || dstlen == 0)
		return 0;

	if (CFGetTypeID(val) == CFStringGetTypeID()) {
		if (CFStringGetCString((CFStringRef)val, dst, (CFIndex)dstlen, kCFStringEncodingUTF8) && dst[0])
			return 1;
		return 0;
	}

	if (CFGetTypeID(val) == CFDataGetTypeID()) {
		CFDataRef data = (CFDataRef)val;
		CFIndex n = CFDataGetLength(data);
		const UInt8 *bytes;

		if (n <= 0)
			return 0;
		bytes = CFDataGetBytePtr(data);
		while (n > 0 && bytes[n - 1] == 0)
			--n;
		if (n <= 0)
			return 0;
		if ((size_t)n >= dstlen)
			n = (CFIndex)dstlen - 1;
		memcpy(dst, bytes, (size_t)n);
		dst[n] = '\0';
		return dst[0] ? 1 : 0;
	}

	if (CFGetTypeID(val) == CFArrayGetTypeID()) {
		CFArrayRef arr = (CFArrayRef)val;
		if (CFArrayGetCount(arr) < 1)
			return 0;
		return macos_gpu_cf_copy_string(CFArrayGetValueAtIndex(arr, 0), dst, dstlen);
	}

	return 0;
}

static int macos_gpu_cf_get_double(CFTypeRef val, double *out)
{
	if (!val || !out)
		return 0;

	if (CFGetTypeID(val) == CFNumberGetTypeID())
		return CFNumberGetValue((CFNumberRef)val, kCFNumberDoubleType, out) ? 1 : 0;

	if (CFGetTypeID(val) == CFStringGetTypeID()) {
		char buf[64];
		if (CFStringGetCString((CFStringRef)val, buf, sizeof(buf), kCFStringEncodingUTF8)) {
			*out = strtod(buf, NULL);
			return 1;
		}
	}
	return 0;
}

static CFTypeRef macos_gpu_dict_get(CFDictionaryRef dict, CFStringRef key)
{
	return dict ? CFDictionaryGetValue(dict, key) : NULL;
}

static int macos_gpu_dict_double(CFDictionaryRef dict, CFStringRef key, double *out)
{
	return macos_gpu_cf_get_double(macos_gpu_dict_get(dict, key), out);
}

static int macos_gpu_dict_string(CFDictionaryRef dict, CFStringRef key, char *dst, size_t dstlen)
{
	return macos_gpu_cf_copy_string(macos_gpu_dict_get(dict, key), dst, dstlen);
}

static void macos_gpu_fill_device(CFDictionaryRef props, unsigned int index, macos_gpu_device_snapshot *snap)
{
	CFDictionaryRef perf;
	double v;

	memset(snap, 0, sizeof(*snap));
	strlcpy(snap->name, "unknown", sizeof(snap->name));
	strlcpy(snap->class_name, "unknown", sizeof(snap->class_name));
	strlcpy(snap->compat, "unknown", sizeof(snap->compat));
	snprintf(snap->index, sizeof(snap->index), "%u", index);

	macos_gpu_dict_string(props, CFSTR("model"), snap->name, sizeof(snap->name));
	macos_gpu_dict_string(props, CFSTR("IOClass"), snap->class_name, sizeof(snap->class_name));
	if (!macos_gpu_dict_string(props, CFSTR("IONameMatched"), snap->compat, sizeof(snap->compat)))
		macos_gpu_dict_string(props, CFSTR("IONameMatch"), snap->compat, sizeof(snap->compat));
	snap->have |= MACOS_GPU_HAVE_DEVICE_INFO;

	if (macos_gpu_dict_double(props, CFSTR("gpu-core-count"), &v)) {
		snap->core_count = v;
		snap->have |= MACOS_GPU_HAVE_CORE_COUNT;
	}

	perf = macos_gpu_dict_get(props, CFSTR("PerformanceStatistics"));
	if (!perf || CFGetTypeID(perf) != CFDictionaryGetTypeID())
		return;

	if (macos_gpu_dict_double(perf, CFSTR("Device Utilization %"), &v)) {
		snap->util_device_percent = v;
		snap->have |= MACOS_GPU_HAVE_UTIL_DEVICE;
	}
	if (macos_gpu_dict_double(perf, CFSTR("Renderer Utilization %"), &v)) {
		snap->util_renderer_percent = v;
		snap->have |= MACOS_GPU_HAVE_UTIL_RENDERER;
	}
	if (macos_gpu_dict_double(perf, CFSTR("Tiler Utilization %"), &v)) {
		snap->util_tiler_percent = v;
		snap->have |= MACOS_GPU_HAVE_UTIL_TILER;
	}
	if (macos_gpu_dict_double(perf, CFSTR("Alloc system memory"), &v)) {
		snap->memory_alloc_bytes = v;
		snap->have |= MACOS_GPU_HAVE_MEM_ALLOC;
	}
	if (macos_gpu_dict_double(perf, CFSTR("In use system memory"), &v)) {
		snap->memory_in_use_bytes = v;
		snap->have |= MACOS_GPU_HAVE_MEM_IN_USE;
	}
	if (macos_gpu_dict_double(perf, CFSTR("In use system memory (driver)"), &v)) {
		snap->memory_in_use_driver_bytes = v;
		snap->have |= MACOS_GPU_HAVE_MEM_IN_USE_DRIVER;
	}
	if (macos_gpu_dict_double(perf, CFSTR("Alloc device memory"), &v)) {
		snap->memory_device_alloc_bytes = v;
		snap->have |= MACOS_GPU_HAVE_MEM_DEV_ALLOC;
	}
	if (macos_gpu_dict_double(perf, CFSTR("In use device memory"), &v)) {
		snap->memory_device_in_use_bytes = v;
		snap->have |= MACOS_GPU_HAVE_MEM_DEV_IN_USE;
	}
	if (macos_gpu_dict_double(perf, CFSTR("recoveryCount"), &v)) {
		snap->recovery_count = v;
		snap->have |= MACOS_GPU_HAVE_RECOVERY;
	}
}

void macos_gpu_scrape(void)
{
	io_iterator_t it;
	io_object_t service;
	kern_return_t kr;
	unsigned int index = 0;
	uint64_t count = 0;
	context_arg *carg;

	if (!ac || !ac->system_carg)
		return;
	carg = ac->system_carg;

	kr = IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it);
	if (kr != KERN_SUCCESS) {
		carglog(carg, L_DEBUG, "macos_gpu: IOServiceGetMatchingServices failed: %d\n", (int)kr);
		return;
	}

	while ((service = IOIteratorNext(it)) != 0) {
		CFMutableDictionaryRef props = NULL;
		macos_gpu_device_snapshot snap;

		kr = IORegistryEntryCreateCFProperties(service, &props, kCFAllocatorDefault, 0);
		if (kr == KERN_SUCCESS && props) {
			macos_gpu_fill_device(props, index, &snap);
			macos_gpu_emit_device(carg, &snap);
			count++;
			CFRelease(props);
		}
		IOObjectRelease(service);
		index++;
	}
	IOObjectRelease(it);

	macos_gpu_emit_globals(carg, count);
}

#endif /* __APPLE__ */
