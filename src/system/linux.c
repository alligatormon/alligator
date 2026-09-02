#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <dirent.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include "main.h"
#include "common/pw.h"
#include "common/selector.h"
#include "common/rpm.h"
#include "system/common.h"
#include "common/deb.h"
#include "parsers/firewall.h"
#include "system/linux/parsers.h"
#include "metric/labels.h"
#include "smart/smart.h"
#include "dstructures/ht.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "cadvisor/run.h"
#include <utmp.h>
#include "system/fdescriptors.h"
#include "common/rtime.h"
#include "system/linux/systemd.h"
#include "system/linux/nftables.h"
#include "system/linux/disk.h"
#include "system/linux/cpu.h"
#include "system/linux/process.h"
#include "system/linux/ipmi.h"
#include "system/linux/pressure.h"
#include "system/linux/proc_net.h"
#include "system/linux/vm_stats.h"
#include "system/linux/host_misc.h"
#include "system/linux/disk_sysfs.h"
#include "system/linux/fabric_sysfs.h"
#include "system/linux/nvml.h"
#include "system/linux/dcgm.h"
#include "system/linux/amdgpu.h"
#define LINUXFS_LINE_LENGTH 300
#define d64 PRId64
#define LINUX_MEMORY 1
#define LINUX_CPU 2
extern aconf *ac;

static void system_scrape_fopen_fail(const char *path, int optional)
{
	int pri = (optional && (errno == ENOENT || errno == ENOTDIR)) ? L_DEBUG : L_ERROR;
	carglog(ac->system_carg, pri, "system scrape fopen %s: %s\n", path, strerror(errno));
}

void check_sockets_by_netlink(char *proto, uint8_t family, uint8_t pproto);

int is_baremetal_or_vm(int8_t platform) {
	return (!platform) || (platform > 4);
}

int is_baremetal(int8_t platform) {
	return (!platform);
}


