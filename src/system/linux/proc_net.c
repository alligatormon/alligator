#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "main.h"
#include "common/logs.h"
#include "system/linux/proc_net.h"

extern aconf *ac;

void get_softnet_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/net/softnet_stat", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: softnet '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	uint64_t cpu = 0;
	while (fgets(line, sizeof(line), fd)) {
		char cpu_label[16];
		uint64_t processed = 0, dropped = 0, squeezed = 0;
		int n = sscanf(line, "%" SCNu64 " %" SCNu64 " %" SCNu64, &processed, &dropped, &squeezed);
		if (n < 1)
			continue;

		snprintf(cpu_label, sizeof(cpu_label), "%" PRIu64, cpu);
		metric_add_labels("softnet_processed_total", &processed, DATATYPE_UINT,
			ac->system_carg, "cpu", cpu_label);
		if (n >= 2)
			metric_add_labels("softnet_dropped_total", &dropped, DATATYPE_UINT,
				ac->system_carg, "cpu", cpu_label);
		if (n >= 3)
			metric_add_labels("softnet_times_squeezed_total", &squeezed, DATATYPE_UINT,
				ac->system_carg, "cpu", cpu_label);
		++cpu;
	}
	fclose(fd);
}

void get_sockstat_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/net/sockstat", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: sockstat '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	while (fgets(line, sizeof(line), fd)) {
		char *cur = line;
		cur += strspn(cur, " \t");
		if (!*cur || *cur == '\n')
			continue;

		char proto[32];
		size_t proto_len = strcspn(cur, ": \t");
		if (!proto_len || proto_len >= sizeof(proto))
			continue;
		strlcpy(proto, cur, proto_len + 1);
		cur += proto_len;
		cur += strspn(cur, ": \t");

		if (!strcmp(proto, "sockets")) {
			while (*cur) {
				char key[32];
				uint64_t val = 0;
				if (sscanf(cur, "%31s %" SCNu64, key, &val) != 2)
					break;
				if (!strcmp(key, "used")) {
					metric_add_auto("sockstat_sockets_used", &val, DATATYPE_UINT, ac->system_carg);
					break;
				}
				cur += strcspn(cur, " \t");
				cur += strspn(cur, " \t");
			}
			continue;
		}

		while (*cur) {
			char stat[32];
			uint64_t val = 0;
			if (sscanf(cur, "%31s %" SCNu64, stat, &val) != 2)
				break;
			metric_add_labels2("sockstat_stat_total", &val, DATATYPE_UINT,
				ac->system_carg, "protocol", proto, "stat", stat);
			cur += strcspn(cur, " \t");
			cur += strspn(cur, " \t");
		}
	}
	fclose(fd);
}

#endif
