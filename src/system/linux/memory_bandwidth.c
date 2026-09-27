#ifdef __linux__

#include "system/linux/memory_bandwidth.h"
#include "cadvisor/parsers.h"
#include "main.h"
#include "common/logs.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern aconf *ac;

static int memory_bandwidth_logged_empty;

static void emit_mbm_file(const char *node, const char *scope, const char *path)
{
	FILE *fd;
	char buf[128];
	uint64_t val;

	fd = fopen(path, "r");
	if (!fd)
		return;
	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return;
	}
	fclose(fd);
	val = strtoull(buf, NULL, 10);
	metric_add_labels2("memory_bandwidth_bytes_total", &val, DATATYPE_UINT, ac->system_carg,
		"node", (char *)node, "scope", (char *)scope);
}

void get_memory_bandwidth_stats(void)
{
	char mon_data[512];
	DIR *dp;
	struct dirent *entry;

	if (!ac || !ac->system_sysfs || !ac->system_carg)
		return;

	snprintf(mon_data, sizeof(mon_data), "%s/fs/resctrl/mon_data", ac->system_sysfs);
	if (!cadvisor_dir_has_entries(mon_data)) {
		if (!memory_bandwidth_logged_empty) {
			carglog(ac->system_carg, L_DEBUG,
				"memory_bandwidth: resctrl mon_data missing or empty at %s\n", mon_data);
			memory_bandwidth_logged_empty = 1;
		}
		return;
	}

	dp = opendir(mon_data);
	if (!dp)
		return;

	while ((entry = readdir(dp))) {
		char path[768];
		if (entry->d_name[0] == '.')
			continue;
		if (strncmp(entry->d_name, "mon_L3_", 7))
			continue;

		snprintf(path, sizeof(path), "%s/%s/mbm_total_bytes", mon_data, entry->d_name);
		emit_mbm_file(entry->d_name, "total", path);
		snprintf(path, sizeof(path), "%s/%s/mbm_local_bytes", mon_data, entry->d_name);
		emit_mbm_file(entry->d_name, "local", path);
	}
	closedir(dp);
}

#else

void get_memory_bandwidth_stats(void) {}

#endif