void print_mount(const struct mntent *fs)
{
	if (!strcmp(fs->mnt_type,"tmpfs") || !strcmp(fs->mnt_type,"xfs") || !strcmp(fs->mnt_type,"ext4") || !strcmp(fs->mnt_type,"btrfs") || !strcmp(fs->mnt_type,"ext3") || !strcmp(fs->mnt_type,"ext2") || !strcmp(fs->mnt_dir, "/"))
	{
		if (!strncmp(fs->mnt_dir, "/dev", 4) || !strncmp(fs->mnt_dir, "/proc", 5) || !strncmp(fs->mnt_dir, "/sys", 4) || !strncmp(fs->mnt_dir, "/run", 4) || !strncmp(fs->mnt_type, "overlay", 7))
			return;

		int f_d = 0;
		f_d = open(fs->mnt_dir,O_RDONLY);
		if(-1 == f_d)
		{
			if (errno == ENOENT || errno == EACCES)
				carglog(ac->system_carg, L_DEBUG, "system scrape open %s: %s\n", fs->mnt_dir, strerror(errno));
			else
				carglog(ac->system_carg, L_ERROR, "system scrape open %s: %s\n", fs->mnt_dir, strerror(errno));
		}
		else
		{
			struct statvfs buf;

			if (statvfs(fs->mnt_dir, &buf) == -1)
				carglog(ac->system_carg, L_ERROR, "statvfs() error on '%s': %s\n", fs->mnt_dir, strerror(errno));
			else
			{
				int64_t total = ((double)buf.f_blocks * buf.f_bsize);
				int64_t avail = ((double)buf.f_bavail * buf.f_bsize);
				//int64_t free = ((double)buf.f_bfree * buf.f_bsize);
				int64_t used = total - avail;
				int64_t inodes_total = buf.f_files;
				int64_t inodes_avail = buf.f_favail;
				int64_t inodes_used = inodes_total-inodes_avail;
				double pused = (double)used*100/(double)total;
				double pfree = 100 - pused;

				double iused = inodes_used*100.0/inodes_total;
				double ifree = 100.0 - iused;
				metric_add_labels2("disk_usage", &total, DATATYPE_INT, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "total");
				metric_add_labels2("disk_usage", &avail, DATATYPE_INT, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "free");
				metric_add_labels2("disk_usage", &used, DATATYPE_INT, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "used");
				metric_add_labels2("disk_usage_percent", &pused, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "used");
				metric_add_labels2("disk_usage_percent", &pfree, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "free");

				metric_add_labels2("disk_inodes", &inodes_avail, DATATYPE_INT, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "free");
				metric_add_labels2("disk_inodes", &inodes_used, DATATYPE_INT, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "used");
				metric_add_labels2("disk_inodes", &inodes_total, DATATYPE_INT, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "total");
				metric_add_labels2("disk_inodes_percent", &iused, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "used");
				metric_add_labels2("disk_inodes_percent", &ifree, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", fs->mnt_dir, "type", "free");

				int64_t one = 1;
				metric_add_labels2("disk_filesystem", &one, DATATYPE_INT, ac->system_carg, "mountpoint", fs->mnt_dir, "fs", fs->mnt_type);
			}

			close(f_d);
		}
	}
}

void get_disk()
{
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: disk: get_stat\n");

	FILE *fp;
	struct mntent *fs;

	fp = setmntent("/etc/mtab", "r");
	if (fp == NULL) {
		carglog(ac->system_carg, L_ERROR, "could not open /etc/mtab\n");
		return;
	}

	while ((fs = getmntent(fp)) != NULL)
		print_mount(fs);

	endmntent(fp);
}

/* Split "key value..." from a procfs line. strip_colon copies dest size i (drops
 * trailing ':') for /proc/meminfo; otherwise dest is i+1. Stops on NUL. */
static int linux_proc_kv_split(char *tmp, char *key, size_t keysz, char *val, size_t valsz, int strip_colon)
{
	size_t n = strlen(tmp);
	if (!n)
		return 0;
	if (tmp[n - 1] == '\n' || tmp[n - 1] == '\r')
		tmp[--n] = 0;
	if (!n)
		return 0;

	size_t i = strcspn(tmp, " ");
	if (!i)
		return 0;

	size_t key_dest = strip_colon ? i : i + 1;
	if (key_dest > keysz)
		key_dest = keysz;
	strlcpy(key, tmp, key_dest);

	while (i < n && tmp[i] == ' ')
		++i;
	size_t swap = i;
	i += strcspn(tmp + i, " ");
	size_t val_dest = (i - swap) + 1;
	if (val_dest > valsz)
		val_dest = valsz;
	strlcpy(val, tmp + swap, val_dest);
	return 1;
}


void get_mem(int8_t platform)
{
	int is_cgroup = is_container(platform); // exclude baremetal and virt
	int is_bm_vm = is_baremetal_or_vm(platform); // exclude baremetal and virt
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: mem\n");

	char pathbuf[255];
	snprintf(pathbuf, 255, "%s/meminfo", ac->system_procfs);

	FILE *fd = fopen(pathbuf, "r");
	if (!fd) {
		system_scrape_fopen_fail(pathbuf, 0);
		return;
	}

	char tmp[LINUXFS_LINE_LENGTH];
	char key[LINUXFS_LINE_LENGTH];
	char key_map[LINUXFS_LINE_LENGTH];
	char val[LINUXFS_LINE_LENGTH];
	int64_t ival;
	int64_t oom_kill = 0;
	int64_t totalswap = 1;
	int64_t freeswap = 1;
	int64_t memtotal = 0;
	//int64_t memfree = 0;
	int64_t memavailable = 0;
	int64_t inactive_anon = 0;
	int64_t active_anon = 0;
	int64_t inactive_file = 0;
	int64_t active_file = 0;
	int64_t inactive = 0;
	int64_t active = 0;
	int64_t cache = 0;
	int64_t mapped = 0;
	int64_t unevictable = 0;
	int64_t shmem = 0;
	int64_t dirty = 0;
	int64_t pgpgin = 0;
	int64_t pgpgout = 0;
	int64_t pgmajfault = 0;
	int64_t pgfault = 0;
	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		if (!linux_proc_kv_split(tmp, key, sizeof(key), val, sizeof(val), 1))
			continue;

		ival = atoll(val);
		if (strstr(tmp, "kB"))
			ival *= 1024;

		if	( !strcmp(key, "MemTotal") ) {
			strlcpy(key_map, "total", 6);
			memtotal = ival;
		}
		//else if ( !strcmp(key, "MemFree") ) {
		//	strlcpy(key_map, "free", 5);
		//	memfree = ival;
		//}
		else if ( !strcmp(key, "MemAvailable") ) {
			strlcpy(key_map, "available", sizeof(key_map));
			memavailable = ival;
		}
		else if ( !strcmp(key, "Inactive") ) {
			strlcpy(key_map, "inactive", sizeof(key_map));
			inactive = ival;
		}
		else if ( !strcmp(key, "Active") ) {
			strlcpy(key_map, "active", sizeof(key_map));
			active = ival;
		}
		else if ( !strcmp(key, "Inactive(anon)") ) {
			strlcpy(key_map, "inactive_anon", sizeof(key_map));
			inactive_anon = ival;
		}
		else if ( !strcmp(key, "Active(anon)") ) {
			strlcpy(key_map, "active_anon", sizeof(key_map));
			active_anon = ival;
		}
		else if ( !strcmp(key, "Inactive(file)") ) {
			strlcpy(key_map, "inactive_anon", sizeof(key_map));
			inactive_file = ival;
		}
		else if ( !strcmp(key, "Active(file)") ) {
			strlcpy(key_map, "active_anon", sizeof(key_map));
			active_file = ival;
		}
		else if ( !strcmp(key, "SwapTotal") ) {
			strlcpy(key_map, "swap_total", sizeof(key_map));
			totalswap = ival;
		}
		else if ( !strcmp(key, "SwapFree") ) {
			strlcpy(key_map, "swap_free", sizeof(key_map));
			freeswap = ival;
		}
		else if ( !strcmp(key, "Buffers") ) {
			strlcpy(key_map, "buffers", sizeof(key_map));
		}
		else if ( !strcmp(key, "Cached") ) {
			strlcpy(key_map, "cache", sizeof(key_map));
			cache = ival;
		}
		else if ( !strcmp(key, "Mapped") ) {
			strlcpy(key_map, "mapped", sizeof(key_map));
			mapped = ival;
		}
		else if ( !strcmp(key, "Unevictable") ) {
			strlcpy(key_map, "unevictable", sizeof(key_map));
			unevictable = ival;
		}
		else if ( !strcmp(key, "Shmem") ) {
			strlcpy(key_map, "shmem", sizeof(key_map));
			shmem = ival;
		}
		else	continue;

		metric_add_labels("memory_usage_hw", &ival, DATATYPE_INT, ac->system_carg, "type", key_map);
	}
	int64_t usageswap = totalswap - freeswap;
	int64_t usagemem = memtotal - memavailable;
	metric_add_labels("memory_usage_hw", &usageswap, DATATYPE_INT, ac->system_carg, "type", "swap_usage");
	metric_add_labels("memory_usage_hw", &usagemem, DATATYPE_INT, ac->system_carg, "type", "usage");
	
	fclose(fd);

	snprintf(pathbuf, 255, "%s/vmstat", ac->system_procfs);
	fd = fopen(pathbuf, "r");
	if (!fd)
		return;

	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		if (!linux_proc_kv_split(tmp, key, sizeof(key), val, sizeof(val), 0))
			continue;

		ival = atoll(val);
		if (!strcmp(key, "pgpgin"))
			pgpgin = ival;
		else if (!strcmp(key, "pgpgout"))
			pgpgout = ival;
		else if (!strcmp(key, "pgmajfault"))
			pgmajfault = ival;
		else if (!strcmp(key, "pgfault"))
			pgfault = ival;
		else if (!strcmp(key, "oom_kill"))
			oom_kill = ival;
		else if (is_bm_vm && !strcmp(key, "pswpin"))
			metric_add_labels("memory_stat", &ival, DATATYPE_INT, ac->system_carg, "type", "pswpin");
		else if (is_bm_vm && !strcmp(key, "pswpout"))
			metric_add_labels("memory_stat", &ival, DATATYPE_INT, ac->system_carg, "type", "pswpout");
		else if (is_bm_vm && !strncmp(key, "numa_", 5))
			metric_add_labels("numa_stat", &ival, DATATYPE_INT, ac->system_carg, "type", key+5);
		else if (is_bm_vm && !strncmp(key, "pgscan_", 7))
			metric_add_labels("pgscan_total", &ival, DATATYPE_INT, ac->system_carg, "type", key+7);
		else if (is_bm_vm && !strncmp(key, "pgsteal_", 8))
			metric_add_labels("pgsteal_total", &ival, DATATYPE_INT, ac->system_carg, "type", key+8);
		else if (is_bm_vm && !strncmp(key, "kswapd_", 7))
			metric_add_labels("pgsteal_total", &ival, DATATYPE_INT, ac->system_carg, "type", key+7);
	}
	fclose(fd);

	// scrape cgroup
	snprintf(pathbuf, 255, "%s/fs/cgroup/memory/memory.stat", ac->system_sysfs);
	fd = fopen(pathbuf, "r");
	if (!fd)
	{
		snprintf(pathbuf, 255, "%s/fs/cgroup/memory.stat", ac->system_sysfs);
		fd = fopen(pathbuf, "r");
		if (!fd)
			return;
	}

	ival = 1;
	uint64_t container_memory_usage = 0;
	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		if (!linux_proc_kv_split(tmp, key, sizeof(key), val, sizeof(val), 0))
			continue;

		ival = atoll(val);

		if	(!strcmp(key, "total_cache")) {
			strlcpy(key_map, "cache", sizeof(key_map));
			cache = is_cgroup ? ival : cache;
		}
		else if (!strcmp(key, "total_mapped_file")) {
			strlcpy(key_map, "mapped", sizeof(key_map));
			mapped = is_cgroup ? ival : mapped;
		}
		else if (!strcmp(key, "total_dirty")) {
			strlcpy(key_map, "dirty", sizeof(key_map));
			dirty = is_cgroup ? ival : dirty;
			container_memory_usage += dirty;
		}
		else if (!strcmp(key, "total_unevictable")) {
			strlcpy(key_map, "unevictable", sizeof(key_map));
			unevictable = is_cgroup ? ival : unevictable;
		}
		else if (!strcmp(key, "total_active_anon")) {
			strlcpy(key_map, "active_anon", sizeof(key_map));
			active_anon = is_cgroup ? ival : active_anon;
			container_memory_usage += active_anon;
		}
		else if (!strcmp(key, "total_inactive_anon")) {
			strlcpy(key_map, "inactive_anon", sizeof(key_map));
			inactive_anon = is_cgroup ? ival : inactive_anon;
			container_memory_usage += inactive_anon;
		}
		else if (!strcmp(key, "total_active_file")) {
			strlcpy(key_map, "active_file", sizeof(key_map));
			active_file = is_cgroup ? ival : active_file;
		}
		else if (!strcmp(key, "total_inactive_file")) {
			strlcpy(key_map, "inactive_file", sizeof(key_map));
			inactive_file = is_cgroup ? ival : inactive_file;
		}
		else if (!strcmp(key, "total_pgpgin")) {
			strlcpy(key_map, "pgpgin", sizeof(key_map));
			pgpgin = is_cgroup ? ival : pgpgin;
		}
		else if (!strcmp(key, "total_pgpgout")) {
			strlcpy(key_map, "pgpgout", sizeof(key_map));
			pgpgout = is_cgroup ? ival : pgpgout;
		}
		else if (!strcmp(key, "total_pgfault")) {
			strlcpy(key_map, "pgfault", sizeof(key_map));
			pgfault = is_cgroup ? ival : pgfault;
		}
		else if (!strcmp(key, "total_pgmajfault")) {
			strlcpy(key_map, "pgmajfault", sizeof(key_map));
			pgmajfault = is_cgroup ? ival : pgmajfault;
		}
		else if (!strcmp(key, "hierarchical_memory_limit")) {
			strlcpy(key_map, "total", sizeof(key_map));
			memtotal = memtotal > ival ? ival : memtotal;
		}
		else if (!strcmp(key, "total_shmem")) {
			strlcpy(key_map, "shmem", sizeof(key_map));
			shmem = is_cgroup ? ival : shmem;
			container_memory_usage += shmem;
		}
		else	continue;

		metric_add_labels("memory_usage_cgroup", &ival, DATATYPE_INT, ac->system_carg, "type", key_map);
	}
	fclose(fd);

	metric_add_labels("memory_usage", &cache, DATATYPE_INT, ac->system_carg, "type", "cache");
	metric_add_labels("memory_usage", &mapped, DATATYPE_INT, ac->system_carg, "type", "mapped");
	metric_add_labels("memory_usage", &dirty, DATATYPE_INT, ac->system_carg, "type", "dirty");
	metric_add_labels("memory_usage", &unevictable, DATATYPE_INT, ac->system_carg, "type", "unevictable");
	metric_add_labels("memory_usage", &inactive_anon, DATATYPE_INT, ac->system_carg, "type", "inactive_anon");
	metric_add_labels("memory_usage", &active_anon, DATATYPE_INT, ac->system_carg, "type", "active_anon");
	metric_add_labels("memory_usage", &active_file, DATATYPE_INT, ac->system_carg, "type", "active_file");
	metric_add_labels("memory_usage", &inactive_file, DATATYPE_INT, ac->system_carg, "type", "inactive_file");
	metric_add_labels("memory_stat", &pgpgin, DATATYPE_INT, ac->system_carg, "type", "pgpgin");
	metric_add_labels("memory_stat", &pgmajfault, DATATYPE_INT, ac->system_carg, "type", "pgmajfault");
	metric_add_labels("memory_stat", &pgfault, DATATYPE_INT, ac->system_carg, "type", "pgfault");
	metric_add_labels("memory_stat", &pgpgout, DATATYPE_INT, ac->system_carg, "type", "pgpgout");
	metric_add_labels("memory_stat", &shmem, DATATYPE_INT, ac->system_carg, "type", "shmem");

	inactive = inactive_file+inactive_anon;
	active = active_file+active_anon;
	metric_add_labels("memory_usage", &active, DATATYPE_INT, ac->system_carg, "type", "active");
	metric_add_labels("memory_usage", &inactive, DATATYPE_INT, ac->system_carg, "type", "inactive");

	usagemem = is_cgroup ? container_memory_usage : usagemem;
	if (memtotal > 0) {
		double percentused = (double)usagemem * 100.0 / (double)memtotal;
		double percentfree = 100.0 - percentused;
		metric_add_labels("memory_usage_percent", &percentused, DATATYPE_DOUBLE, ac->system_carg, "type", "used");
		metric_add_labels("memory_usage_percent", &percentfree, DATATYPE_DOUBLE, ac->system_carg, "type", "free");
	}
	metric_add_labels("memory_usage", &usagemem, DATATYPE_INT, ac->system_carg, "type", "usage");
	metric_add_labels("memory_usage", &memtotal, DATATYPE_INT, ac->system_carg, "type", "total");

	// check oom control
	snprintf(pathbuf, 255, "%s/fs/cgroup/memory/memory.oom_control", ac->system_sysfs);
	fd = fopen(pathbuf, "r");
	if (fd)
	{
		while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
		{
			if (!strncmp(tmp, "oom_kill", 8))
			{
				char *tmp2 = tmp+8 + strspn(tmp + 8, " \t\r\n");
				oom_kill = strtoll(tmp2, NULL, 10);
			}
		}
		metric_add_auto("oom_kill", &oom_kill, DATATYPE_INT, ac->system_carg);

		fclose(fd);
	}
}

