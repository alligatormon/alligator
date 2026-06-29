#ifdef __APPLE__
#include "main.h"
#include "common/logs.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <stdio.h>
#include <string.h>

extern aconf *ac;

static char *cfstr_to_cstr(CFStringRef s)
{
	char buf[256];

	if (!s)
		return NULL;
	if (!CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8))
		return NULL;
	return strdup(buf);
}

static uint64_t cfnum_to_u64(CFNumberRef n)
{
	uint64_t val = 0;

	if (!n)
		return 0;
	CFNumberGetValue(n, kCFNumberSInt64Type, &val);
	return val;
}

static void emit_disk_ident(const char *disk, const char *model, const char *serial,
	const char *firmware, uint64_t size)
{
	uint64_t val = 1;
	uint64_t unalloc = 0;

	if (model && model[0])
		metric_add_labels2("disk_model", &val, DATATYPE_UINT, ac->system_carg, "model", (char *)model, "disk", (char *)disk);
	if (serial && serial[0])
		metric_add_labels2("disk_serial", &val, DATATYPE_UINT, ac->system_carg, "serial", (char *)serial, "disk", (char *)disk);
	if (firmware && firmware[0])
		metric_add_labels2("disk_firmware", &val, DATATYPE_UINT, ac->system_carg, "firmware", (char *)firmware, "disk", (char *)disk);
	if (size > 0)
		metric_add_labels2("disk_size", &size, DATATYPE_UINT, ac->system_carg, "disk", (char *)disk, "controller", (char *)disk);
	metric_add_labels2("disk_unallocated", &unalloc, DATATYPE_UINT, ac->system_carg, "disk", (char *)disk, "controller", (char *)disk);
}

static void emit_disk_io(const char *disk, CFDictionaryRef stats)
{
	CFNumberRef n;
	int64_t bytes_read = 0, bytes_write = 0;
	int64_t transfers_read = 0, transfers_write = 0;
	double duration_read = 0.0, duration_write = 0.0, duration_other = 0.0;
	double busy_time = 0.0;

	if (!stats)
		return;

	n = CFDictionaryGetValue(stats, CFSTR("Bytes (Read)"));
	if (n)
		bytes_read = (int64_t)cfnum_to_u64(n);
	n = CFDictionaryGetValue(stats, CFSTR("Bytes (Write)"));
	if (n)
		bytes_write = (int64_t)cfnum_to_u64(n);
	n = CFDictionaryGetValue(stats, CFSTR("Reads"));
	if (n)
		transfers_read = (int64_t)cfnum_to_u64(n);
	n = CFDictionaryGetValue(stats, CFSTR("Writes"));
	if (n)
		transfers_write = (int64_t)cfnum_to_u64(n);
	n = CFDictionaryGetValue(stats, CFSTR("Total Time (Read)"));
	if (n)
		duration_read = (double)cfnum_to_u64(n) / 1000.0;
	n = CFDictionaryGetValue(stats, CFSTR("Total Time (Write)"));
	if (n)
		duration_write = (double)cfnum_to_u64(n) / 1000.0;

	metric_add_labels2("disk_io", &bytes_read, DATATYPE_INT, ac->system_carg, "dev", (char *)disk, "type", "bytes_read");
	metric_add_labels2("disk_io", &bytes_write, DATATYPE_INT, ac->system_carg, "dev", (char *)disk, "type", "bytes_write");
	metric_add_labels2("disk_io", &transfers_read, DATATYPE_INT, ac->system_carg, "dev", (char *)disk, "type", "transfers_read");
	metric_add_labels2("disk_io", &transfers_write, DATATYPE_INT, ac->system_carg, "dev", (char *)disk, "type", "transfers_write");
	metric_add_labels("disk_busy", &busy_time, DATATYPE_DOUBLE, ac->system_carg, "dev", (char *)disk);
	metric_add_labels2("disk_io_await_seconds_total", &duration_read, DATATYPE_DOUBLE, ac->system_carg, "dev", (char *)disk, "type", "read");
	metric_add_labels2("disk_io_await_seconds_total", &duration_write, DATATYPE_DOUBLE, ac->system_carg, "dev", (char *)disk, "type", "write");
	metric_add_labels2("disk_io_await_seconds_total", &duration_other, DATATYPE_DOUBLE, ac->system_carg, "dev", (char *)disk, "type", "other");
}

