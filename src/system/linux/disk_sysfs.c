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
#include "system/linux/disk_sysfs.h"

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

static void emit_fs_stat(const char *metric, const char *device_label, const char *device,
	const char *stat, const char *path)
{
	int64_t raw = getkvfile((char *)path);
	if (raw < 0)
		return;
	uint64_t val = (uint64_t)raw;
	metric_add_labels2((char *)metric, &val, DATATYPE_UINT,
		ac->system_carg, (char *)device_label, (char *)device, "stat", (char *)stat);
}

static void walk_one_level_stats(const char *base, const char *metric, const char *device_label)
{
	DIR *dir = opendir(base);
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		char devdir[1024];
		snprintf(devdir, sizeof(devdir), "%s/%s", base, ent->d_name);
		DIR *sdir = opendir(devdir);
		if (!sdir)
			continue;

		struct dirent *sf;
		while ((sf = readdir(sdir)) != NULL) {
			if (sf->d_name[0] == '.')
				continue;
			char stat[64];
			normalize_stat_name(stat, sizeof(stat), sf->d_name);
			char fpath[1200];
			snprintf(fpath, sizeof(fpath), "%s/%s", devdir, sf->d_name);
			emit_fs_stat(metric, device_label, ent->d_name, stat, fpath);
		}
		closedir(sdir);
	}
	closedir(dir);
}

static void walk_recursive_numeric(const char *base, const char *metric,
	const char *device_label, const char *device, int depth)
{
	if (depth > 3)
		return;

	DIR *dir = opendir(base);
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		char path[1200];
		snprintf(path, sizeof(path), "%s/%s", base, ent->d_name);

		DIR *sub = opendir(path);
		if (sub) {
			closedir(sub);
			walk_recursive_numeric(path, metric, device_label, device, depth + 1);
			continue;
		}

		char stat[128];
		normalize_stat_name(stat, sizeof(stat), ent->d_name);
		emit_fs_stat(metric, device_label, device, stat, path);
	}
	closedir(dir);
}

void get_xfs_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/fs/xfs", ac->system_sysfs);
	DIR *dir = opendir(root);
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		char stats[768];
		snprintf(stats, sizeof(stats), "%s/%s/stats", root, ent->d_name);
		walk_recursive_numeric(stats, "xfs_stat_total", "device", ent->d_name, 0);
	}
	closedir(dir);
}

void get_btrfs_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/fs/btrfs", ac->system_sysfs);
	walk_one_level_stats(root, "btrfs_stat_total", "uuid");
}

void get_bcache_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/fs/bcache", ac->system_sysfs);
	DIR *dir = opendir(root);
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		char devdir[768];
		snprintf(devdir, sizeof(devdir), "%s/%s", root, ent->d_name);
		walk_recursive_numeric(devdir, "bcache_stat_total", "uuid", ent->d_name, 0);
	}
	closedir(dir);
}

static int path_is_multipath_dm(const char *block_path)
{
	char uuid_path[768];
	snprintf(uuid_path, sizeof(uuid_path), "%s/dm/uuid", block_path);
	char uuid[256] = "";
	getkvfile_str(uuid_path, uuid, sizeof(uuid));
	return strstr(uuid, "mpath") != NULL || strstr(uuid, "multipath") != NULL;
}

void get_dmmultipath_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/block", ac->system_sysfs);
	DIR *dir = opendir(root);
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (strncmp(ent->d_name, "dm-", 3))
			continue;

		char block_path[768];
		snprintf(block_path, sizeof(block_path), "%s/%s", root, ent->d_name);
		if (!path_is_multipath_dm(block_path))
			continue;

		char size_path[900];
		snprintf(size_path, sizeof(size_path), "%s/size", block_path);
		int64_t sectors = getkvfile(size_path);
		if (sectors >= 0) {
			uint64_t bytes = (uint64_t)sectors * 512ULL;
			metric_add_labels2("dmmultipath_stat", &bytes, DATATYPE_UINT,
				ac->system_carg, "device", ent->d_name, "type", "size_bytes");
		}

		snprintf(size_path, sizeof(size_path), "%s/dm/suspended", block_path);
		int64_t suspended = getkvfile(size_path);
		if (suspended >= 0) {
			uint64_t active = suspended ? 0 : 1;
			metric_add_labels2("dmmultipath_stat", &active, DATATYPE_UINT,
				ac->system_carg, "device", ent->d_name, "type", "active");
		}

		snprintf(size_path, sizeof(size_path), "%s/slaves", block_path);
		FILE *sfd = fopen(size_path, "r");
		if (!sfd)
			continue;

		uint64_t paths = 0, paths_active = 0;
		char line[512];
		if (fgets(line, sizeof(line), sfd)) {
			char *slave = line;
			while (*slave) {
				slave += strspn(slave, " \t\n");
				if (!*slave)
					break;
				char *end = slave + strcspn(slave, " \t\n");
				if (*end)
					*end++ = '\0';
				++paths;

				char state_path[1024];
				snprintf(state_path, sizeof(state_path), "%s/%s/device/state", block_path, slave);
				char state[32] = "";
				getkvfile_str(state_path, state, sizeof(state));
				if (!strcmp(state, "running") || !strcmp(state, "live"))
					++paths_active;

				slave = end;
			}
		}
		fclose(sfd);

		metric_add_labels2("dmmultipath_stat", &paths, DATATYPE_UINT,
			ac->system_carg, "device", ent->d_name, "type", "paths");
		metric_add_labels2("dmmultipath_stat", &paths_active, DATATYPE_UINT,
			ac->system_carg, "device", ent->d_name, "type", "paths_active");
	}
	closedir(dir);
}

void get_tape_stats(void)
{
	char root[512];
	snprintf(root, sizeof(root), "%s/class/scsi_tape", ac->system_sysfs);
	DIR *dir = opendir(root);
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		char stats[768];
		snprintf(stats, sizeof(stats), "%s/%s/stats", root, ent->d_name);
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
			emit_fs_stat("tape_stat_total", "device", ent->d_name, stat, path);
		}
		closedir(sdir);
	}
	closedir(dir);
}

#endif