void throttle_stat()
{
	int64_t mval;
	char throttle_path[255];

	snprintf(throttle_path, 255, "%s/fs/cgroup/memory/memory.memsw.failcnt", ac->system_sysfs);
	mval = getkvfile(throttle_path);
	metric_add_labels("memory_cgroup_fails", &mval, DATATYPE_INT, ac->system_carg, "type", "memsw");

	snprintf(throttle_path, 255, "%s/fs/cgroup/memory/memory.failcnt", ac->system_sysfs);
	mval = getkvfile(throttle_path);
	metric_add_labels("memory_cgroup_fails", &mval, DATATYPE_INT, ac->system_carg, "type", "mem");

	snprintf(throttle_path, 255, "%s/fs/cgroup/memory/memory.kmem.failcnt", ac->system_sysfs);
	mval = getkvfile(throttle_path);
	metric_add_labels("memory_cgroup_fails", &mval, DATATYPE_INT, ac->system_carg, "type", "kmem");

	snprintf(throttle_path, 255, "%s/fs/cgroup/memory/memory.kmem.tcp.failcnt", ac->system_sysfs);
	mval = getkvfile(throttle_path);
	metric_add_labels("memory_cgroup_fails", &mval, DATATYPE_INT, ac->system_carg, "type", "kmem_tcp");

	char *tmp;
	char buf[1000];
	
	snprintf(throttle_path, 255, "%s/fs/cgroup/cpu,cpuacct/cpu.stat", ac->system_sysfs);
	FILE *fd = fopen(throttle_path, "r");
	if (!fd)
	{
		snprintf(throttle_path, 255, "%s/fs/cgroup/cpu/cpu.stat", ac->system_sysfs);
		fd = fopen(throttle_path, "r");
		if (!fd)
			return;
	}

	while (fgets(buf, 1000, fd))
	{
		tmp = buf;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		if (!strncmp(buf, "nr_periods", 10))
		{
			uint64_t val = strtoull(tmp, NULL, 10);
			metric_add_auto("cpu_cgroup_periods_total", &val, DATATYPE_UINT, ac->system_carg);
		}
		else if (!strncmp(buf, "nr_throttled", 12))
		{
			uint64_t val = strtoull(tmp, NULL, 10);
			metric_add_auto("cpu_cgroup_throttled_periods_total", &val, DATATYPE_UINT, ac->system_carg);
		}
		else if (!strncmp(buf, "throttled_time", 14))
		{
			double val = strtof(tmp, NULL) / 1000000000.0;
			metric_add_auto("cpu_cgroup_throttled_seconds_total", &val, DATATYPE_DOUBLE, ac->system_carg);
		}
	}
	fclose(fd);
}

void get_netstat_statistics(char *ns_file)
{
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: netstat_statistics '%s'\n", ns_file);

	FILE *fp = fopen(ns_file, "r");
	if(!fp) {
		system_scrape_fopen_fail(ns_file, 1);
		return;
	}

	size_t filesize = 10000;
	char bufheader[filesize];
	char bufbody[filesize];
	uint64_t header_index, body_index, header_update;
	int64_t buf_val;
	char buf[255], proto[255];

	while (1)
	{
		if (!fgets(bufheader, filesize, fp))
			break;
		if (!fgets(bufbody, filesize, fp))
			break;

		size_t header_size = strlen(bufheader);
		size_t body_size = strlen(bufbody);

		header_update = strcspn(bufheader, " \t");
		strlcpy(proto, bufheader, header_update);
		header_update += strspn(bufheader+header_update, " \t");
		header_index = header_update;

		body_index = strcspn(bufbody, " \t");

		for (; header_index<header_size && body_index<body_size; )
		{
			header_update = strcspn(bufheader+header_index, " \t\n");
			strlcpy(buf, bufheader+header_index, header_update+1);
			header_update += strspn(bufheader+header_index+header_update, " \t\n");
			header_index += header_update;

			body_index += strcspn(bufbody+body_index, " \t");
			buf_val = atoll(bufbody+body_index);
			body_index += strspn(bufbody+body_index, " \t");

			//printf("%s:%s: %lld\n", proto, buf, buf_val);
			metric_add_labels2("network_stat_total", &buf_val, DATATYPE_INT, ac->system_carg, "proto", proto, "stat", buf);
		}
	}
	fclose(fp);
}


void get_nofile_stat()
{
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: nofile_stat\n");

	char filenr[255];
	snprintf(filenr, 255, "%s/sys/fs/file-nr", ac->system_procfs);
	FILE *fd = fopen(filenr, "r");
	if (!fd) {
		system_scrape_fopen_fail(filenr, 0);
		return;
	}

	char buf[LINUXFS_LINE_LENGTH];
	if(!fgets(buf, LINUXFS_LINE_LENGTH, fd))
	{
		fclose(fd);
		return;
	}
	int64_t stat[3];

	int64_t file_open = 0;
	int64_t kern_file_max = 0;
	int64_t i, j;
	size_t len = strlen(buf);
	for (i=0, j=0; i<len; i++, j++)
	{
		int64_t val = atoll(buf+i);
		stat[j] = val;

		i += strcspn(buf+i, " \t");
	}
	if (j>0)
	{
		file_open = stat[0];
		metric_add_auto("open_files_system", &file_open, DATATYPE_INT, ac->system_carg);
	}
	if (j>2)
	{
		kern_file_max = stat[2];
		metric_add_auto("max_files", &kern_file_max, DATATYPE_INT, ac->system_carg);
	}
	fclose(fd);
}

void get_disk_io_stat()
{
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: disk: io_stat\n");

	char diskstats[255];
	snprintf(diskstats, 255, "%s/diskstats", ac->system_procfs);
	FILE *fd = fopen(diskstats, "r");
	if (!fd) {
		system_scrape_fopen_fail(diskstats, 0);
		return;
	}

	int64_t stat[15];
	char buf[LINUXFS_LINE_LENGTH];
	while ( fgets(buf, LINUXFS_LINE_LENGTH, fd) )
	{
		int64_t i, j;
		size_t len = strlen(buf) - 1;
		buf[len] = '\0';
		char devname[50];
		for (i=0, j=0; i<len && j<15; i++, j++)
		{
			while (buf[i] && buf[i] == ' ')
				i++;
			int64_t val = atoll(buf+i);
			stat[j] = val;
			if ( j == 2 )
			{
				int64_t offset = strcspn(buf+i, " \t")+1;
				strlcpy(devname, buf+i, offset);
			}
			i += strcspn(buf+i, " \t");
		}
		if (j<10)
			continue;

		char bldevname[UCHAR_MAX];
		snprintf(bldevname, UCHAR_MAX, "%s/block/%s/queue/hw_sector_size", ac->system_sysfs, devname);
		int64_t sectorsize = getkvfile(bldevname);

		int64_t read_bytes = stat[5] * sectorsize;
		int64_t write_bytes = stat[9] * sectorsize;
		int64_t io_w = stat[7];
		int64_t io_r = stat[3];
		int64_t disk_busy = stat[12]/1000;
		metric_add_labels2("disk_io", &io_r, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "transfers_read");
		metric_add_labels2("disk_io", &io_w, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "transfers_write");
		metric_add_labels2("disk_io", &read_bytes, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "bytes_read");
		metric_add_labels2("disk_io", &write_bytes, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "bytes_write");
		int64_t await_write_sec = stat[10] / 1000;
		int64_t await_read_sec = stat[6] / 1000;
		int64_t await_queue_sec = stat[13] / 1000;
		metric_add_labels2("disk_io_await_seconds_total", &await_write_sec, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "write");
		metric_add_labels2("disk_io_await_seconds_total", &await_read_sec, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "read");
		metric_add_labels2("disk_io_await_seconds_total", &await_queue_sec, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "queue");
		metric_add_labels("disk_busy", &disk_busy, DATATYPE_INT, ac->system_carg, "dev", devname);
		if (j>14)
			metric_add_labels2("disk_io", &stat[14], DATATYPE_INT, ac->system_carg, "dev", devname, "type", "transfers_discard");
	}
	fclose(fd);
}

void get_loadavg()
{
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: loadavg\n");

	char loadavg[255];
	snprintf(loadavg, 255, "%s/loadavg", ac->system_procfs);
	FILE *fd = fopen(loadavg, "r");
	if (!fd) {
		system_scrape_fopen_fail(loadavg, 0);
		return;
	}

	char str[LINUXFS_LINE_LENGTH];
	if(!fgets(str, LINUXFS_LINE_LENGTH, fd))
	{
		fclose(fd);
		return;
	}
	double load1, load5, load15;
	if (sscanf(str, "%lf %lf %lf", &load1, &load5, &load15) != 3) {
		fclose(fd);
		return;
	}
	metric_add_labels("load_average", &load1, DATATYPE_DOUBLE, ac->system_carg, "type", "load1");
	metric_add_labels("load_average", &load5, DATATYPE_DOUBLE, ac->system_carg, "type", "load5");
	metric_add_labels("load_average", &load15, DATATYPE_DOUBLE, ac->system_carg, "type", "load15");

	fclose(fd);
}

void get_uptime()
{
	r_time time1 = setrtime();

	char firstproc[255];
	snprintf(firstproc, 255, "%s/1", ac->system_procfs);
	uint64_t uptime = time1.sec - get_file_atime(firstproc);
	metric_add_auto("system_uptime_seconds", &uptime, DATATYPE_UINT, ac->system_carg);
}

