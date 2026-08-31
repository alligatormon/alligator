#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include "main.h"
#include "common/logs.h"
#include "common/selector.h"
#include "system/linux/fabric_sysfs.h"

extern aconf *ac;

static void normalize_stat_name(char *dst, size_t dstlen, const char *src)
{
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 1 < dstlen; ++i) {
		char c = src[i];
		if (c == ' ' || c == '-' || c == '/')
			c = '_';
		else if (isupper((unsigned char)c))
			c = (char)tolower((unsigned char)c);
		if (c == '_' && j && dst[j - 1] == '_')
			continue;
		dst[j++] = c;
	}
	while (j && dst[j - 1] == '_')
		--j;
	dst[j] = '\0';
}

static void emit_sysfs_counter(const char *metric, const char *l1, const char *v1,
	const char *l2, const char *v2, const char *l3, const char *v3, const char *file)
{
	int64_t raw = getkvfile((char *)file);
	if (raw < 0)
		return;
	uint64_t val = (uint64_t)raw;

	if (l3)
		metric_add_labels3((char *)metric, &val, DATATYPE_UINT,
			ac->system_carg, (char *)l1, (char *)v1, (char *)l2, (char *)v2, (char *)l3, (char *)v3);
	else if (l2)
		metric_add_labels2((char *)metric, &val, DATATYPE_UINT,
			ac->system_carg, (char *)l1, (char *)v1, (char *)l2, (char *)v2);
	else
		metric_add_labels((char *)metric, &val, DATATYPE_UINT, ac->system_carg, (char *)l1, (char *)v1);
}

static void walk_counter_dir(const char *metric, const char *l1, const char *v1,
	const char *l2, const char *v2, const char *l3, const char *v3, const char *dir)
{
	DIR *d = opendir(dir);
	if (!d)
		return;

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		char stat[64];
		normalize_stat_name(stat, sizeof(stat), ent->d_name);
		emit_sysfs_counter(metric, l1, v1, l2, v2, l3, stat, path);
	}
	closedir(d);
}

void get_infiniband_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/class/infiniband", ac->system_sysfs);
	DIR *dir = opendir(root);
	if (!dir)
		return;

	struct dirent *dev;
	while ((dev = readdir(dir)) != NULL) {
		if (dev->d_name[0] == '.')
			continue;

		char ports[768];
		snprintf(ports, sizeof(ports), "%s/%s/ports", root, dev->d_name);
		DIR *pdir = opendir(ports);
		if (!pdir)
			continue;

		struct dirent *port;
		while ((port = readdir(pdir)) != NULL) {
			if (port->d_name[0] == '.')
				continue;

			char counters[1024];
			snprintf(counters, sizeof(counters), "%s/%s/counters", ports, port->d_name);
			walk_counter_dir("infiniband_stat_total", "device", dev->d_name,
				"port", port->d_name, "stat", NULL, counters);

			char hw[1100];
			snprintf(hw, sizeof(hw), "%s/%s/hw_counters", ports, port->d_name);
			walk_counter_dir("infiniband_stat_total", "device", dev->d_name,
				"port", port->d_name, "stat", NULL, hw);
		}
		closedir(pdir);
	}
	closedir(dir);
}

void get_fibrechannel_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/class/fc_host", ac->system_sysfs);
	DIR *dir = opendir(root);
	if (!dir)
		return;

	struct dirent *host;
	while ((host = readdir(dir)) != NULL) {
		if (host->d_name[0] == '.')
			continue;

		char stats[768];
		snprintf(stats, sizeof(stats), "%s/%s/statistics", root, host->d_name);
		DIR *sdir = opendir(stats);
		if (!sdir)
			continue;

		struct dirent *sf;
		while ((sf = readdir(sdir)) != NULL) {
			if (sf->d_name[0] == '.')
				continue;
			char stat[64];
			normalize_stat_name(stat, sizeof(stat), sf->d_name);
			char path[1024];
			snprintf(path, sizeof(path), "%s/%s", stats, sf->d_name);
			emit_sysfs_counter("fibrechannel_stat_total", "host", host->d_name,
				"stat", stat, NULL, NULL, path);
		}
		closedir(sdir);
	}
	closedir(dir);
}

#endif
