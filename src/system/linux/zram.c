#ifdef __linux__

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "common/logs.h"
#include "system/linux/zram.h"

extern aconf *ac;

#define ZRAM_BD_UNIT 4096ULL

static int zram_is_device(const char *name)
{
	size_t i;

	if (!name || strncmp(name, "zram", 4) || !name[4])
		return 0;
	for (i = 4; name[i]; ++i) {
		if (!isdigit((unsigned char)name[i]))
			return 0;
	}
	return 1;
}

static size_t zram_read_u64_line(const char *path, uint64_t *out, size_t maxn)
{
	FILE *fd;
	char buf[512];
	char *cur;
	size_t n = 0;

	fd = fopen(path, "r");
	if (!fd)
		return 0;
	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return 0;
	}
	fclose(fd);

	cur = buf;
	while (n < maxn) {
		char *end = NULL;
		unsigned long long v;

		while (*cur == ' ' || *cur == '\t')
			++cur;
		if (!*cur || *cur == '\n' || *cur == '\r')
			break;
		errno = 0;
		v = strtoull(cur, &end, 10);
		if (end == cur || errno == ERANGE)
			break;
		out[n++] = (uint64_t)v;
		cur = end;
	}
	return n;
}

static void zram_emit_bytes(const char *device, const char *stat, uint64_t val)
{
	metric_add_labels2("zram_bytes", &val, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "stat", (char *)stat);
}

static void zram_emit_stat(const char *device, const char *stat, uint64_t val)
{
	metric_add_labels2("zram_stat", &val, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "stat", (char *)stat);
}

static void zram_emit_total(const char *device, const char *stat, uint64_t val)
{
	metric_add_labels2("zram_stat_total", &val, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "stat", (char *)stat);
}

static void zram_parse_mm_stat(const char *path, const char *device)
{
	uint64_t f[9];
	size_t n = zram_read_u64_line(path, f, 9);

	if (n < 5)
		return;

	zram_emit_bytes(device, "orig_data_size", f[0]);
	zram_emit_bytes(device, "compr_data_size", f[1]);
	zram_emit_bytes(device, "mem_used_total", f[2]);
	zram_emit_bytes(device, "mem_limit", f[3]);
	zram_emit_bytes(device, "mem_used_max", f[4]);
	if (n > 5)
		zram_emit_stat(device, "same_pages", f[5]);
	if (n > 6)
		zram_emit_total(device, "pages_compacted", f[6]);
	if (n > 7)
		zram_emit_stat(device, "huge_pages", f[7]);
	if (n > 8)
		zram_emit_total(device, "huge_pages_since", f[8]);
}

static void zram_parse_io_stat(const char *path, const char *device)
{
	uint64_t f[4];
	size_t n = zram_read_u64_line(path, f, 4);

	if (n < 4)
		return;
	zram_emit_total(device, "failed_reads", f[0]);
	zram_emit_total(device, "failed_writes", f[1]);
	zram_emit_total(device, "invalid_io", f[2]);
	zram_emit_total(device, "notify_free", f[3]);
}

static void zram_parse_bd_stat(const char *path, const char *device)
{
	uint64_t f[3];
	size_t n = zram_read_u64_line(path, f, 3);

	if (n < 3)
		return;
	zram_emit_bytes(device, "bd_count", f[0] * ZRAM_BD_UNIT);
	zram_emit_bytes(device, "bd_reads", f[1] * ZRAM_BD_UNIT);
	zram_emit_bytes(device, "bd_writes", f[2] * ZRAM_BD_UNIT);
}

static void zram_parse_disksize(const char *path, const char *device)
{
	uint64_t f[1];

	if (zram_read_u64_line(path, f, 1) < 1)
		return;
	zram_emit_bytes(device, "disksize", f[0]);
}

static void zram_parse_initstate(const char *path, const char *device)
{
	uint64_t f[1];

	if (zram_read_u64_line(path, f, 1) < 1)
		return;
	zram_emit_stat(device, "initstate", f[0]);
}

static void zram_parse_comp_algorithm(const char *path, const char *device)
{
	FILE *fd;
	char buf[256];
	char algo[64];
	char *lb;
	char *rb;
	size_t n;
	uint64_t one = 1;

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

	lb = strchr(buf, '[');
	rb = lb ? strchr(lb + 1, ']') : NULL;
	if (lb && rb && rb > lb + 1) {
		size_t alen = (size_t)(rb - lb - 1);
		if (alen >= sizeof(algo))
			alen = sizeof(algo) - 1;
		memcpy(algo, lb + 1, alen);
		algo[alen] = '\0';
	} else {
		char *tok = buf;
		size_t tlen;

		while (*tok == ' ' || *tok == '\t')
			++tok;
		tlen = strcspn(tok, " \t");
		if (tlen >= sizeof(algo))
			tlen = sizeof(algo) - 1;
		memcpy(algo, tok, tlen);
		algo[tlen] = '\0';
	}
	if (!algo[0])
		return;
	metric_add_labels2("zram_comp_algorithm", &one, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "algorithm", algo);
}

static void zram_read_device(const char *blockdir, const char *device)
{
	char path[1200];

	snprintf(path, sizeof(path), "%s/%s/mm_stat", blockdir, device);
	zram_parse_mm_stat(path, device);
	snprintf(path, sizeof(path), "%s/%s/io_stat", blockdir, device);
	zram_parse_io_stat(path, device);
	snprintf(path, sizeof(path), "%s/%s/bd_stat", blockdir, device);
	zram_parse_bd_stat(path, device);
	snprintf(path, sizeof(path), "%s/%s/disksize", blockdir, device);
	zram_parse_disksize(path, device);
	snprintf(path, sizeof(path), "%s/%s/initstate", blockdir, device);
	zram_parse_initstate(path, device);
	snprintf(path, sizeof(path), "%s/%s/comp_algorithm", blockdir, device);
	zram_parse_comp_algorithm(path, device);
}

void get_zram_stats(void)
{
	char root[1024];
	DIR *dir;
	struct dirent *ent;

	snprintf(root, sizeof(root), "%s/block", ac->system_sysfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: zram '%s'\n", root);

	dir = opendir(root);
	if (!dir)
		return;

	while ((ent = readdir(dir)) != NULL) {
		if (!zram_is_device(ent->d_name))
			continue;
		zram_read_device(root, ent->d_name);
	}
	closedir(dir);
}

#endif