void get_mdadm()
{
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: disk: mdadm\n");

	char mdstat[255];
	snprintf(mdstat, 255, "%s/mdstat", ac->system_procfs);
	FILE *fd;
	fd = fopen(mdstat, "r");
	if (!fd) {
		system_scrape_fopen_fail(mdstat, 1);
		return;
	}

	char str1[LINUXFS_LINE_LENGTH];
	char str2[LINUXFS_LINE_LENGTH];
	char *tmp;
	if(!fgets(str1, LINUXFS_LINE_LENGTH, fd))
	{
		fclose(fd);
		return;
	}

	while(1)
	{
		*str1 = 0;
		while ( !(tmp = strstr(str1, " : ")) )
		{
			if(!fgets(str1, LINUXFS_LINE_LENGTH, fd))
			{
				fclose(fd);
				return;
			}
		}
		if(!fgets(str2, LINUXFS_LINE_LENGTH, fd))
		{
			fclose(fd);
			return;
		}

		char name[LINUXFS_LINE_LENGTH];
		char status[LINUXFS_LINE_LENGTH];
		char level[LINUXFS_LINE_LENGTH];
		char dev[LINUXFS_LINE_LENGTH];
		int64_t vl = 1;
		int64_t nvl = 0;
		uint64_t pt;
		strlcpy(name, str1, tmp-str1+1);

		tmp += 3;
		pt = strcspn(tmp, " ");
		strlcpy(status, tmp, pt+1);
		tmp += pt;
		tmp += strspn(tmp, " ");

		pt = strcspn(tmp, " ");
		strlcpy(level, tmp, pt+1);
		tmp += pt;
		tmp += strspn(tmp, " ");
		//printf("name is '%s', status is '%s', level is '%s'\n", name, status, level);

		size_t str2_size = strlen(str2);

		while (*tmp)
		{
			pt = strcspn(tmp, " [\n");
			strlcpy(dev, tmp, pt+1);
			tmp += pt;
			tmp += strspn(tmp, " [1234567890]\n");
			//printf("dev '%s'\n", dev);
			metric_add_labels2("raid_part", &vl, DATATYPE_INT, ac->system_carg, "array", name, "dev", dev);
		}

		pt = strcspn(str2, " ");
		int64_t size = atoll(str2+pt);
		int64_t arr_sz = 0;
		int64_t arr_cur = 0;
		tmp = str2+pt;

		pt = strcspn(tmp, "[")+1;
		if (pt < str2_size)
		{
			arr_sz = atoll(tmp+pt);
			tmp = str2+pt;

			pt = strcspn(tmp, "/")+1;
			if (pt < str2_size - (tmp - str2))
				arr_cur = atoll(tmp+pt);
		}

		//printf("size %lld, sz %lld, cur %lld\n", size, arr_sz, arr_cur);
		metric_add_labels3("raid_configuration", &vl, DATATYPE_INT, ac->system_carg, "array", name, "status", status, "level", level);
		metric_add_labels("raid_size_bytes", &size, DATATYPE_INT, ac->system_carg, "array", name);
		metric_add_labels2("raid_devices", &arr_cur, DATATYPE_INT, ac->system_carg, "array", name, "type", "current");
		metric_add_labels2("raid_devices", &arr_sz, DATATYPE_INT, ac->system_carg, "array", name, "type", "full");

		if (arr_cur >= arr_sz && !strcmp(status, "active"))
			metric_add_labels("raid_status", &vl, DATATYPE_INT, ac->system_carg, "array", name);
		else
			metric_add_labels("raid_status", &nvl, DATATYPE_INT, ac->system_carg, "array", name);
	}
}

int8_t get_platform(int8_t mode)
{
	if (ac->system_platform == PLATFORM_OPENSTACK) {
		uint64_t okval = 1;
		metric_add_labels("server_platform", &okval, DATATYPE_UINT, ac->system_carg, "platform", "openstack");
		carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: platform %d: openstack\n", mode);
		return PLATFORM_OPENSTACK;
	}
	else if (ac->system_platform == PLATFORM_KVM) {
		uint64_t okval = 1;
		metric_add_labels("server_platform", &okval, DATATYPE_UINT, ac->system_carg, "platform", "kvm");
		carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: platform %d: kvm\n", mode);
		return PLATFORM_KVM;
	}

	int64_t vl = 1;

	char procpath[255];
	snprintf(procpath, 255, "%s/1/cgroup", ac->system_procfs);
	FILE *env = fopen(procpath, "r");
	if (!env)
		return 0;

	char env_str[LINUXFS_LINE_LENGTH];
	while(fgets(env_str, LINUXFS_LINE_LENGTH, env))
	{
		if (strstr(env_str, "nspawn"))
		{
			if (mode)
				metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", "nspawn");
			carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: platform %d: nspawn\n", mode);
			fclose(env);
			return PLATFORM_NSPAWN;
		}
		else if (strstr(env_str, "docker"))
		{
			if (mode)
				metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", "docker");
			carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: platform %d: docker\n", mode);
			fclose(env);
			return PLATFORM_DOCKER;
		}
	}
	fclose(env);

	uint8_t mbopenvz = 0;
	snprintf(procpath, 255, "%s/1/environ", ac->system_procfs);
	env = fopen(procpath, "r");
	if (!env)
		return 0;
	if (!fgets(env_str, LINUXFS_LINE_LENGTH, env))
	{
		fclose(env);
		return 0;
	}
	if (strstr(env_str, "container=lxc"))
	{
		fclose(env);
		if (mode)
			metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", "lxc");
		carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: platform %d: lxc\n", mode);
		return PLATFORM_LXC;
	}
	fclose(env);

	snprintf(procpath, 255, "%s/user_beancounters", ac->system_procfs);
	env = fopen(procpath, "r");
	if (env)
	{
		mbopenvz = 1;
	}

	snprintf(procpath, 255, "%s/vz/version", ac->system_procfs);
	env = fopen(procpath, "r");
	if (!env)
	{
		if (mbopenvz)
		{
			if (mode)
				metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", "openvz");
			carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: platform %d: openvz\n", mode);
			return PLATFORM_OPENVZ;
		}
		else
		{
			if (mode)
				metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", "bare-metal");
			carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: platform %d: bare-metal\n", mode);
			return 0;
		}
	}
	else
	{
		if (mode)
			metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", "bare-metal");

		fclose(env);
		carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: platform %d: bare-metal\n", mode);
		return PLATFORM_BAREMETAL;
	}
}


void get_thermal()
{
	char fname[1024];
	char monname[512];
	char devname[255];
	char *tmp;
	char name[255];
	int64_t temp;
	struct dirent *entry;
	DIR *dp;

	char hwmonpath[255];
	snprintf(hwmonpath, 255, "%s/class/hwmon/", ac->system_sysfs);
	dp = opendir(hwmonpath);
	if (!dp)
		return;

	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;
	
		struct dirent *monentry;
		DIR *mondp;

		snprintf(monname, 511, "%s/class/hwmon/%s/", ac->system_sysfs, entry->d_name);
		mondp = opendir(monname);
		if (!mondp)
			continue;

		// get device name
		snprintf(fname, 1023, "%s/name", monname);
		FILE *fd = fopen(fname, "r");
		if(!fd)
		{
			closedir(mondp);
			continue;
		}
		if(!fgets(name, 255, fd))
		{
			closedir(mondp);
			fclose(fd);
			continue;
		}
		name[strlen(name)-1] = 0;
		fclose(fd);


		while((monentry = readdir(mondp)))
		{
			if ( monentry->d_name[0] == '.' )
				continue;

			if ((tmp = strstr(monentry->d_name, "_label")))
			{
				// get component name
				snprintf(fname, 1023, "%s/%s", monname, monentry->d_name);
				FILE *fd = fopen(fname, "r");
				if(!fd)
					continue;
				if(!fgets(devname, 255, fd))
				{
					fclose(fd);
					continue;
				}
				devname[strlen(devname)-1] = 0;
				fclose(fd);

				strlcpy(fname+strlen(monname)+(tmp-monentry->d_name)+1, "_input", 7);
				temp = getkvfile(fname);
				/* hwmon temp_input is millidegrees Celsius */
				int64_t temp_c = temp / 1000;
				metric_add_labels3("core_temperature_celsius", &temp_c, DATATYPE_INT, ac->system_carg, "name", name, "component", devname, "hwmon", entry->d_name);
			}
		}

		closedir(mondp);
	}

	closedir(dp);
}

void get_smart_info()
{
	char partitions[255];
	snprintf(partitions, 255, "%s/partitions", ac->system_procfs);
	FILE *fd = fopen(partitions, "r");
	if (!fd)
		return;

	char buf[1000];
	char dev[255];
	strlcpy(dev, "/dev/", 6);
	uint64_t cur;
	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	while (fgets(buf, 1000, fd))
	{
		cur = strcspn(buf, " ");
		cur += strspn(buf+cur, " ");
		cur += strcspn(buf+cur, " ");
		cur += strspn(buf+cur, " ");
		cur += strcspn(buf+cur, " ");
		cur += strspn(buf+cur, " ");
		cur += strcspn(buf+cur, " ");
		cur += strspn(buf+cur, " ");
		size_t len = strlen(buf+cur);
		if (isdigit(buf[cur+len-2]))
			continue;

		strlcpy(dev+5, buf+cur, len);
		get_ata_smart_info(dev);
	}
	fclose(fd);
}

void get_buddyinfo()
{
	char buddyinfo[255];
	snprintf(buddyinfo, 255, "%s/buddyinfo", ac->system_procfs);
	FILE *fd = fopen(buddyinfo, "r");
	if (!fd)
		return;

	char buf[1000];
	char zone[255];
	char node[255];
	char index[19];
	size_t diff;
	uint64_t count;

	while (fgets(buf, 1000, fd))
	{
		size_t len = strlen(buf)-1;
		buf[len] = 0;

		char *cur = buf + 5;
		char *tmp;
		size_t node_size = strcspn(cur, " ,\t");
		strlcpy(node, cur, node_size+1);

		tmp = strstr(cur, "zone");
		if (!tmp)
			continue;

		cur = tmp + 4;
		cur += strspn(cur, " \t");
		size_t zone_size = strcspn(cur, " \t");
		strlcpy(zone, cur, zone_size+1);

		cur += zone_size;

		uint64_t i;
		for (i = 0; cur-buf != len && i < 50; ++i)
		{
			count = strtoull(cur, &cur, 10);
			snprintf(index, 19, "%"u64, i);
			//printf("%s %s %s %llu\n", node, zone, index, count);
			metric_add_labels3("buddyinfo_count", &count, DATATYPE_UINT, ac->system_carg, "node", node, "zone", zone, "size", index);

			diff = strspn(cur, " \t");
			cur += diff;
		}
	}

	fclose(fd);
}

void get_kernel_version(int8_t platform)
{
	char procversion[255];
	snprintf(procversion, 255, "%s/version", ac->system_procfs);
	FILE *fd = fopen(procversion, "r");
	if (!fd)
		return;

	char buf[1000];
	char *cur;
	size_t version_size;
	char version[255];

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}

	cur = strstr(buf, "version");
	if (!cur)
	{
		fclose(fd);
		return;
	}
	cur += strcspn(cur+7, " \t") + 7;
	cur += strspn(cur, " \t");
	version_size = strcspn(cur, " \t");
	strlcpy(version, cur, version_size+1);
	int64_t vl = 1;
	if (platform == PLATFORM_BAREMETAL)
		metric_add_labels2("kernel_version", &vl, DATATYPE_INT, ac->system_carg, "version", version, "platform", "bare-metal");
	else if (platform == PLATFORM_LXC)
		metric_add_labels2("kernel_version", &vl, DATATYPE_INT, ac->system_carg, "version", version, "platform", "lxc");
	else if (platform == PLATFORM_DOCKER)
		metric_add_labels2("kernel_version", &vl, DATATYPE_INT, ac->system_carg, "version", version, "platform", "docker");
	else if (platform == PLATFORM_OPENVZ)
		metric_add_labels2("kernel_version", &vl, DATATYPE_INT, ac->system_carg, "version", version, "platform", "openvz");
	else if (platform == PLATFORM_KVM)
		metric_add_labels2("kernel_version", &vl, DATATYPE_INT, ac->system_carg, "version", version, "platform", "kvm");
	else if (platform == PLATFORM_OPENSTACK)
		metric_add_labels2("kernel_version", &vl, DATATYPE_INT, ac->system_carg, "version", version, "platform", "openstack");
	else
		metric_add_labels2("kernel_version", &vl, DATATYPE_INT, ac->system_carg, "version", version, "platform", "unknown");

    uint8_t i = 0;
    char *p = version;
	while (*p) {
		if (isdigit(*p)) {
			ac->kernel_version[i] = strtol(p, &p, 10);
			i++;
		} else {
			p++;
		}
	}

	fclose(fd);
}

