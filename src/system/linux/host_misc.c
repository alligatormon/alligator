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
#include "system/linux/host_misc.h"

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
		if (c == '_') {
			if (j && dst[j - 1] == '_')
				continue;
		}
		dst[j++] = c;
	}
	while (j && dst[j - 1] == '_')
		--j;
	dst[j] = '\0';
}

static void emit_zoneinfo_protection(char *node, char *zone, char *line)
{
	char *paren = strchr(line, '(');
	if (!paren)
		return;
	++paren;
	char *end = strchr(paren, ')');
	if (!end)
		return;
	*end = '\0';

	unsigned idx = 0;
	for (char *tok = paren; *tok; ) {
		while (*tok == ' ' || *tok == ',')
			++tok;
		if (!*tok)
			break;
		char *comma = strchr(tok, ',');
		if (comma)
			*comma = '\0';
		uint64_t val = strtoull(tok, NULL, 10);
		char stat[64];
		snprintf(stat, sizeof(stat), "protection_%u", idx);
		metric_add_labels3("zoneinfo_stat_total", &val, DATATYPE_UINT,
			ac->system_carg, "node", node, "zone", zone, "stat", stat);
		++idx;
		if (!comma)
			break;
		tok = comma + 1;
	}
}

void get_softirqs_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/softirqs", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: interrupts: softirqs '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[4096];
	char header[4096];
	if (!fgets(header, sizeof(header), fd)) {
		fclose(fd);
		return;
	}

	size_t ncpu = 0;
	char *cur = header;
	cur += strspn(cur, " \t");
	while (*cur) {
		char *end = cur + strcspn(cur, " \t\n");
		if (end == cur)
			break;
		++ncpu;
		cur = end + 1;
		cur += strspn(cur, " \t");
	}

	while (fgets(line, sizeof(line), fd)) {
		char *type = line;
		type += strspn(type, " \t");
		if (!*type || *type == '\n')
			continue;

		char *colon = strchr(type, ':');
		if (!colon)
			continue;
		*colon = '\0';
		char type_label[32];
		normalize_stat_name(type_label, sizeof(type_label), type);

		cur = colon + 1;
		for (size_t i = 0; i < ncpu; ++i) {
			while (*cur == ' ' || *cur == '\t')
				++cur;
			if (!*cur)
				break;
			uint64_t val = strtoull(cur, &cur, 10);
			char cpu_label[16];
			snprintf(cpu_label, sizeof(cpu_label), "%zu", i);
			metric_add_labels2("softirq_stat_total", &val, DATATYPE_UINT,
				ac->system_carg, "cpu", cpu_label, "type", type_label);
		}
	}
	fclose(fd);
}

void get_entropy_stats(void)
{
	char path[512];
	uint8_t err = 0;
	uint64_t avail = 0, pool = 0;

	snprintf(path, sizeof(path), "%s/sys/kernel/random/entropy_avail", ac->system_procfs);
	int64_t v = getkvfile_ext(path, &err);
	if (!err) {
		avail = (uint64_t)v;
		metric_add_auto("entropy_available_bits", &avail, DATATYPE_UINT, ac->system_carg);
	}

	snprintf(path, sizeof(path), "%s/sys/kernel/random/poolsize", ac->system_procfs);
	v = getkvfile_ext(path, &err);
	if (!err) {
		pool = (uint64_t)v;
		metric_add_auto("entropy_pool_size_bits", &pool, DATATYPE_UINT, ac->system_carg);
	}
}

void get_selinux_stats(void)
{
	char path[512];
	uint8_t err = 0;
	snprintf(path, sizeof(path), "%s/fs/selinux/enforce", ac->system_sysfs);
	int64_t enforce = getkvfile_ext(path, &err);
	if (err)
		return;

	uint64_t enabled = 1;
	uint64_t mode = (uint64_t)enforce;
	metric_add_auto("selinux_enabled", &enabled, DATATYPE_UINT, ac->system_carg);
	metric_add_auto("selinux_enforce_mode", &mode, DATATYPE_UINT, ac->system_carg);
}

static void read_watchdog_device(const char *name, const char *base)
{
	char path[512];
	char stat[32];
	uint64_t val;

	const char *metrics[] = {"timeleft", "timeout", "nowayout", "bootstatus", NULL};
	for (int i = 0; metrics[i]; ++i) {
		snprintf(path, sizeof(path), "%s/%s", base, metrics[i]);
		int64_t v = getkvfile(path);
		if (v < 0)
			continue;
		val = (uint64_t)v;
		normalize_stat_name(stat, sizeof(stat), metrics[i]);
		metric_add_labels2("watchdog_stat", &val, DATATYPE_UINT,
			ac->system_carg, "device", (char *)name, "type", stat);
	}
}

void get_watchdog_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/class/watchdog", ac->system_sysfs);
	DIR *dir = opendir(root);
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		char base[768];
		snprintf(base, sizeof(base), "%s/%s", root, ent->d_name);
		read_watchdog_device(ent->d_name, base);
	}
	closedir(dir);
}

