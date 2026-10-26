#ifdef __APPLE__
#include "main.h"
#include "common/logs.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <stdio.h>
#include <string.h>

extern aconf *ac;

/* IOKit Total Time (Read|Write) is nanoseconds since driver instantiation. */
static const double IOKIT_TIME_NS_PER_SEC = 1000000000.0;

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

static uint64_t stats_u64(CFDictionaryRef stats, CFStringRef key)
{
	CFNumberRef n = CFDictionaryGetValue(stats, key);

	if (!n || CFGetTypeID(n) != CFNumberGetTypeID())
		return 0;
	return cfnum_to_u64(n);
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
		metric_add_labels2("disk_size_bytes", &size, DATATYPE_UINT, ac->system_carg, "disk", (char *)disk, "controller", (char *)disk);
	metric_add_labels2("disk_unallocated_bytes", &unalloc, DATATYPE_UINT, ac->system_carg, "disk", (char *)disk, "controller", (char *)disk);
}

static void emit_disk_io(const char *disk, CFDictionaryRef stats)
{
	int64_t bytes_read = 0, bytes_write = 0;
	int64_t transfers_read = 0, transfers_write = 0;
	double duration_read = 0.0, duration_write = 0.0, duration_other = 0.0;
	double busy_time = 0.0;

	if (!stats || CFGetTypeID(stats) != CFDictionaryGetTypeID())
		return;

	bytes_read = (int64_t)stats_u64(stats, CFSTR(kIOBlockStorageDriverStatisticsBytesReadKey));
	bytes_write = (int64_t)stats_u64(stats, CFSTR(kIOBlockStorageDriverStatisticsBytesWrittenKey));
	/* Official keys are "Operations (Read|Write)", not "Reads"/"Writes". */
	transfers_read = (int64_t)stats_u64(stats, CFSTR(kIOBlockStorageDriverStatisticsReadsKey));
	transfers_write = (int64_t)stats_u64(stats, CFSTR(kIOBlockStorageDriverStatisticsWritesKey));
	duration_read = (double)stats_u64(stats, CFSTR(kIOBlockStorageDriverStatisticsTotalReadTimeKey))
		/ IOKIT_TIME_NS_PER_SEC;
	duration_write = (double)stats_u64(stats, CFSTR(kIOBlockStorageDriverStatisticsTotalWriteTimeKey))
		/ IOKIT_TIME_NS_PER_SEC;
	/*
	 * Best-effort busy seconds: IOKit exposes cumulative read/write service
	 * time, not a single exclusive busy counter like Linux diskstats field 12.
	 * Concurrent R+W can make this sum exceed wall-clock busy time.
	 */
	busy_time = duration_read + duration_write;

	metric_add_labels2("disk_io_bytes_total", &bytes_read, DATATYPE_INT, ac->system_carg, "dev", (char *)disk, "op", "read");
	metric_add_labels2("disk_io_bytes_total", &bytes_write, DATATYPE_INT, ac->system_carg, "dev", (char *)disk, "op", "write");
	metric_add_labels2("disk_io_transfers_total", &transfers_read, DATATYPE_INT, ac->system_carg, "dev", (char *)disk, "op", "read");
	metric_add_labels2("disk_io_transfers_total", &transfers_write, DATATYPE_INT, ac->system_carg, "dev", (char *)disk, "op", "write");
	metric_add_labels("disk_busy_seconds_total", &busy_time, DATATYPE_DOUBLE, ac->system_carg, "dev", (char *)disk);
	metric_add_labels2("disk_io_await_seconds_total", &duration_read, DATATYPE_DOUBLE, ac->system_carg, "dev", (char *)disk, "type", "read");
	metric_add_labels2("disk_io_await_seconds_total", &duration_write, DATATYPE_DOUBLE, ac->system_carg, "dev", (char *)disk, "type", "write");
	metric_add_labels2("disk_io_await_seconds_total", &duration_other, DATATYPE_DOUBLE, ac->system_carg, "dev", (char *)disk, "type", "other");
}