void get_alligator_info()
{
	char genpath[255];
	char tmp[LINUXFS_LINE_LENGTH];
	char val[255];
	int64_t ival;
	snprintf(genpath, 254, "%s/%"d64"/status", ac->system_procfs, (int64_t)getpid());
	FILE *fd = fopen(genpath, "r");
	if (!fd)
		return;

	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		size_t tmp_len = strlen(tmp)-1;
		tmp[tmp_len] = 0;
		int64_t i = strcspn(tmp, " \t");

		int swap = strspn(tmp+i, " \t")+i;
		int size = strcspn(tmp+swap, " \t");
		strlcpy(val, tmp+swap, size+1);

		ival = atoll(val);

		if ( strstr(tmp+swap, "kB") )
			ival *= 1024;

		if ( !strncmp(tmp, "VmRSS", 5) )
		{
			metric_add_labels("alligator_memory_usage_bytes", &ival, DATATYPE_INT, ac->system_carg, "type", "rss");
		}
		if ( !strncmp(tmp, "VmSize", 6) )
		{
			metric_add_labels("alligator_memory_usage_bytes", &ival, DATATYPE_INT, ac->system_carg, "type", "vsz");
		}
	}
	fclose(fd);


	char cpupath[255];
	snprintf(cpupath, 254, "%s/%"d64"/stat", ac->system_procfs, (int64_t)getpid());
	fd = fopen(cpupath, "r");

	if ( !fd )
		return;

	if (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		char *t;
		t = strchr (tmp, ')');
		size_t sz = strlen(t);

		uint64_t cursor = 0;
		for (int i = 0; i++ < 10; int_get_next(t+4, sz, ' ', &cursor));

		int64_t utime = int_get_next(t+4, sz, ' ', &cursor);
		int64_t stime = int_get_next(t+4, sz, ' ', &cursor);
		int64_t cutime = int_get_next(t+4, sz, ' ', &cursor);
		int64_t cstime = int_get_next(t+4, sz, ' ', &cursor);

		double inv = 1.0 / (double)linux_user_hz();
		double stotal_time = (double)(stime + cstime) * inv;
		double utotal_time = (double)(utime + cutime) * inv;
		double total_time = stotal_time + utotal_time;

		metric_add_labels("alligator_cpu_seconds_total", &stotal_time, DATATYPE_DOUBLE, ac->system_carg, "mode", "system");
		metric_add_labels("alligator_cpu_seconds_total", &utotal_time, DATATYPE_DOUBLE, ac->system_carg, "mode", "user");
		metric_add_labels("alligator_cpu_seconds_total", &total_time, DATATYPE_DOUBLE, ac->system_carg, "mode", "total");
	}
	fclose (fd);
}

void get_packages_info()
{
	get_rpm_info();
	dpkg_crawl(DPKG_ADMINDIR "/status");
}

static uint64_t systemd_unit_is_enabled_in_dir(char *svcdir, char *service_name)
{
	char pathsystemd[1280];
	struct dirent *entry;
	DIR *dp;

	dp = opendir(svcdir);
	if (!dp)
	{
		return 0;
	}

	uint64_t enabled = 0;
	while((entry = readdir(dp)))
	{
		if (entry->d_name[0] == '.')
			continue;
		if (!strstr(entry->d_name, ".target.wants"))
			continue;

		snprintf(pathsystemd, 1279, "%s/%s/%s", svcdir, entry->d_name, service_name);
		FILE *fd = fopen(pathsystemd, "r");
		if (fd)
		{
			enabled = 1;
			fclose(fd);
			break;
		}
	}
	closedir(dp);
	return enabled;
}

void get_activate_status_services(char *service_name, char *username)
{
	char svcdir[1000];
	uint64_t enabled = 0;

	if (strstr(service_name, ".mount"))
		return;

	if (!username || !strcmp(username, "system"))
	{
		snprintf(svcdir, 999, "%s/systemd/system/", ac->system_etcdir);
		enabled = systemd_unit_is_enabled_in_dir(svcdir, service_name);
	}
	else if (!strcmp(username, "user"))
	{
		snprintf(svcdir, 999, "%s/systemd/user/", ac->system_etcdir);
		enabled = systemd_unit_is_enabled_in_dir(svcdir, service_name);
		if (!enabled)
		{
			snprintf(svcdir, 999, "%s/lib/systemd/user/", ac->system_usrdir);
			enabled = systemd_unit_is_enabled_in_dir(svcdir, service_name);
		}
	}
	else
	{
		struct passwd *pwd = getpwnam(username);
		if (pwd && pwd->pw_dir)
		{
			snprintf(svcdir, 999, "%s/.config/systemd/user/", pwd->pw_dir);
			enabled = systemd_unit_is_enabled_in_dir(svcdir, service_name);
		}
		if (!enabled)
		{
			snprintf(svcdir, 999, "%s/systemd/user/", ac->system_etcdir);
			enabled = systemd_unit_is_enabled_in_dir(svcdir, service_name);
		}
		if (!enabled)
		{
			snprintf(svcdir, 999, "%s/lib/systemd/user/", ac->system_usrdir);
			enabled = systemd_unit_is_enabled_in_dir(svcdir, service_name);
		}
	}

	metric_add_labels2("service_enabled", &enabled, DATATYPE_UINT, ac->system_carg, "service", service_name, "username", username ? username : "system");
}

void get_service_tasks_status(char *servicename, char *fname, char *type, char *username)
{
	char systemdpath[1000];
	struct stat path_stat;
	FILE *fd = NULL;
	char buf[100];
	uint64_t cnt = 0;
	char *metric_username = username ? username : "system";

	if (!username || !strcmp(username, "system"))
	{
		snprintf(systemdpath, 999, "%s/fs/cgroup/systemd/system.slice/%s/%s", ac->system_sysfs, servicename, fname);
		if (stat(systemdpath, &path_stat))
			snprintf(systemdpath, 999, "%s/fs/cgroup/system.slice/%s/%s", ac->system_sysfs, servicename, fname);
		fd = fopen(systemdpath, "r");
	}
	else
	{
		struct passwd *pwd = getpwnam(username);
		if (!pwd)
			return;

		snprintf(systemdpath, 999, "%s/fs/cgroup/user.slice/user-%u.slice/user@%u.service/app.slice/%s/%s", ac->system_sysfs, pwd->pw_uid, pwd->pw_uid, servicename, fname);
		fd = fopen(systemdpath, "r");
		if (!fd)
		{
			snprintf(systemdpath, 999, "%s/fs/cgroup/user.slice/user-%u.slice/user@%u.service/session.slice/%s/%s", ac->system_sysfs, pwd->pw_uid, pwd->pw_uid, servicename, fname);
			fd = fopen(systemdpath, "r");
		}
		if (!fd)
		{
			snprintf(systemdpath, 999, "%s/fs/cgroup/user.slice/user-%u.slice/user@%u.service/%s/%s", ac->system_sysfs, pwd->pw_uid, pwd->pw_uid, servicename, fname);
			fd = fopen(systemdpath, "r");
		}
	}

	if (!fd)
		return;

	for (cnt = 1; fgets(buf, 100, fd); ++cnt);
	fclose(fd);

	metric_add_labels3("service_tasks_count", &cnt, DATATYPE_UINT, ac->system_carg, "service", servicename, "type", type, "username", metric_username);
}

void service_running_status(char *name, char *username)
{
	int has_services = (match_mapper(ac->services_match, name, strlen(name), name) == 1);
	int has_services_process = (match_mapper(ac->services_process_match, name, strlen(name), name) == 1);

	if (!has_services && !has_services_process)
		return;

	int check_ready = 0;
	uint64_t val = systemd_check_service(name, username, &check_ready);

	metric_add_labels2("service_running", &val, DATATYPE_UINT, ac->system_carg, "service", name, "username", username ? username : "system");

	get_activate_status_services(name, username);
	get_service_tasks_status(name, "cgroup.procs", "processes", username);
	get_service_tasks_status(name, "tasks", "threads", username);

	if (has_services == 1 || has_services_process == 1)
	{
		if (check_ready)
			metric_add_labels2("service_match", &val, DATATYPE_UINT, ac->system_carg, "service", name, "username", username ? username : "system");
	}

	if (has_services_process == 1)
	{
		char cgrouppath[1024];
		struct stat path_stat;

		if (!username || !strcmp(username, "system"))
		{
			snprintf(cgrouppath, 1023, "%s/fs/cgroup/systemd/system.slice/%s/cgroup.procs", ac->system_sysfs, name);
			if (stat(cgrouppath, &path_stat))
				snprintf(cgrouppath, 1023, "%s/fs/cgroup/system.slice/%s/cgroup.procs", ac->system_sysfs, name);
			cgroup_procs_scrape(cgrouppath);
		}
		else
		{
			struct passwd *pwd = getpwnam(username);
			if (pwd)
			{
				snprintf(cgrouppath, 1023, "%s/fs/cgroup/user.slice/user-%u.slice/user@%u.service/app.slice/%s/cgroup.procs", ac->system_sysfs, pwd->pw_uid, pwd->pw_uid, name);
				if (stat(cgrouppath, &path_stat))
				{
					snprintf(cgrouppath, 1023, "%s/fs/cgroup/user.slice/user-%u.slice/user@%u.service/session.slice/%s/cgroup.procs", ac->system_sysfs, pwd->pw_uid, pwd->pw_uid, name);
					if (stat(cgrouppath, &path_stat))
						snprintf(cgrouppath, 1023, "%s/fs/cgroup/user.slice/user-%u.slice/user@%u.service/%s/cgroup.procs", ac->system_sysfs, pwd->pw_uid, pwd->pw_uid, name);
				}
				cgroup_procs_scrape(cgrouppath);
			}
		}
	}
}