static void read_rapl_zone(const char *path, const char *name, int index)
{
	char energy_path[1024];
	snprintf(energy_path, sizeof(energy_path), "%s/energy_uj", path);
	int64_t uj = getkvfile(energy_path);
	if (uj < 0)
		return;

	double joules = uj / 1000000.0;
	char idx[16];
	snprintf(idx, sizeof(idx), "%d", index);
	metric_add_labels2("rapl_energy_joules_total", &joules, DATATYPE_DOUBLE,
		ac->system_carg, "name", (char *)name, "index", idx);
}

static void walk_rapl_dir(const char *dirpath, int *index)
{
	DIR *dir = opendir(dirpath);
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		char sub[1024];
		snprintf(sub, sizeof(sub), "%s/%s", dirpath, ent->d_name);
		char energy_path[1100];
		snprintf(energy_path, sizeof(energy_path), "%s/energy_uj", sub);
		FILE *test = fopen(energy_path, "r");
		if (test) {
			fclose(test);
			read_rapl_zone(sub, ent->d_name, (*index)++);
		} else {
			walk_rapl_dir(sub, index);
		}
	}
	closedir(dir);
}

void get_rapl_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/class/powercap", ac->system_sysfs);
	int index = 0;
	walk_rapl_dir(root, &index);
}

void get_zoneinfo_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/zoneinfo", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: zoneinfo '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	char node[16] = "";
	char zone[32] = "";

	while (fgets(line, sizeof(line), fd)) {
		if (!strncmp(line, "Node", 4)) {
			char *comma = strchr(line, ',');
			if (!comma)
				continue;
			char *node_start = line + 4;
			node_start += strspn(node_start, " \t");
			size_t node_len = strcspn(node_start, ", \t");
			strlcpy(node, node_start, node_len + 1);

			char *zone_start = strstr(comma, "zone");
			if (!zone_start)
				continue;
			zone_start += 4;
			zone_start += strspn(zone_start, " \t");
			size_t zone_len = strcspn(zone_start, " \t\n");
			strlcpy(zone, zone_start, zone_len + 1);
			continue;
		}

		char buf[512];
		strlcpy(buf, line, sizeof(buf));
		size_t blen = strlen(buf);
		while (blen && (buf[blen - 1] == '\n' || buf[blen - 1] == ' ' || buf[blen - 1] == '\t'))
			buf[--blen] = '\0';

		if (strstr(buf, "protection:")) {
			if (node[0] && zone[0])
				emit_zoneinfo_protection(node, zone, buf);
			continue;
		}

		char *last_space = NULL;
		for (char *p = buf; *p; ++p) {
			if (*p == ' ' || *p == '\t')
				last_space = p;
		}
		if (!last_space)
			continue;

		char *val_start = last_space + 1;
		val_start += strspn(val_start, " \t");
		if (!*val_start)
			continue;

		uint64_t val = strtoull(val_start, NULL, 10);
		while (last_space > buf && (last_space[-1] == ' ' || last_space[-1] == '\t'))
			--last_space;
		*last_space = '\0';

		char stat[64];
		char *stat_start = buf;
		stat_start += strspn(stat_start, " \t");
		strlcpy(stat, stat_start, sizeof(stat));
		normalize_stat_name(stat, sizeof(stat), stat);
		if (!node[0] || !zone[0] || !stat[0])
			continue;

		metric_add_labels3("zoneinfo_stat_total", &val, DATATYPE_UINT,
			ac->system_carg, "node", node, "zone", zone, "stat", stat);
	}
	fclose(fd);
}

void get_numa_meminfo_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/devices/system/node", ac->system_sysfs);
	DIR *dir = opendir(root);
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (strncmp(ent->d_name, "node", 4))
			continue;

		char path[768];
		snprintf(path, sizeof(path), "%s/%s/meminfo", root, ent->d_name);
		FILE *fd = fopen(path, "r");
		if (!fd)
			continue;

		char line[256];
		while (fgets(line, sizeof(line), fd)) {
			char *cur = line;
			if (!strncmp(cur, "Node", 4)) {
				cur += 4;
				cur += strspn(cur, " \t");
				cur += strcspn(cur, " \t");
				cur += strspn(cur, " \t");
			}
			char key[64];
			uint64_t val_kb = 0;
			if (sscanf(cur, "%63s %" SCNu64, key, &val_kb) != 2)
				continue;
			size_t klen = strlen(key);
			if (klen && key[klen - 1] == ':')
				key[klen - 1] = '\0';
			normalize_stat_name(key, sizeof(key), key);
			uint64_t val_bytes = val_kb * 1024ULL;
			metric_add_labels2("numa_meminfo_bytes", &val_bytes, DATATYPE_UINT,
				ac->system_carg, "node", ent->d_name, "type", key);
		}
		fclose(fd);

		snprintf(path, sizeof(path), "%s/%s/numastat", root, ent->d_name);
		fd = fopen(path, "r");
		if (!fd)
			continue;
		while (fgets(line, sizeof(line), fd)) {
			char key[64];
			uint64_t val = 0;
			if (sscanf(line, "%63s %" SCNu64, key, &val) != 2)
				continue;
			normalize_stat_name(key, sizeof(key), key);
			metric_add_labels2("numa_node_stat_total", &val, DATATYPE_UINT,
				ac->system_carg, "node", ent->d_name, "stat", key);
		}
		fclose(fd);
	}
	closedir(dir);
}

#endif
