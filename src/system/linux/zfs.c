#ifdef __linux__

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "common/logs.h"
#include "system/linux/zfs.h"

extern aconf *ac;

/* node_exporter zfsPoolStatesName — one-hot gauges. */
static const char *zfs_pool_states[] = {
	"online", "degraded", "faulted", "offline", "removed", "unavail", "suspended"
};

static void zfs_normalize_stat(char *dst, size_t dstlen, const char *src)
{
	size_t j = 0;
	size_t i;

	for (i = 0; src[i] && j + 1 < dstlen; ++i) {
		char c = src[i];
		if (c == '-')
			c = '_';
		dst[j++] = c;
	}
	dst[j] = '\0';
}

/* Current ARC size/target/occupancy vs cumulative hits/evicts. Unknown keys default to counters. */
static int zfs_arc_is_gauge(const char *key)
{
	size_t n;

	if (!key || !key[0])
		return 0;
	if (!strcmp(key, "c") || !strcmp(key, "c_min") || !strcmp(key, "c_max")
		|| !strcmp(key, "p") || !strcmp(key, "size"))
		return 1;
	n = strlen(key);
	if (n >= 5 && !strcmp(key + n - 5, "_size"))
		return 1;
	if (strstr(key, "evictable"))
		return 1;
	if (!strncmp(key, "arc_meta_", 9) || !strncmp(key, "hash_", 5))
		return 1;
	if (!strcmp(key, "arc_need_free") || !strcmp(key, "arc_no_grow")
		|| !strcmp(key, "arc_prune") || !strcmp(key, "arc_sys_free")
		|| !strcmp(key, "arc_tempreserve") || !strcmp(key, "arc_loaned_bytes")
		|| !strcmp(key, "duplicate_buffers") || !strcmp(key, "memory_available_bytes")
		|| !strcmp(key, "l2_asize"))
		return 1;
	return 0;
}

static void zfs_emit_named(const char *metric, const char *key, const char *type_code, const char *data)
{
	char stat[128];

	zfs_normalize_stat(stat, sizeof(stat), key);
	if (!stat[0])
		return;

	if (!strcmp(type_code, "3")) {
		int64_t v = strtoll(data, NULL, 10);
		metric_add_labels((char *)metric, &v, DATATYPE_INT, ac->system_carg, "stat", stat);
	} else if (!strcmp(type_code, "4")) {
		uint64_t v = strtoull(data, NULL, 10);
		metric_add_labels((char *)metric, &v, DATATYPE_UINT, ac->system_carg, "stat", stat);
	}
}

static void zfs_parse_named_file(const char *path, const char *counter_metric, const char *gauge_metric)
{
	FILE *fd;
	char line[512];
	int in_data = 0;

	fd = fopen(path, "r");
	if (!fd)
		return;

	carglog(ac->system_carg, L_TRACE, "system scrape metrics: zfs: '%s'\n", path);

	while (fgets(line, sizeof(line), fd)) {
		char name[128];
		char typ[16];
		char data[64];
		const char *metric;

		if (sscanf(line, "%127s %15s %63s", name, typ, data) < 3)
			continue;

		if (!in_data) {
			if (!strcmp(name, "name") && !strcmp(typ, "type") && !strcmp(data, "data"))
				in_data = 1;
			continue;
		}

		if (gauge_metric && zfs_arc_is_gauge(name))
			metric = gauge_metric;
		else
			metric = counter_metric;
		zfs_emit_named(metric, name, typ, data);
	}

	fclose(fd);
}

static void zfs_lowercase_inplace(char *s)
{
	for (; s && *s; ++s)
		*s = (char)tolower((unsigned char)*s);
}

static void zfs_parse_pool_state(const char *path, const char *pool)
{
	FILE *fd;
	char buf[64];
	size_t n;
	size_t i;
	uint64_t one = 1;
	uint64_t zero = 0;

	fd = fopen(path, "r");
	if (!fd)
		return;

	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return;
	}
	fclose(fd);

	n = strlen(buf);
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' || buf[n - 1] == '\t'))
		buf[--n] = '\0';
	zfs_lowercase_inplace(buf);

	for (i = 0; i < sizeof(zfs_pool_states) / sizeof(zfs_pool_states[0]); ++i) {
		uint64_t *v = strcmp(buf, zfs_pool_states[i]) ? &zero : &one;
		metric_add_labels2("zfs_zpool_state", v, DATATYPE_UINT, ac->system_carg,
			"pool", (char *)pool, "state", (char *)zfs_pool_states[i]);
	}
}

void get_zfs_stats(void)
{
	char root[1024];
	char path[1200];
	DIR *dir;
	struct dirent *ent;

	snprintf(root, sizeof(root), "%s/spl/kstat/zfs", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: zfs: '%s'\n", root);

	dir = opendir(root);
	if (!dir)
		return;

	/* Host kstats. Per-dataset objset-* files are not collected (cardinality). */
	snprintf(path, sizeof(path), "%s/arcstats", root);
	zfs_parse_named_file(path, "zfs_arc_stat", "zfs_arc_bytes");
	snprintf(path, sizeof(path), "%s/dmu_tx", root);
	zfs_parse_named_file(path, "zfs_dmu_tx_stat", NULL);
	snprintf(path, sizeof(path), "%s/zil", root);
	zfs_parse_named_file(path, "zfs_zil_stat", NULL);

	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s/state", root, ent->d_name);
		zfs_parse_pool_state(path, ent->d_name);
	}

	closedir(dir);
}

#endif