static int service_name_seen(char **seen, size_t seen_count, const char *name, char *username)
{
	char key[2048];
	size_t i;

	snprintf(key, sizeof(key), "%s|%s", username ? username : "system", name ? name : "");
	for (i = 0; i < seen_count; ++i)
	{
		if (!strcmp(seen[i], key))
			return 1;
	}
	return 0;
}

static int service_name_seen_any_user(char **seen, size_t seen_count, const char *name)
{
	size_t i;
	size_t name_len;

	if (!name)
		return 0;

	name_len = strlen(name);
	for (i = 0; i < seen_count; ++i)
	{
		size_t seen_len = strlen(seen[i]);
		if (seen_len > name_len + 1 && !strcmp(seen[i] + seen_len - name_len, name) && seen[i][seen_len - name_len - 1] == '|')
			return 1;
	}

	return 0;
}

static int is_systemd_unit_name(const char *name)
{
	const char *suffixes[] = {
		".service", ".socket", ".target", ".timer",
		".mount", ".slice", ".scope", ".path", ".swap"
	};
	size_t i;
	size_t name_len;

	if (!name)
		return 0;

	name_len = strlen(name);
	if (!name_len)
		return 0;

	for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
	{
		size_t suffix_len = strlen(suffixes[i]);
		if (name_len >= suffix_len && !strcmp(name + name_len - suffix_len, suffixes[i]))
			return 1;
	}

	return 0;
}

static void service_name_add(char ***seen, size_t *seen_count, size_t *seen_cap, const char *name, char *username)
{
	char key[2048];

	snprintf(key, sizeof(key), "%s|%s", username ? username : "system", name ? name : "");

	if (*seen_count == *seen_cap)
	{
		size_t new_cap = (*seen_cap == 0) ? 64 : (*seen_cap * 2);
		char **new_seen = realloc(*seen, new_cap * sizeof(*new_seen));
		if (!new_seen)
			return;
		*seen = new_seen;
		*seen_cap = new_cap;
	}

	(*seen)[*seen_count] = strdup(key);
	if (!(*seen)[*seen_count])
		return;
	++(*seen_count);
}

static void scan_services_dir_unique(const char *path, char *username, char ***seen, size_t *seen_count, size_t *seen_cap)
{
	DIR *dp = opendir(path);
	struct dirent *entry;

	if (!dp)
		return;

	while ((entry = readdir(dp)))
	{
		if (entry->d_name[0] == '.')
			continue;

		if (!is_systemd_unit_name(entry->d_name))
			continue;

		if (service_name_seen(*seen, *seen_count, entry->d_name, username))
			continue;

		service_running_status(entry->d_name, username);
		service_name_add(seen, seen_count, seen_cap, entry->d_name, username);
	}

	closedir(dp);
}

typedef struct service_not_found_emit_ctx {
	char ***seen;
	size_t *seen_count;
	size_t *seen_cap;
} service_not_found_emit_ctx;

static void emit_not_found_service_match_cb(void *arg, void *obj)
{
	service_not_found_emit_ctx *ctx = arg;
	match_string *ms = obj;
	uint64_t val = 2;

	if (!ctx || !ms || !ms->s || !ms->s[0])
		return;

	if (!service_name_seen_any_user(*ctx->seen, *ctx->seen_count, ms->s))
	{
		metric_add_labels2("service_match", &val, DATATYPE_UINT, ac->system_carg, "service", ms->s, "username", "not_found");
		service_name_add(ctx->seen, ctx->seen_count, ctx->seen_cap, ms->s, "not_found");
	}
}

static void emit_not_found_service_match(alligator_ht *hash, char ***seen, size_t *seen_count, size_t *seen_cap)
{
	service_not_found_emit_ctx ctx = {seen, seen_count, seen_cap};

	if (!hash)
		return;

	alligator_ht_foreach_arg(hash, emit_not_found_service_match_cb, &ctx);
}

static int service_user_allowed(const char *username)
{
	uint32_t hash;

	if (!ac->system_services_checking_users || !alligator_ht_count(ac->system_services_checking_users))
		return 1;

	if (!username)
		return 0;

	hash = tommy_strhash_u32(0, username);
	return alligator_ht_search(ac->system_services_checking_users, system_string_compare, (void*)username, hash) != NULL;
}

typedef struct configured_service_scan_ctx {
	char ***seen_services;
	size_t *seen_count;
	size_t *seen_cap;
} configured_service_scan_ctx;

static void scan_configured_service_user_cb(void *arg, void *obj)
{
	configured_service_scan_ctx *ctx = arg;
	system_string_node *user = obj;
	struct passwd *pwd;
	char userdir[1000];
	uid_t uid;
	char pwname[256];

	if (!ctx || !user || !user->name)
		return;

	if (!strcmp(user->name, "system") || !strcmp(user->name, "user"))
		return;

	pwd = getpwnam(user->name);
	if (!pwd)
		return;

	uid = pwd->pw_uid;
	strlcpy(pwname, pwd->pw_name, sizeof(pwname));

	snprintf(userdir, 999, "/run/user/%u/systemd/user/", uid);
	scan_services_dir_unique(userdir, pwname, ctx->seen_services, ctx->seen_count, ctx->seen_cap);

	if (pwd->pw_dir)
	{
		snprintf(userdir, 999, "%s/.config/systemd/user/", pwd->pw_dir);
		scan_services_dir_unique(userdir, pwname, ctx->seen_services, ctx->seen_count, ctx->seen_cap);
	}
}

void get_services()
{
	char svcdir[1000];
	char userdir[1000];
	struct dirent *entry;
	char **seen_services = NULL;
	size_t seen_count = 0;
	size_t seen_cap = 0;
	uint8_t has_service_user_filter = (ac->system_services_checking_users && alligator_ht_count(ac->system_services_checking_users));
	size_t i;
	DIR *dp;

	if (!has_service_user_filter || service_user_allowed("system"))
	{
		snprintf(svcdir, 999, "%s/lib/systemd/system/", ac->system_usrdir);
		scan_services_dir_unique(svcdir, "system", &seen_services, &seen_count, &seen_cap);

		snprintf(svcdir, 999, "%s/systemd/", ac->system_rundir);
		dp = opendir(svcdir);
		if (dp)
		{
			while((entry = readdir(dp)))
			{
				if (strncmp(entry->d_name, "generator", 9))
					continue;

				snprintf(userdir, 999, "%s/systemd/%s", ac->system_rundir, entry->d_name);
				scan_services_dir_unique(userdir, "system", &seen_services, &seen_count, &seen_cap);
			}
			closedir(dp);
		}
	}

	if (!has_service_user_filter || service_user_allowed("user"))
	{
		snprintf(svcdir, 999, "%s/lib/systemd/user/", ac->system_usrdir);
		scan_services_dir_unique(svcdir, "user", &seen_services, &seen_count, &seen_cap);

		snprintf(svcdir, 999, "%s/systemd/user/", ac->system_etcdir);
		scan_services_dir_unique(svcdir, "user", &seen_services, &seen_count, &seen_cap);
	}

	if (has_service_user_filter)
	{
		configured_service_scan_ctx ctx = {&seen_services, &seen_count, &seen_cap};
		alligator_ht_foreach_arg(ac->system_services_checking_users, scan_configured_service_user_cb, &ctx);
	}
	else
	{
		dp = opendir("/run/user");
		if (dp)
		{
			while ((entry = readdir(dp)))
			{
				long uid;
				struct passwd *pwd;
				int n;

				if (entry->d_name[0] == '.')
					continue;

				for (n = 0; entry->d_name[n]; ++n)
				{
					if (entry->d_name[n] < '0' || entry->d_name[n] > '9') {
						break;
					}
				}

				if (entry->d_name[n] != '\0')
					continue;

				uid = strtol(entry->d_name, NULL, 10);
				if (uid <= 0)
					continue;

				pwd = getpwuid((uid_t) uid);
				if (!pwd || !pwd->pw_name)
					continue;

				{
					char pwname[256];
					strlcpy(pwname, pwd->pw_name, sizeof(pwname));

					snprintf(userdir, 999, "/run/user/%s/systemd/user/", entry->d_name);
					scan_services_dir_unique(userdir, pwname, &seen_services, &seen_count, &seen_cap);

					if (pwd->pw_dir)
					{
						snprintf(userdir, 999, "%s/.config/systemd/user/", pwd->pw_dir);
						scan_services_dir_unique(userdir, pwname, &seen_services, &seen_count, &seen_cap);
					}
				}
			}
			closedir(dp);
		}
	}

	emit_not_found_service_match(ac->services_match ? ac->services_match->hash : NULL, &seen_services, &seen_count, &seen_cap);
	emit_not_found_service_match(ac->services_process_match ? ac->services_process_match->hash : NULL, &seen_services, &seen_count, &seen_cap);

	for (i = 0; i < seen_count; ++i)
		free(seen_services[i]);
	free(seen_services);
}



void utmp_parse(struct utmp *log) {
	if (log && log->ut_type == USER_PROCESS)
	{
		//printf("{ ut_type: %i, ut_pid: %i, ut_line: %s, ut_user: %s, ut_host:   %s, ut_exit: { e_termination: %i, e_exit: %i }, ut_session: %i, timeval: { tv_sec: %i, tv_usec: %i }, ut_addr_v6: %i }\n\n", log->ut_type, log->ut_pid, log->ut_line, log->ut_user, log->ut_host, log->ut_exit.e_termination, log->ut_exit.e_exit, log->ut_session, log->ut_tv.tv_sec, log->ut_tv.tv_usec, log->ut_addr_v6);
		int64_t time = log->ut_tv.tv_sec;
		if (!strncmp(log->ut_line, "tty", 3))
		{
			//printf("user: %s, host %s, logged %u, line %s, type=\"terminal\"\n", log->ut_user, log->ut_host, log->ut_tv.tv_sec, log->ut_line);
			metric_add_labels4("utmp_logged_in_timestamp_seconds", &time, DATATYPE_INT, ac->system_carg, "user", log->ut_user, "host", log->ut_host, "type", "terminal", "terminal", log->ut_line);
		}
		if (!strncmp(log->ut_line, "pts", 3))
		{
			//printf("user: %s, host %s, logged %u, line %s, type=\"pseudo-terminal\"\n", log->ut_user, log->ut_host, log->ut_tv.tv_sec, log->ut_line);
			metric_add_labels4("utmp_logged_in_timestamp_seconds", &time, DATATYPE_INT, ac->system_carg, "user", log->ut_user, "host", log->ut_host, "type", "pseudo-terminal", "terminal", log->ut_line);
		}
	}
}