static int storage_bsd_name(io_object_t service, char *disk, size_t disksz)
{
	io_registry_entry_t parent;
	CFStringRef bsd_name;
	int ok = 0;

	if (IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) != KERN_SUCCESS)
		return 0;

	bsd_name = IORegistryEntryCreateCFProperty(parent, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
	IOObjectRelease(parent);
	if (!bsd_name)
		return 0;

	ok = CFStringGetCString(bsd_name, disk, disksz, kCFStringEncodingUTF8);
	CFRelease(bsd_name);
	return ok;
}

static void scan_storage_identity(io_object_t service)
{
	char disk[64];
	CFMutableDictionaryRef props = NULL;
	char *model = NULL, *serial = NULL, *firmware = NULL;
	uint64_t size = 0;

	if (!storage_bsd_name(service, disk, sizeof(disk)))
		return;

	if (IORegistryEntryCreateCFProperties(service, &props, kCFAllocatorDefault, 0) == KERN_SUCCESS && props) {
		CFTypeRef dc = CFDictionaryGetValue(props, CFSTR("Device Characteristics"));
		CFNumberRef n;

		if (dc && CFGetTypeID(dc) == CFDictionaryGetTypeID()) {
			model = cfstr_to_cstr(CFDictionaryGetValue((CFDictionaryRef)dc, CFSTR("Product Name")));
			serial = cfstr_to_cstr(CFDictionaryGetValue((CFDictionaryRef)dc, CFSTR("Serial Number")));
			firmware = cfstr_to_cstr(CFDictionaryGetValue((CFDictionaryRef)dc, CFSTR("Firmware Revision")));
		}
		n = CFDictionaryGetValue(props, CFSTR("Media Size"));
		if (n)
			size = cfnum_to_u64(n);
		CFRelease(props);
	}

	emit_disk_ident(disk, model, serial, firmware, size);
	free(model);
	free(serial);
	free(firmware);
}

static void scan_storage_io(io_object_t service)
{
	char disk[64];
	CFDictionaryRef stats;

	if (!storage_bsd_name(service, disk, sizeof(disk)))
		return;

	stats = IORegistryEntryCreateCFProperty(service, CFSTR("Statistics"), kCFAllocatorDefault, 0);
	emit_disk_io(disk, stats);
	if (stats)
		CFRelease(stats);
}

static void foreach_storage(void (*cb)(io_object_t))
{
	io_iterator_t iter;
	io_object_t obj;

	if (IOServiceGetMatchingServices(kIOMainPortDefault,
	    IOServiceMatching(kIOBlockStorageDriverClass), &iter) != KERN_SUCCESS)
		return;

	while ((obj = IOIteratorNext(iter))) {
		cb(obj);
		IOObjectRelease(obj);
	}
	IOObjectRelease(iter);
}

void disk_io_stats(void)
{
	uint64_t disks_num = 0;
	io_iterator_t iter;
	io_object_t obj;

	if (IOServiceGetMatchingServices(kIOMainPortDefault,
	    IOServiceMatching(kIOBlockStorageDriverClass), &iter) != KERN_SUCCESS)
		return;

	while ((obj = IOIteratorNext(iter))) {
		scan_storage_io(obj);
		disks_num++;
		IOObjectRelease(obj);
	}
	IOObjectRelease(iter);

	metric_add_auto("disk_num", &disks_num, DATATYPE_UINT, ac->system_carg);
}

void disks_info(void)
{
	foreach_storage(scan_storage_identity);
}
#endif
