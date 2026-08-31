#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "main.h"
#include "common/logs.h"
#include "system/linux/vm_stats.h"

extern aconf *ac;

void get_swap_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/swaps", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: swaps '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	if (!fgets(line, sizeof(line), fd)) {
		fclose(fd);
		return;
	}

	while (fgets(line, sizeof(line), fd)) {
		char device[256];
		char type[32];
		uint64_t size_kb = 0;
		uint64_t used_kb = 0;
		int priority = 0;

		if (sscanf(line, "%255s %31s %" SCNu64 " %" SCNu64 " %d",
			device, type, &size_kb, &used_kb, &priority) < 4)
			continue;

		uint64_t size_bytes = size_kb * 1024ULL;
		uint64_t used_bytes = used_kb * 1024ULL;
		uint64_t free_bytes = (size_kb >= used_kb) ? (size_kb - used_kb) * 1024ULL : 0;

		metric_add_labels2("swap_device_bytes", &size_bytes, DATATYPE_UINT,
			ac->system_carg, "device", device, "type", "size");
		metric_add_labels2("swap_device_bytes", &used_bytes, DATATYPE_UINT,
			ac->system_carg, "device", device, "type", "used");
		metric_add_labels2("swap_device_bytes", &free_bytes, DATATYPE_UINT,
			ac->system_carg, "device", device, "type", "free");

		int64_t prio = priority;
		metric_add_labels("swap_device_priority", &prio, DATATYPE_INT,
			ac->system_carg, "device", device);
	}
	fclose(fd);
}

void get_schedstat_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/schedstat", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: schedstat '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	while (fgets(line, sizeof(line), fd)) {
		int cpu = -1;
		uint64_t run_time = 0;
		uint64_t runqueue_time = 0;
		uint64_t run_periods = 0;

		if (sscanf(line, "cpu%d %" SCNu64 " %" SCNu64 " %" SCNu64,
			&cpu, &run_time, &runqueue_time, &run_periods) != 4)
			continue;

		char cpu_label[16];
		snprintf(cpu_label, sizeof(cpu_label), "%d", cpu);

		metric_add_labels("schedstat_run_time_nanoseconds_total", &run_time, DATATYPE_UINT,
			ac->system_carg, "cpu", cpu_label);
		metric_add_labels("schedstat_runqueue_time_nanoseconds_total", &runqueue_time, DATATYPE_UINT,
			ac->system_carg, "cpu", cpu_label);
		metric_add_labels("schedstat_run_periods_total", &run_periods, DATATYPE_UINT,
			ac->system_carg, "cpu", cpu_label);
	}
	fclose(fd);
}

void get_slabinfo_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/slabinfo", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: memory: slabinfo '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[1024];
	while (fgets(line, sizeof(line), fd)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		if (!strncmp(line, "slabinfo", 8))
			continue;

		char slab[256];
		uint64_t active_objs = 0;
		uint64_t num_objs = 0;
		unsigned int objsize = 0;

		if (sscanf(line, "%255s %" SCNu64 " %" SCNu64 " %u",
			slab, &active_objs, &num_objs, &objsize) < 4)
			continue;

		metric_add_labels2("slabinfo_objects", &active_objs, DATATYPE_UINT,
			ac->system_carg, "slab", slab, "type", "active");
		metric_add_labels2("slabinfo_objects", &num_objs, DATATYPE_UINT,
			ac->system_carg, "slab", slab, "type", "total");
		uint64_t objsize_u64 = objsize;
		metric_add_labels("slabinfo_object_size_bytes", &objsize_u64, DATATYPE_UINT,
			ac->system_carg, "slab", slab);
	}
	fclose(fd);
}

#endif