void get_utmp_info()
{
	//int logsize = 10;
	FILE *file;
	//struct utmp log[logsize];
	struct utmp log;

	uint64_t btmp_size = get_file_size("/var/log/btmp");
	metric_add_auto("btmp_file_size", &btmp_size, DATATYPE_UINT, ac->system_carg);

	file = fopen("/var/run/utmp", "rb");

	if (!file) {
		return;
	}

	//fread(&log, sizeof(struct utmp), logsize, file);
	size_t rc = 1;
	for(; rc;) {
		rc = fread(&log, sizeof(struct utmp), 1, file);
		if (rc)
			utmp_parse(&log);
	}

	fclose(file);
}

void get_drbd_info()
{
	FILE *file;
	char drbdfile[1024];
	char str[1024];
	char token[1024];
	char minor[1024];
	*minor = 0;
	char metric_name[1024];
	snprintf(drbdfile, 1024, "%s/drbd", ac->system_procfs);
	file = fopen(drbdfile, "r");
	if (!file)
		return;

	strlcpy(metric_name, "drbd_", 6);

	char *tmp = 0;
	while (fgets(str, 1024, file))
	{
		uint64_t str_size = strlen(str);

		uint64_t mode = 0;
		if (*str == 0)
			continue;

		if (!isspace(*str))
			continue;

		if (!isdigit(*(str+1)))
		{
			if (strstr(str, "ns:"))
				mode = 1;
			else if ((tmp = strstr(str, "%")))
				mode = 2;
			else
				continue;
		}

		if (mode == 0)
		{
			char *obj[] = {"field", "minor", "connection_state", "state", "disk_state"};
			for (uint64_t i = 0, k = 0; i < str_size && k < 5; i++, k++)
			{
				uint64_t token_size = str_get_next(str, token, 1024, " \t\n\r", &i);
				if (!token_size)
					continue;

				//printf("\ttoken is %s\n", token);

				uint64_t key_sz = strcspn(token, ":");
				char param[1024];
				strlcpy(param, token + key_sz + 1, 1024);

				if (*param == 0)
					strlcpy(param, token, key_sz + 1);

				if (k == 1)
				{
					strlcpy(minor, token, key_sz + 1);
					continue;
				}

				//printf("\t\tobj: %s, param %s\n", obj[k], param);
				strlcpy(metric_name + 5, obj[k], 1024 - 5);
				uint64_t val = 1;
				metric_add_labels2(metric_name, &val, DATATYPE_UINT, ac->system_carg, obj[k], param, "minor", minor);
			}
		}
		else if (mode == 1)
		{
			//printf("%u/%s", mode, str);
			char *obj[] = {"network_send", "network_receive", "disk_write", "disk_read", "activity_log", "bit_map", "local_count", "pending", "unacknowledged", "application_pending", "epochs", "write_order", "out_of_sync" };
			for (uint64_t i = 0, k = 0; i < str_size && k < 13; i++, k++)
			{
				uint64_t token_size = str_get_next(str, token, 1024, " \t\n\r", &i);
				if (!token_size)
					continue;

				//printf("\ttoken is %s\n", token);

				uint64_t key_sz = strcspn(token, ":");
				char param[1024];
				strlcpy(param, token + key_sz + 1, 1024);

				if (*param == 0)
					strlcpy(param, token, key_sz + 1);

				//printf("\t\tobj: %s, param %s\n", obj[k], param);
				strlcpy(metric_name + 5, obj[k], 1024 - 5);
				uint64_t val = strtoull(param, NULL, 10);
				metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "minor", minor);
			}
		}
		else
		{
			//printf("%u/%s", mode, str);
			tmp = strstr(str, "sync'ed:");
			if (!tmp)
				continue;

			tmp += 9;
			double val = strtod(tmp, NULL);
			metric_add_labels("drbd_sync_percent", &val, DATATYPE_DOUBLE, ac->system_carg, "minor", minor);
		}
	}

	fclose(file);
}

void parse_nfs_stats(char *name, char *mname)
{
	FILE *file;
	char fname[1024];
	char str[1024];
	char token[1024];
	//char *param;
	char metric_name[1024];
	snprintf(fname, 1024, "%s/net/rpc/%s", ac->system_procfs, name);
	file = fopen(fname, "r");
	if (!file)
		return;

	uint64_t mname_size = strlcpy(metric_name, mname, 1024);

	//char *tmp = 0;
	while (fgets(str, 1024, file))
	{
		uint64_t str_size = strlen(str);
		//puts(str);

		uint64_t i = 0;
		str_get_next(str, token, 1024, " \t\n\r", &i);
		//printf("\tparam: '%s'\n", token);

		if (!strncmp(token, "rc", 2))
		{
			strlcpy(metric_name + mname_size, "reply_cache_count", 1024 - mname_size);
			char *obj[] = {"hits", "misses", "nocache" };
			str_get_next(str, token, 1024, " \t\n\r", &i);
			++i;
			for (uint64_t k = 0; i < str_size && k < 3; i++, k++)
			{
				str_get_next(str, token, 1024, " \t\n\r", &i);
				//printf("\t{'%s'}:: '%s': '%s'\n", metric_name, obj[k], token);
				uint64_t val = strtoull(token, NULL, 10);
				metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "type", obj[k]);
			}
		}

		else if (!strncmp(token, "fh", 2))
		{
			strlcpy(metric_name + mname_size, "file_handlers_count", 1024 - mname_size);
			char *obj[] = {"stale", "total_lookups", "nocanonlookups", "dirnocache", "nodirnocache" };
			str_get_next(str, token, 1024, " \t\n\r", &i);
			++i;
			for (uint64_t k = 0; i < str_size && k < 5; i++, k++)
			{
				str_get_next(str, token, 1024, " \t\n\r", &i);
				//printf("\t{'%s'}:: '%s': '%s'\n", metric_name, obj[k], token);
				uint64_t val = strtoull(token, NULL, 10);
				metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "type", obj[k]);
			}
		}

		else if (!strncmp(token, "io", 2))
		{
			strlcpy(metric_name + mname_size, "io_bytes", 1024 - mname_size);
			char *obj[] = {"read", "write" };
			str_get_next(str, token, 1024, " \t\n\r", &i);
			++i;
			for (uint64_t k = 0; i < str_size && k < 2; i++, k++)
			{
				str_get_next(str, token, 1024, " \t\n\r", &i);
				//printf("\t{'%s'}:: '%s': '%s'\n", metric_name, obj[k], token);
				uint64_t val = strtoull(token, NULL, 10);
				metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "type", obj[k]);
			}
		}

		else if (!strncmp(token, "th", 2))
		{
			strlcpy(metric_name + mname_size, "threads_count", 1024 - mname_size);
			str_get_next(str, token, 1024, " \t\n\r", &i);
			//printf("\t{'%s'}:: '%s'\n", metric_name, token);
			uint64_t val = strtoull(token, NULL, 10);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, ac->system_carg);

			strlcpy(metric_name + mname_size, "threads_full_count", 1024 - mname_size);
			str_get_next(str, token, 1024, " \t\n\r", &i);
			//printf("\t{'%s'}:: '%s'\n", metric_name, token);
			val = strtoull(token, NULL, 10);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, ac->system_carg);
		}

		else if (!strncmp(token, "ra", 2))
		{
			char *obj[] = { "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" };

			str_get_next(str, token, 1024, " \t\n\r", &i);
			++i;

			strlcpy(metric_name + mname_size, "read_ahead_cache_size", 1024 - mname_size);
			str_get_next(str, token, 1024, " \t\n\r", &i);
			uint64_t val = strtoull(token, NULL, 10);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, ac->system_carg);

			strlcpy(metric_name + mname_size, "read_ahead_cache_found", 1024 - mname_size);

			for (uint64_t k = 0; i < str_size && k < 10; i++, k++)
			{
				str_get_next(str, token, 1024, " \t\n\r", &i);
				val = strtoull(token, NULL, 10);
				metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "percent", obj[k]);
			}

			strlcpy(metric_name + mname_size, "read_ahead_cache_not_found", 1024 - mname_size);
			str_get_next(str, token, 1024, " \t\n\r", &i);
			val = strtoull(token, NULL, 10);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, ac->system_carg);
		}

		else if (!strncmp(token, "net", 3))
		{
			strlcpy(metric_name + mname_size, "network_count", 1024 - mname_size);
			char *obj[] = {"total", "UDP", "TCP", "TCPconnect" };
			str_get_next(str, token, 1024, " \t\n\r", &i);
			++i;
			for (uint64_t k = 0; i < str_size && k < 4; i++, k++)
			{
				str_get_next(str, token, 1024, " \t\n\r", &i);
				//printf("\t{'%s'}:: '%s': '%s'\n", metric_name, obj[k], token);
				uint64_t val = strtoull(token, NULL, 10);
				metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "type", obj[k]);
			}
		}

		else if (!strncmp(token, "rpc", 3))
		{
			strlcpy(metric_name + mname_size, "rpc", 1024 - mname_size);
			char *obj[] = {"count", "bad_count", "badfmt", "bad_auth" };
			str_get_next(str, token, 1024, " \t\n\r", &i);
			++i;
			for (uint64_t k = 0; i < str_size && k < 4; i++, k++)
			{
				str_get_next(str, token, 1024, " \t\n\r", &i);
				//printf("\t{'%s'}:: '%s': '%s'\n", metric_name, obj[k], token);
				uint64_t val = strtoull(token, NULL, 10);
				metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "type", obj[k]);
			}
		}

		else if (!strncmp(token, "proc2", 5))
		{
			strlcpy(metric_name + mname_size, "proto_v2_stat", 1024 - mname_size);
			char *obj[] = {"values", "null", "null", "getattr", "setattr", "get_filesystem_root", "lookup", "readlink", "read", "write_cache", "write", "create", "remove", "rename", "link_create", "symlink_create", "mkdir", "rmdir", "readdir", "fsstat" };
			for (uint64_t k = 0; i < str_size && k < 19; i++, k++)
			{
				str_get_next(str, token, 1024, " \t\n\r", &i);
				if (k > 2)
				{
					//printf("\t{'%s'}:: '%s': '%s'\n", metric_name, obj[k], token);
					uint64_t val = strtoull(token, NULL, 10);
					metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "type", obj[k]);
				}
			}
		}

		else if (!strncmp(token, "proc3", 5))
		{
			strlcpy(metric_name + mname_size, "proto_v3_stat", 1024 - mname_size);
			char *obj[] = {"values", "null", "null", "getattr", "setattr", "lookup", "check_access", "readlink", "read", "write", "create", "mkdir", "symlink", "mknode", "remove", "rmdir", "rename", "link", "readdir", "extended_read", "fsstat", "fsinfo", "pathconf", "commit" };
			for (uint64_t k = 0; i < str_size && k < 23; i++, k++)
			{
				str_get_next(str, token, 1024, " \t\n\r", &i);
				if (k > 2)
				{
					//printf("\t{'%s'}:: '%s': '%s'\n", metric_name, obj[k], token);
					uint64_t val = strtoull(token, NULL, 10);
					metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "type", obj[k]);
				}
			}
		}

		else if (!strncmp(token, "proc4ops", 8))
		{
			strlcpy(metric_name + mname_size, "proto_v4_stat_op", 1024 - mname_size);
			char *obj[] = {"values", "null", "null", "compound", "access", "close", "commit", "create", "delegations_purge", "delegations_return", "getattr", "get_filehandle", "link_create", "lock_create", "lock_test", "unlock", "lookup_file", "lookup_parent_directory", "verify_difference_attr", "open", "openattr", "open_confirm", "open_downgrade", "set_current_filehandle", "set_public_filehandle", "set_root_filehandle", "read", "readdir", "readlink", "remove", "rename", "renew", "restore_filehandle", "save_filehandle", "secinfo", "setattr", "negotiate_clientid", "verify", "write"};
			for (uint64_t k = 0; i < str_size && k < 38; i++, k++)
			{
				str_get_next(str, token, 1024, " \t\n\r", &i);
				if (k > 2)
				{
					//printf("\t{'%s'}:: '%s': '%s'\n", metric_name, obj[k], token);
					uint64_t val = strtoull(token, NULL, 10);
					metric_add_labels(metric_name, &val, DATATYPE_UINT, ac->system_carg, "type", obj[k]);
				}
			}
		}

		//else
		//	printf("%s:%s:%s\n", name, metric_name, str);
	}

	fclose(file);
}