static int bsd_name_from_entry(io_registry_entry_t entry, char *disk, size_t disksz)
{
	CFTypeRef prop;
	CFStringRef bsd_name;
	int ok;

	prop = IORegistryEntryCreateCFProperty(entry, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
	if (!prop)
		return 0;
	if (CFGetTypeID(prop) != CFStringGetTypeID()) {
		CFRelease(prop);
		return 0;
	}
	bsd_name = prop;
	ok = CFStringGetCString(bsd_name, disk, disksz, kCFStringEncodingUTF8);
	CFRelease(bsd_name);
	return ok && disk[0];
}

static int media_is_whole(io_registry_entry_t entry)
{
	CFTypeRef prop;
	int whole = 0;

	prop = IORegistryEntryCreateCFProperty(entry, CFSTR("Whole"), kCFAllocatorDefault, 0);
	if (!prop)
		return 0;
	if (CFGetTypeID(prop) == CFBooleanGetTypeID())
		whole = CFBooleanGetValue(prop);
	CFRelease(prop);
	return whole;
}

/*
 * Older IOKit trees put "BSD Name" on the parent; Apple Silicon NVMe puts it on
 * the child IOMedia. Walk parent first, then prefer Whole IOMedia children.
 */
static int storage_bsd_name(io_object_t service, char *disk, size_t disksz)
{
	io_registry_entry_t parent;
	io_iterator_t kids;
	io_object_t kid;
	char fallback[64];
	int have_fallback = 0;

	disk[0] = 0;
	fallback[0] = 0;

	if (IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) == KERN_SUCCESS) {
		int ok = bsd_name_from_entry(parent, disk, disksz);

		IOObjectRelease(parent);
		if (ok)
			return 1;
	}

	if (IORegistryEntryGetChildIterator(service, kIOServicePlane, &kids) != KERN_SUCCESS)
		return 0;

	while ((kid = IOIteratorNext(kids))) {
		char name[64];

		if (bsd_name_from_entry(kid, name, sizeof(name))) {
			if (media_is_whole(kid)) {
				strlcpy(disk, name, disksz);
				IOObjectRelease(kid);
				IOObjectRelease(kids);
				return 1;
			}
			if (!have_fallback || strlen(name) < strlen(fallback)) {
				strlcpy(fallback, name, sizeof(fallback));
				have_fallback = 1;
			}
		}
		IOObjectRelease(kid);
	}
	IOObjectRelease(kids);

	if (have_fallback) {
		strlcpy(disk, fallback, disksz);
		return 1;
	}
	return 0;
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
			CFTypeRef v;

			v = CFDictionaryGetValue((CFDictionaryRef)dc, CFSTR("Product Name"));
			if (v && CFGetTypeID(v) == CFStringGetTypeID())
				model = cfstr_to_cstr(v);
			v = CFDictionaryGetValue((CFDictionaryRef)dc, CFSTR("Serial Number"));
			if (v && CFGetTypeID(v) == CFStringGetTypeID())
				serial = cfstr_to_cstr(v);
			v = CFDictionaryGetValue((CFDictionaryRef)dc, CFSTR("Firmware Revision"));
			if (v && CFGetTypeID(v) == CFStringGetTypeID())
				firmware = cfstr_to_cstr(v);
		}
		n = CFDictionaryGetValue(props, CFSTR("Media Size"));
		if (n && CFGetTypeID(n) == CFNumberGetTypeID())
			size = cfnum_to_u64(n);
		CFRelease(props);
	}

	emit_disk_ident(disk, model, serial, firmware, size);
	free(model);
	free(serial);
	free(firmware);
}

static int scan_storage_io(io_object_t service)
{
	char disk[64];
	CFTypeRef prop;
	CFDictionaryRef stats;

	if (!storage_bsd_name(service, disk, sizeof(disk)))
		return 0;

	prop = IORegistryEntryCreateCFProperty(service, CFSTR(kIOBlockStorageDriverStatisticsKey), kCFAllocatorDefault, 0);
	if (!prop || CFGetTypeID(prop) != CFDictionaryGetTypeID()) {
		if (prop)
			CFRelease(prop);
		return 0;
	}
	stats = prop;
	emit_disk_io(disk, stats);
	CFRelease(stats);
	return 1;
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
		if (scan_storage_io(obj))
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