void get_nfs_stats()
{
	parse_nfs_stats("nfsd", "nfs_server_");
	parse_nfs_stats("nfs", "nfs_client_");
}

void get_systemd_scopes()
{
	char dirname[255];
	snprintf(dirname, 255, "%s/systemd/system/", ac->system_rundir);

	uint64_t session = 0;
	uint64_t run = 0;
	uint64_t user = 0;

	struct dirent *entry;
	DIR* dp = opendir(dirname);
	if (!dp)
	{
		return;
	}

	while((entry = readdir(dp)))
	{
		if (!strncmp(entry->d_name, "session", 7))
		{
			if (!strstr(entry->d_name, ".scope.d"))
				++session;
		}

		else if (!strncmp(entry->d_name, "run", 3))
		{
			if (!strstr(entry->d_name, ".scope.d"))
				++run;
		}

		else if (!strncmp(entry->d_name, "user", 3))
		{
			if (!strstr(entry->d_name, ".slice.d"))
				++user;
		}
	}

	metric_add_labels("systemd_scopes_count", &session, DATATYPE_UINT, ac->system_carg, "type", "session");
	metric_add_labels("systemd_scopes_count", &run, DATATYPE_UINT, ac->system_carg, "type", "run");
	metric_add_labels("systemd_scopes_count", &user, DATATYPE_UINT, ac->system_carg, "type", "user");
	closedir(dp);
}

void get_system_metrics()
{
	int8_t platform = -1;
	if (ac->system_base)
	{
		get_uptime();
		platform = get_platform(1);
		get_mem(platform);
		get_nofile_stat();
		get_loadavg();
		get_kernel_version(platform);
		get_alligator_info();
		get_conntrack_info();
		ipaddr_info();
		hw_cpu_info();
		get_utsname();
		get_utmp_info();
		get_drbd_info();
		get_nfs_stats();
		get_systemd_scopes();
		get_distribution_name();
		get_pressure_stats();
		get_swap_stats();
		get_schedstat_stats();
		get_entropy_stats();
		get_selinux_stats();
		collect_power_supply();
		if (is_baremetal_or_vm(platform)) { // exclude containers
			get_proc_interrupts(ac->system_interrupts);
		}
		if (is_baremetal(platform))
		{
			memory_errors_by_controller();
			get_thermal();
			get_buddyinfo();
		}
		else
			throttle_stat();
	}

	// find_pid before system_network!
	if (ac->system_process)
	{
		clear_counts_process();
		find_pid(0);
		fill_counts_process();
	}
	else if (ac->system_base)
		find_pid(1);

	if (ac->system_interrupts)
		get_softirqs_stats();

	if (ac->system_network)
	{
		get_network_statistics_netlink();
		char dirname[255];

		snprintf(dirname, 255, "%s/net/netstat", ac->system_procfs);
		get_netstat_statistics(dirname);

		snprintf(dirname, 255, "%s/net/snmp", ac->system_procfs);
		get_netstat_statistics(dirname);

		get_softnet_stats();
		get_sockstat_stats();
		get_bonding_stats();
		get_arp_stats();
		get_ipvs_stats();

		interface_stats();

		check_sockets_by_netlink("tcp", AF_INET, IPPROTO_TCP);
		check_sockets_by_netlink("tcp6", AF_INET6, IPPROTO_TCP);

		check_sockets_by_netlink("udp", AF_INET, IPPROTO_UDP);
		check_sockets_by_netlink("udp6", AF_INET6, IPPROTO_UDP);
	}
	if (ac->system_disk)
	{
		get_disk_io_stat();
		get_disk();
		get_dmmultipath_stats();
		if (platform == -1)
			platform = get_platform(0);
		if (is_baremetal_or_vm(!platform))
			get_mdadm();
	}

	if (ac->fdesc)
	{
		alligator_ht_foreach_arg(ac->fdesc, process_fdescriptors_free, ac->fdesc);
		alligator_ht_done(ac->fdesc);
		alligator_ht_init(ac->fdesc);
	}
	if (ac->system_firewall)
	{
		if (!ac->kernel_version[0]) {
			get_kernel_version(platform);
		}
		if (ac->kernel_version[0] >= 5)
			nftables_handler();
		else  {
			get_iptables_info(ac->system_procfs, "iptables", "nat", ac->system_carg);
			get_iptables_info(ac->system_procfs, "iptables", "filter", ac->system_carg);
			get_iptables_info(ac->system_procfs, "ip6tables", "nat", ac->system_carg);
			get_iptables_info(ac->system_procfs, "ip6tables", "filter", ac->system_carg);
		}

		if (ac->system_ipset)
			ipset();
	}
	if (ac->system_cpuavg)
		get_cpu_avg();

	if (ac->system_cadvisor)
		cadvisor_metrics();

	if (ac->system_services || ac->system_services_process)
		get_services();

	if (ac->system_ipmi)
	{
		if (!platform)
			ipmi_schedule_get_status();
	}

	if (ac->system_nvml)
		nvml_schedule_scrape();

	if (ac->system_dcgm)
		dcgm_schedule_scrape();

	if (ac->system_amdgpu)
		amdgpu_scrape();

	get_pidfile_stats();
	get_userprocess_stats();
	sysctl_run(ac->system_sysctl);
}

void system_free()
{
	if (ac->fdesc)
	{
		alligator_ht_foreach_arg(ac->fdesc, process_fdescriptors_free, ac->fdesc);
		alligator_ht_done(ac->fdesc);
		free(ac->fdesc);
		ac->fdesc = NULL;
	}

	if (ac->system_avg_metrics)
	{
		free(ac->system_avg_metrics);
		ac->system_avg_metrics = NULL;
	}

	if (ac->scs)
	{
		if (ac->scs->cores)
			free(ac->scs->cores);

		free(ac->scs);
		ac->scs = NULL;
	}

	free(ac->system_sysfs);
	free(ac->system_procfs);
	free(ac->system_rundir);
	free(ac->system_usrdir);
	free(ac->system_etcdir);
	free(ac->system_pidfile);

	match_free(ac->process_match);

	match_free(ac->packages_match);

	match_free(ac->services_match);

	match_free(ac->services_process_match);

	userprocess_free(ac->system_userprocess);

	userprocess_free(ac->system_groupprocess);
	service_user_free(ac->system_services_checking_users);
	sysctl_free(ac->system_sysctl);
}

void system_fast_scrape()
{
	if (ac->system_base)
	{
		get_cpu(get_platform(0));
		get_scaling_current_cpu_freq();
	}
}

void system_slow_scrape()
{
	int8_t platform = get_platform(0);
	if (ac->system_packages)
	{
		get_packages_info();
	}

	if (ac->system_base)
	{
		if (!platform)
		{
			get_smbios();
			disks_info();
		}
		get_zoneinfo_stats();
		get_numa_meminfo_stats();
		get_watchdog_stats();
		if (is_baremetal(platform))
			get_rapl_stats();
	}

	if (ac->system_memory)
		get_slabinfo_stats();

	if (ac->system_network)
	{
		get_infiniband_stats();
		get_fibrechannel_stats();
	}

	if (ac->system_disk)
	{
		get_xfs_stats();
		get_btrfs_stats();
		get_bcache_stats();
		get_tape_stats();
	}

	if (ac->system_smart)
	{
		if (!platform)
			get_smart_info();
	}

}

#endif
