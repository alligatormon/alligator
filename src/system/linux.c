#ifdef __linux__
#include <limits.h>
#include <unistd.h>
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
#include "metric/labels.h"
#include "common/smart.h"
#include "dstructures/ht.h"
#include <utmp.h>
#include "system/linux.h"
//#include "common/rpm.h"
#define LINUXFS_LINE_LENGTH 300
#define d64 PRId64
#define PLATFORM_BAREMETAL 0
#define PLATFORM_LXC 1
#define PLATFORM_OPENVZ 2
#define PLATFORM_NSPAWN 3
#define PLATFORM_DOCKER 4
#define LINUX_MEMORY 1
#define LINUX_CPU 2
extern aconf *ac;

typedef struct process_states
{
	uint64_t running;
	uint64_t sleeping;
	uint64_t uninterruptible;
	uint64_t zombie;
	uint64_t stopped;
} process_states;

typedef struct ulimit_pid_stat {
	uint64_t datasize;
	uint64_t stacksize;
	uint64_t rsssize;
	uint64_t openfiles;
	uint64_t lockedsize;
	uint64_t addressspace;
} ulimit_pid_stat;

typedef struct port_usage {
	char *remote_addr;
	char *remote_port;
	char *local_addr;
	char *local_port;
	uint64_t count;
	int state_proto;
	int state;
	char *process;

	char *key;
	tommy_node node;
} port_usage;

void print_mount(const struct mntent *fs)
{
	if (!strcmp(fs->mnt_type,"tmpfs") || !strcmp(fs->mnt_type,"xfs") || !strcmp(fs->mnt_type,"ext4") || !strcmp(fs->mnt_type,"btrfs") || !strcmp(fs->mnt_type,"ext3") || !strcmp(fs->mnt_type,"ext2"))
	{
		if (!strncmp(fs->mnt_dir, "/etc", 4) || !strncmp(fs->mnt_dir, "/dev", 4) || !strncmp(fs->mnt_dir, "/proc", 5) || !strncmp(fs->mnt_dir, "/sys", 4) || !strncmp(fs->mnt_dir, "/run", 4) || !strncmp(fs->mnt_dir, "/var/lib/docker", 15))
			return;

		int f_d = 0;
		f_d = open(fs->mnt_dir,O_RDONLY);
		if(-1 == f_d)
		{
		}
		else
		{
			struct statvfs buf;

			if (statvfs(fs->mnt_dir, &buf) == -1)
				perror("statvfs() error");
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
	if (ac->log_level > 2)
		puts("system scrape metrics: disk: get_stat");

	FILE *fp;
	struct mntent *fs;

	fp = setmntent("/etc/mtab", "r");
	if (fp == NULL) {
		fprintf(stderr, "could not open /etc/mtab\n " );
		return;
	}

	while ((fs = getmntent(fp)) != NULL)
		print_mount(fs);

	endmntent(fp);
}

void cpu_avg_push(double now)
{
	if (now > 100)
		return;

	double old = ac->system_avg_metrics[ac->system_cpuavg_ptr];
	ac->system_avg_metrics[ac->system_cpuavg_ptr] = now;
	ac->system_cpuavg_sum = (ac->system_cpuavg_sum - old) + now;
	if (ac->log_level > 2)
		printf("set ac->system_avg_metrics[%"u64"]=%lf, sum: %lf\n", ac->system_cpuavg_ptr, ac->system_avg_metrics[ac->system_cpuavg_ptr], ac->system_cpuavg_sum);

	++ac->system_cpuavg_ptr;
	if (ac->system_cpuavg_ptr >= ac->system_cpuavg_period)
		ac->system_cpuavg_ptr = 0;
}

void get_cpu(int8_t platform)
{
	if (ac->log_level > 2)
		puts("fast scrape metrics: base: cpu");
	r_time ts_start = setrtime();

	int64_t effective_cores;
	int64_t num_cpus = sysconf( _SC_NPROCESSORS_ONLN );
	char syspath[255];
	snprintf(syspath, 255, "%s/fs/cgroup/cpu/cpu.cfs_quota_us", ac->system_sysfs);
	int64_t num_cpus_cgroup = (getkvfile(syspath)/100000);
	double dividecpu = 1;
	if ( num_cpus_cgroup <= 0 )
		effective_cores = dividecpu = num_cpus;
	else if ( num_cpus < num_cpus_cgroup )
	{
		effective_cores = dividecpu = num_cpus;
	}
	else
		effective_cores = dividecpu = num_cpus_cgroup;

	if (!ac->scs->cores)
		ac->scs->cores = calloc(1, sizeof(system_cpu_cores_stats)*num_cpus);


	metric_add_auto("cores_num_hw", &num_cpus, DATATYPE_INT, ac->system_carg);
	metric_add_auto("cores_num_cgroup", &num_cpus_cgroup, DATATYPE_INT, ac->system_carg);
	metric_add_auto("cores_num", &effective_cores, DATATYPE_INT, ac->system_carg);

	char temp[LINUXFS_LINE_LENGTH];
	uint64_t core_num = 0;
	system_cpu_cores_stats *sccs;

	char cpu_usage_name[30];
	char cpu_usage_core_name[30];
	char cpu_usage_time_name[30];

	if (!platform)
	{
		strlcpy(cpu_usage_name, "cpu_usage", 10);
		strlcpy(cpu_usage_core_name, "cpu_usage_core", 15);
		strlcpy(cpu_usage_time_name, "cpu_usage_time", 15);
	}
	else
	{
		strlcpy(cpu_usage_name, "cpu_usage_hw", 13);
		strlcpy(cpu_usage_core_name, "cpu_usage_hw_core", 18);
		strlcpy(cpu_usage_time_name, "cpu_usage_hw_time", 18);
	}

	// hw stats
	char procstat[255];
	snprintf(procstat, 255, "%s/stat", ac->system_procfs);
	FILE *fd = fopen(procstat, "r");
	if ( !fd )
		return;

	while ( fgets(temp, LINUXFS_LINE_LENGTH, fd) )
	{
		if ( !strncmp(temp, "cpu", 3) )
		{
			int64_t t1, t2, t3, t4, t5;
			char cpuname[6];

			sscanf(temp, "%s %"d64" %"d64" %"d64" %"d64" %"d64"", cpuname, &t1, &t2, &t3, &t4, &t5);
			core_num = atoll(cpuname+3);
			if (!strcmp(cpuname, "cpu"))
				sccs = &ac->scs->hw;
			else
			{
				sccs = &ac->scs->cores[core_num];
				if (core_num >= num_cpus)
				{
					printf("error: cpus: %"PRId64", core num: %"PRIu64", field: '%s' (/proc/stat)\n", num_cpus, core_num, temp);
					continue;
				}
			}

			uint64_t ttotal = (t1 + t3) / dividecpu;
			//printf("%"u64" = ( %"u64" + %"u64" ) / %lf\n", ttotal, t1, t3, dividecpu);
			uint64_t tuser = t1 / dividecpu;
			uint64_t tnice = t2 / dividecpu;
			uint64_t tsystem = t3 / dividecpu;
			uint64_t tidle = t4 / dividecpu;
			uint64_t tiowait = t5 / dividecpu;

			uint64_t t12345 = t1+t2+t3+t4+t5 - sccs->total;
			sccs->total += t12345;

			t1 -= sccs->user;
			sccs->user += t1;
			t2 -= sccs->nice;
			sccs->nice += t2;
			t3 -= sccs->system;
			sccs->system += t3;
			t4 -= sccs->idle;
			sccs->idle += t4;
			t5 -= sccs->iowait;
			sccs->iowait += t5;

			double usage = ((t1 + t3)*100.0/t12345);
			if (usage<0)
				usage = 0;
			double user = ((t1)*100.0/t12345);
			if (user<0)
				user = 0;
			double nice = ((t2)*100.0/t12345);
			if (nice<0)
				nice = 0;
			double system = ((t3)*100.0/t12345);
			if (system<0)
				system = 0;
			double idle = ((t4)*100.0/t12345);
			if (idle<0)
				idle = 0;
			double iowait = ((t5)*100.0/t12345);
			if (iowait<0)
				iowait = 0;

			if (!strcmp(cpuname, "cpu"))
			{
				//printf("CPU: usage %lf, user %lf, system %lf, nice %lf, idle %lf, iowait %lf\n", usage, user, system, nice, idle, iowait);
				//printf("CPU: tusage %"d64", tuser %"d64", tnice %"d64", tsystem %"d64", tidle %"d64", tiowait %"d64"\n", ttotal, tuser, tnice, tsystem, tidle, tiowait);
				metric_add_labels(cpu_usage_name, &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "total");
				metric_add_labels(cpu_usage_name, &user, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
				metric_add_labels(cpu_usage_name, &system, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
				metric_add_labels(cpu_usage_name, &nice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice");
				metric_add_labels(cpu_usage_name, &idle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle");
				metric_add_labels(cpu_usage_name, &iowait, DATATYPE_DOUBLE, ac->system_carg, "type", "iowait");
				metric_add_labels(cpu_usage_time_name, &ttotal, DATATYPE_UINT, ac->system_carg, "type", "total");
				metric_add_labels(cpu_usage_time_name, &tuser, DATATYPE_INT, ac->system_carg, "type", "user");
				metric_add_labels(cpu_usage_time_name, &tnice, DATATYPE_INT, ac->system_carg, "type", "nice");
				metric_add_labels(cpu_usage_time_name, &tsystem, DATATYPE_INT, ac->system_carg, "type", "system");
				metric_add_labels(cpu_usage_time_name, &tidle, DATATYPE_INT, ac->system_carg, "type", "idle");
				metric_add_labels(cpu_usage_time_name, &tiowait, DATATYPE_INT, ac->system_carg, "type", "iowait");
				if (!platform && ac->system_cpuavg)
				{
					cpu_avg_push(usage);
				}
			}
			else
			{
				//printf("core: '%s' usage %lf, user %lf, system %lf, nice %lf, idle %lf, iowait %lf\n", cpuname, usage, user, system, nice, idle, iowait);
				metric_add_labels2(cpu_usage_core_name, &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "total", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &user, DATATYPE_DOUBLE, ac->system_carg, "type", "user", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &system, DATATYPE_DOUBLE, ac->system_carg, "type", "system", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &nice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &idle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &iowait, DATATYPE_DOUBLE, ac->system_carg, "type", "iowait", "cpu", cpuname);
			}
		}
		else if ( !strncmp(temp, "processes", 9) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("forks", &ival, DATATYPE_UINT, ac->system_carg);
		}
		else if ( !strncmp(temp, "ctxt", 4) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("context_switches", &ival, DATATYPE_UINT, ac->system_carg);
		}
		else if ( !strncmp(temp, "intr", 4) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("interrupts", &ival, DATATYPE_UINT, ac->system_carg);
		}
		else if ( !strncmp(temp, "softirq", 7) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("softirq", &ival, DATATYPE_UINT, ac->system_carg);
		}
	}
	fclose(fd);

	// cgroup stats
	if (platform)
	{
		sccs = &ac->scs->cgroup;

		snprintf(syspath, 255, "%s/fs/cgroup/cpu/cpu.cfs_period_us", ac->system_sysfs);
		int64_t cfs_period = getkvfile(syspath);

		snprintf(syspath, 255, "%s/fs/cgroup/cpu/cpu.cfs_quota_us", ac->system_sysfs);
		int64_t cfs_quota = getkvfile(syspath);
		if ( cfs_quota < 0 )
		{
			cfs_quota = cfs_period*dividecpu;
		}

		snprintf(syspath, 255, "%s/fs/cgroup/cpuacct/cpuacct.stat", ac->system_sysfs);
		FILE *fd = fopen(syspath, "r");
		if (!fd)
		{
			printf("Not found %s\n", syspath);
			return;
		}

		char *tmp;
		char buf[1000];
		int64_t cgroup_system_ticks = 0;
		int64_t cgroup_user_ticks = 0;
		while (fgets(buf, 1000, fd))
		{
			if (!strncmp(buf, "user", 4))
			{
				tmp = buf;
				tmp += strcspn(buf, " \t");
				tmp += strspn(buf, " \t");
				cgroup_user_ticks = strtoll(tmp, NULL, 10);
			}
			else if (!strncmp(buf, "system", 6))
			{
				tmp = buf;
				tmp += strcspn(buf, " \t");
				tmp += strspn(buf, " \t");
				cgroup_system_ticks = strtoll(tmp, NULL, 10);
			}
		}

		fclose(fd);

		////printf("system %lld - %lld\n", cgroup_system_ticks, sccs->system);
		cgroup_system_ticks -= sccs->system;
		////printf("system minus %lld\n", cgroup_system_ticks);
		sccs->system += cgroup_system_ticks;
		//printf("system plus %lld\n", sccs->system);

		//printf("user %lld - %lld\n", cgroup_user_ticks, sccs->user);
		cgroup_user_ticks -= sccs->user;
		//printf("user minus %lld\n", cgroup_user_ticks);
		sccs->user += cgroup_user_ticks;
		//printf("user plus %lld\n", sccs->user);

		dividecpu = (cfs_quota*1.0/cfs_period);
		//printf("cfs_quota/cfs_period %d/%d = %lf/%lf\n", cfs_quota, cfs_period, (cfs_quota*1.0/cfs_period), dividecpu);
		double cgroup_system_usage = cgroup_system_ticks*1.0/dividecpu;
		double cgroup_user_usage = cgroup_user_ticks*1.0/dividecpu;
		//printf("cgroup_system_ticks %lld, cgroup_user_ticks %lld dividecpu %lf\n", cgroup_system_ticks, cgroup_user_ticks, dividecpu);
		double cgroup_total_usage = cgroup_system_usage + cgroup_user_usage;
		if (!cgroup_system_usage && !cgroup_user_usage)
			cgroup_total_usage = 0;
		else if (!cgroup_total_usage)
		{
			snprintf(syspath, 255, "%s/fs/cgroup/cpuacct/cpuacct.usage", ac->system_sysfs);
			cgroup_total_usage = getkvfile(syspath)*cfs_period/1000000000/cfs_quota*100.0;
		}

		uint64_t time_system = sccs->system / dividecpu;
		uint64_t time_user = sccs->user / dividecpu;
		uint64_t sccs_total = time_system + time_user;

		//printf("cgroup CPU: usage %lf, user %lf, system %lf\n", cgroup_total_usage, cgroup_user_usage, cgroup_system_usage);
		metric_add_labels("cpu_usage_cgroup", &cgroup_system_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
		metric_add_labels("cpu_usage_cgroup", &cgroup_user_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
		metric_add_labels("cpu_usage_cgroup", &cgroup_total_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "total");
		metric_add_labels("cpu_usage", &cgroup_total_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "total");
		metric_add_labels("cpu_usage", &cgroup_user_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
		metric_add_labels("cpu_usage", &cgroup_system_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
		metric_add_labels("cpu_usage_time", &sccs->system, DATATYPE_UINT, ac->system_carg, "type", "system");
		metric_add_labels("cpu_usage_time", &sccs_total, DATATYPE_UINT, ac->system_carg, "type", "total");
		metric_add_labels("cpu_usage_time", &sccs->user, DATATYPE_UINT, ac->system_carg, "type", "user");

		if (ac->system_cpuavg)
		{
			cpu_avg_push(cgroup_total_usage);
		}
	}
	r_time ts_end = setrtime();
	int64_t scrape_time = getrtime_ns(ts_start, ts_end);
	double diff_time = getrtime_ns(ac->last_time_cpu, ts_start)/1000000000.0;
	if (ac->log_level > 2)
		printf("system scrape metrics: base: get_cpu time execute '%"d64", diff '%lf'\n", scrape_time, diff_time);

	ac->last_time_cpu = ts_start;
	metric_add_auto("cpu_usage_calc_delta_time", &diff_time, DATATYPE_DOUBLE, ac->system_carg);

	uint64_t sec = ts_end.sec;
	metric_add_auto("time_now", &sec, DATATYPE_UINT, ac->system_carg);
}

ulimit_pid_stat* get_pid_ulimit_stat(char *path)
{
	FILE *fd = fopen(path, "r");
	if (!fd)
		return NULL;

	char ulimit[1024];
	ulimit_pid_stat *ups = calloc(1, sizeof(*ups));
	while (fgets(ulimit, 1024, fd))
	{
		if (!strncmp(ulimit, "Max data size", 13))
		{
			char *tmp = ulimit+13 + strspn(ulimit+13, " \t\r\n");
			if (*tmp != 'u')
				ups->datasize = strtoull(tmp, NULL, 10) * 1024;
		}
		else if (!strncmp(ulimit, "Max stack size", 14))
		{
			char *tmp = ulimit+14 + strspn(ulimit+14, " \t\r\n");
			if (*tmp != 'u')
				ups->stacksize = strtoull(tmp, NULL, 10) * 1024;
		}
		else if (!strncmp(ulimit, "Max resident set", 16))
		{
			char *tmp = ulimit+16 + strspn(ulimit+16, " \t\r\n");
			if (*tmp != 'u')
				ups->rsssize = strtoull(tmp, NULL, 10) * 1024;
		}
		else if (!strncmp(ulimit, "Max open files", 14))
		{
			char *tmp = ulimit+14 + strspn(ulimit+14, " \t\r\n");
			if (*tmp != 'u')
				ups->openfiles = strtoull(tmp, NULL, 10);
		}
		else if (!strncmp(ulimit, "Max locked memory", 17))
		{
			char *tmp = ulimit+17 + strspn(ulimit+17, " \t\r\n");
			if (*tmp != 'u')
				ups->lockedsize = strtoull(tmp, NULL, 10) * 1024;
		}
		else if (!strncmp(ulimit, "Max address space", 17))
		{
			char *tmp = ulimit+17 + strspn(ulimit+17, " \t\r\n");
			if (*tmp != 'u')
				ups->addressspace = strtoull(tmp, NULL, 10) * 1024;
		}
	}
	if (ac->log_level > 2)
	{
		printf("rlimit: %s: datasize = %"PRIu64"\n", path, ups->datasize);
		printf("rlimit: %s: stacksize = %"PRIu64"\n", path, ups->stacksize);
		printf("rlimit: %s: rsssize = %"PRIu64"\n", path, ups->rsssize);
		printf("rlimit: %s: openfiles = %"PRIu64"\n", path, ups->openfiles);
		printf("rlimit: %s: lockedsize = %"PRIu64"\n", path, ups->lockedsize);
		printf("rlimit: %s: addressspace = %"PRIu64"\n", path, ups->addressspace);
	}
	fclose(fd);
	return ups;
}

void get_process_extra_info(char *file, char *name, char *pid, ulimit_pid_stat* ups)
{
	FILE *fd = fopen(file, "r");
	if (!fd)
		return;
	char tmp[LINUXFS_LINE_LENGTH];
	char key[LINUXFS_LINE_LENGTH];
	char val[LINUXFS_LINE_LENGTH];
	int64_t ival = 1;
	int64_t ctxt_switches = 0;

	r_time time = setrtime();
	uint64_t uptime = time.sec - get_file_atime(file);

	metric_add_labels2("process_uptime", &uptime, DATATYPE_UINT, ac->system_carg, "name", name, "pid", pid);

	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		size_t tmp_len = strlen(tmp)-1;
		tmp[tmp_len] = 0;
		int64_t i = strcspn(tmp, " \t");
		strlcpy(key, tmp, i+1);

		int swap = strspn(tmp+i, " \t")+i;
		for (; tmp[i] && (tmp[i]!=' ' || tmp[i]!='\t'); i++);
		strlcpy(val, tmp+swap, i-swap+1);

		ival = atoll(val);

		if ( strstr(tmp+swap, "kB") )
			ival *= 1024;

		if  ( !strncmp(key, "Threads", 7) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "threads", "name", name, "pid", pid);
		else if (!strncmp(key, "voluntary_ctxt_switches", 23))
		{
			if ( ctxt_switches != 0 )
			{
				ctxt_switches += ival;
				metric_add_labels3("process_stats", &ctxt_switches, DATATYPE_INT, ac->system_carg, "type", "ctx_switches", "name", name, "pid", pid);
			}
			else
			{
				ctxt_switches += ival;
			}
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "voluntary_ctx_switches", "name", name, "pid", pid);
		}
		else if (!strncmp(key, "nonvoluntary_ctxt_switches", 26))
		{
			if ( ctxt_switches != 0 )
			{
				ctxt_switches += ival;
				metric_add_labels3("process_stats", &ctxt_switches, DATATYPE_INT, ac->system_carg, "type", "ctx_switches", "name", name, "pid", pid);
			}
			else
			{
				ctxt_switches += ival;
			}
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "nonvoluntary_ctx_switches", "name", name, "pid", pid);
		}
		else if ( !strncmp(key, "VmSwap", 6) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "swap_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "VmLib", 5) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "lib_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "VmExe", 5) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "executable_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "VmStk", 5) )
		{
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "stack_bytes", "name", name, "pid", pid);
			if (ups && ups->stacksize)
			{
				metric_add_labels3("process_rlimit", &ups->stacksize, DATATYPE_UINT, ac->system_carg, "type", "stack_bytes", "name", name, "pid", pid);
				double usage = ival * 1.0 / ups->stacksize;
				metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "stack_bytes", "name", name, "pid", pid);
			}
		}
		else if ( !strncmp(key, "VmData", 6) )
		{
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "data_bytes", "name", name, "pid", pid);
			if (ups && ups->datasize)
			{
				metric_add_labels3("process_rlimit", &ups->datasize, DATATYPE_UINT, ac->system_carg, "type", "data_bytes", "name", name, "pid", pid);
				double usage = ival * 1.0 / ups->datasize;
				metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "data_bytes", "name", name, "pid", pid);
			}
		}
		else if ( !strncmp(key, "VmLck", 5) )
		{
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "lock_bytes", "name", name, "pid", pid);
			if (ups && ups->lockedsize)
			{
				metric_add_labels3("process_rlimit", &ups->lockedsize, DATATYPE_UINT, ac->system_carg, "type", "lock_bytes", "name", name, "pid", pid);
				double usage = ival * 1.0 / ups->lockedsize;
				metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "lock_bytes", "name", name, "pid", pid);
			}
		}
		else if ( !strncmp(key, "RssAnon", 7) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "anon_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "RssFile", 7) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "file_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "VmRSS", 5) )
		{
			metric_add_labels3("process_memory", &ival, DATATYPE_INT, ac->system_carg, "type", "rss", "name", name, "pid", pid);
			if (ups && ups->rsssize)
			{
				metric_add_labels3("process_rlimit", &ups->rsssize, DATATYPE_UINT, ac->system_carg, "type", "rss", "name", name, "pid", pid);
				double usage = ival * 1.0 / ups->rsssize;
				metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "rss", "name", name, "pid", pid);
			}
		}
		else if ( !strncmp(key, "VmSize", 6) )
		{
			metric_add_labels3("process_memory", &ival, DATATYPE_INT, ac->system_carg, "type", "vsz", "name", name, "pid", pid);
			if (ups && ups->addressspace)
			{
				metric_add_labels3("process_rlimit", &ups->addressspace, DATATYPE_UINT, ac->system_carg, "type", "vsz", "name", name, "pid", pid);
				double usage = ival * 1.0 / ups->addressspace;
				metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "vsz", "name", name, "pid", pid);
			}
		}
		else if ( !strncmp(key, "RssShmem", 8) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "shmem_bytes", "name", name, "pid", pid);
		else	continue;
	}

	fclose(fd);
}


void get_proc_info(char *szFileName, char *exName, char *pid_number, int8_t lightweight, process_states *states, int8_t match)
{
	char szStatStr[LINUXFS_LINE_LENGTH];
	char		state;
	int64_t	utime;
	int64_t	stime;
	int64_t	cutime;
	int64_t	cstime;

	FILE *fp = fopen(szFileName, "r");

	if ( !fp )
		return;

	if (!fgets(szStatStr, LINUXFS_LINE_LENGTH, fp))
	{
		fclose (fp);
		return;
	}

	char *t;
	t = strchr (szStatStr, ')');
	size_t sz = strlen(t);
	int64_t cursor = 0;
	int64_t val = 1;
	int64_t unval = 0;

	int cnt = 10;
	while (cnt--)
	{
		if (cnt == 8)
		{
			state = t[2];
			if (state == 'R')
			{
				metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				++states->running;
			}
			else if (state == 'S')
			{
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
				metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				++states->sleeping;
			}
			else if (state == 'D')
			{
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
				metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				++states->uninterruptible;
			}
			else if (state == 'Z')
			{
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
				metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				++states->zombie;
			}
			else if (state == 'T')
			{
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
				metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
				metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				++states->stopped;
			}

			if (lightweight || !match)
			{
				fclose (fp);
				return;
			}
			int_get_next(t+4, sz, ' ', &cursor);
		}
		else if (cnt == 3)
		{
			int64_t val = int_get_next(t+4, sz, ' ', &cursor);
			metric_add_labels3("process_page_faults", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "minor");
		}
		else if (cnt == 1)
		{
			int64_t val = int_get_next(t+4, sz, ' ', &cursor);
			metric_add_labels3("process_page_faults", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "major");
		}
		else
			int_get_next(t+4, sz, ' ', &cursor);
			//printf("%d, cnt: %d\n", int_get_next(t+4, sz, ' ', &cursor), cnt);
	}
	utime = int_get_next(t+4, sz, ' ', &cursor);
	stime = int_get_next(t+4, sz, ' ', &cursor);
	cutime = int_get_next(t+4, sz, ' ', &cursor);
	cstime = int_get_next(t+4, sz, ' ', &cursor);

	cnt = 5;
	while (cnt--)
		int_get_next(t+4, sz, ' ', &cursor);

	fclose (fp);

	//int64_t Hertz = sysconf(_SC_CLK_TCK);
	int64_t stotal_time = stime + cstime;
	int64_t utotal_time = utime + cutime;
	int64_t total_time = stotal_time + utotal_time;

	metric_add_labels3("process_cpu", &stotal_time, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "system");
	metric_add_labels3("process_cpu", &utotal_time, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "user");
	metric_add_labels3("process_cpu", &total_time, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "total");
}

int process_fdescriptors_compare(const void* arg, const void* obj)
{
	uint32_t s1 = *(uint32_t*)arg;
	uint32_t s2 = ((process_fdescriptors*)obj)->fd;
	return s1 != s2;
}

int userprocess_compare(const void* arg, const void* obj)
{
	uint32_t s1 = *(uint32_t*)arg;
	uint32_t s2 = ((userprocess_node*)obj)->uid;
	return s1 != s2;
}

void process_fdescriptors_free(void *funcarg, void* arg)
{
	process_fdescriptors *fdescriptors = arg;
	//printf("DEB: free %p: %llu\n", fdescriptors, fdescriptors->fd);
	if (!fdescriptors)
		return;

	if (fdescriptors->procname)
	{
		// SF
		//printf("DEB: procname %p/%s\n", fdescriptors, fdescriptors->procname);
		free(fdescriptors->procname);
		fdescriptors->procname = NULL;
	}

	//alligator_ht_remove_existing(funcarg, &(fdescriptors->node));
	free(fdescriptors);
}

void get_proc_socket_number(char *path, char *procname)
{
	char buf[255];
	ssize_t len = readlink(path, buf, 254);
	if (len < 0)
	{
		if (ac->log_level > 1)
			perror("readlink: ");
		return;
	}

	buf[len] = 0;
	char *cur = buf;
	if (*cur != 's')
		return;
	uint64_t i;
	for (i = 0; i < len && !isdigit(*cur); ++cur, i++);
	uint32_t fdesc = strtoull(cur, NULL, 10);
	//printf("%s/%s: %"PRIu32"\n", buf, cur, fdesc);

	alligator_ht *hash = ac->fdesc;
	if (!hash)
	{
		hash = ac->fdesc = alligator_ht_init(NULL);
	}

	uint32_t fdesc_hash = tommy_inthash_u32(fdesc);
	process_fdescriptors *fdescriptors = alligator_ht_search(hash, process_fdescriptors_compare, &fdesc, fdesc_hash);
	if (!fdescriptors)
	{
		fdescriptors = malloc(sizeof(*fdescriptors));
		fdescriptors->fd = fdesc;
		fdescriptors->procname = strdup(procname);
		//printf("DEB: alloc %p: %llu with name %s\n", fdescriptors, fdescriptors->fd, fdescriptors->procname);
		alligator_ht_insert(hash, &(fdescriptors->node), fdescriptors, fdesc_hash);
	}
	else
	{
		if (strcmp(fdescriptors->procname, procname))
		{
			//printf("DEB: realloc %p: %llu from name %s to %s\n", fdescriptors, fdescriptors->fd, fdescriptors->procname, procname);
			free(fdescriptors->procname);
			fdescriptors->procname = strdup(procname);
		}
	}
}

int64_t get_fd_info_process(char *fddir, char *procname)
{
	char buf[255];
	size_t buf_size = strlcpy(buf, fddir, 255);
	char *bufcur = buf+buf_size;

	struct dirent *entry;
	DIR *dp;

	dp = opendir(fddir);
	if (!dp)
	{
		//perror("opendir");
		return 0;
	}

	//char dir[FILENAME_MAX];
	int64_t i = 0;
	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;

		if (ac->system_network)
		{
			strcpy(bufcur, entry->d_name);
			get_proc_socket_number(buf, procname);
		}

		i++;
	}

	closedir(dp);
	return i;
}

void get_process_io_stat(char *file, char *command, char *pid)
{
	FILE *fd = fopen(file, "r");
	if (!fd)
		return;

	char	buf[LINUXFS_LINE_LENGTH],
		buf2[LINUXFS_LINE_LENGTH];
	int64_t val;
	while (fgets(buf, LINUXFS_LINE_LENGTH, fd))
	{
		sscanf(buf, "%[^:]: %"d64"", buf2, &val);
		metric_add_labels3("process_io", &val, DATATYPE_INT, ac->system_carg, "name", command, "type", buf2, "pid", pid);
	}
	fclose(fd);
}

void schedstat_process_info(char *pid, char *name)
{
	char fpath[1000];
	char buf[1000];
	char *tmp;
	snprintf(fpath, 1000, "%s/%s/schedstat", ac->system_procfs, pid);

	FILE *sched_fd = fopen(fpath, "r");
	if (!sched_fd)
		return;

	if (!fgets(buf, 1000, sched_fd))
	{
		perror("fgets:");
		fclose(sched_fd);
		return;
	}
	fclose(sched_fd);

	tmp = buf;
	uint64_t run_time = strtoull(tmp, &tmp, 10);

	tmp += strcspn(tmp, " \t");
	tmp += strspn(tmp, " \t");
	uint64_t runqueue_time = strtoull(tmp, &tmp, 10);

	tmp += strcspn(tmp, " \t");
	tmp += strspn(tmp, " \t");
	uint64_t run_periods = strtoull(tmp, &tmp, 10);

	metric_add_labels2("process_schedstat_run_time", &run_time, DATATYPE_UINT, ac->system_carg, "name", name, "pid", pid);
	metric_add_labels2("process_schedstat_runqueue_time", &runqueue_time, DATATYPE_UINT, ac->system_carg, "name", name, "pid", pid);
	metric_add_labels2("process_schedstat_run_periods", &run_periods, DATATYPE_UINT, ac->system_carg, "name", name, "pid", pid);
}

int get_pid_info(char *pid, int64_t *allfilesnum, int8_t lightweight, process_states *states, int8_t need_match)
{
	char dir[FILENAME_MAX];
	uint64_t rc;

	// get comm name
	snprintf(dir, FILENAME_MAX, "%s/%s/comm", ac->system_procfs, pid);
	FILE *fd = fopen(dir, "r");
	if (!fd)
		return 0;

	char procname[_POSIX_PATH_MAX];
	if(!fgets(procname, _POSIX_PATH_MAX, fd))
	{
		fclose(fd);
		return 0;
	}
	size_t procname_size = strlen(procname)-1;
	procname[procname_size] = '\0';
	fclose(fd);

	// get cmdline
	snprintf(dir, FILENAME_MAX, "%s/%s/cmdline", ac->system_procfs, pid);
	//printf("read '%s'\n", dir);
	fd = fopen(dir, "r");
	if (!fd)
		return 0;

	size_t cmdline_size = 16384;
	char cmdline[cmdline_size];
	memset(cmdline, 0, cmdline_size);
	cmdline[0] = 0;
	if(!(rc=fread(cmdline, 1, cmdline_size, fd)))
	{
		cmdline[0] = 0;
		cmdline_size = 0;
	}
	else
	{
		for(int64_t iter = 0; iter < rc-1 && iter < cmdline_size - 1; iter++)
			if (!cmdline[iter])
				cmdline[iter] = ' ';
		cmdline[cmdline_size - 1] = 0;
		cmdline_size = strlen(cmdline);
	}

	fclose(fd);

	//printf("cmdline(%zu) is '%s'\n", cmdline_size, cmdline);
	int8_t match = 1;
	if (need_match)
		if (!match_mapper(ac->process_match, procname, procname_size, procname))
			if (!match_mapper(ac->process_match, cmdline, cmdline_size, procname))
				match = 0;

	snprintf(dir, FILENAME_MAX, "%s/%s/stat", ac->system_procfs, pid);
	get_proc_info(dir, procname, pid, lightweight, states, match);

	snprintf(dir, FILENAME_MAX, "%s/%s/fd/", ac->system_procfs, pid);
	int64_t filesnum = get_fd_info_process(dir, procname);
	if (match && filesnum && !lightweight)
		metric_add_labels3("process_stats", &filesnum, DATATYPE_INT, ac->system_carg, "name", procname, "type", "open_files", "pid", pid);
	*allfilesnum += filesnum;

	if (!match)
		return 1;

	if (lightweight)
		return 1;

	schedstat_process_info(pid, procname);

	snprintf(dir, FILENAME_MAX, "%s/%s/limits", ac->system_procfs, pid);
	ulimit_pid_stat* ups = get_pid_ulimit_stat(dir);
	if (ups && ups->openfiles)
	{
		metric_add_labels3("process_rlimit", &ups->openfiles, DATATYPE_UINT, ac->system_carg, "type", "open_files", "name", procname, "pid", pid);
		double usage = filesnum * 1.0 / ups->openfiles;
		metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "open_files", "name", procname, "pid", pid);
		
	}

	snprintf(dir, FILENAME_MAX, "%s/%s/status", ac->system_procfs, pid);
	get_process_extra_info(dir, procname, pid, ups);
	if (ups)
		free(ups);

	snprintf(dir, FILENAME_MAX, "%s/%s/io", ac->system_procfs, pid);
	get_process_io_stat(dir, procname, pid);

	return 1;
}

void pidfile_push(char *file, int type)
{
	if (!ac->system_pidfile)
		return;

	pidfile_node *node = NULL;
	if (!ac->system_pidfile->head)
	{
		//node = ac->system_pidfile->head = ac->system_pidfile->tail = calloc(1, sizeof(*ac->system_pidfile->head));
		node = calloc(1, sizeof(pidfile_node));
		ac->system_pidfile->head = ac->system_pidfile->tail = node;
	}
	else
	{
		//node = ac->system_pidfile->tail->next = calloc(1, sizeof(pidfile_node));
		node = calloc(1, sizeof(pidfile_node));
		ac->system_pidfile->tail->next = node;
	}

	if (!node)
		return;

	node->pidfile = strdup(file);
	node->type = type;
	ac->system_pidfile->tail = node;
}

void pidfile_del(char *file, int type)
{
	if (!ac->system_pidfile)
		return;

	pidfile_node *prev = NULL;
	pidfile_node *node = ac->system_pidfile->head;
	if (!node)
		return;

	while (node)
	{
		if (!strcmp(node->pidfile, file))
		{
			if (ac->system_pidfile->head == node)
				ac->system_pidfile->head = node->next;
			if (ac->system_pidfile->tail == node)
				ac->system_pidfile->tail = NULL;
			if (prev)
				prev->next = node->next;

			free(node->pidfile);
			free(node);
		}

		prev = node;
		node = node->next;
	}
}

void simple_pidfile_scrape(char *find_pid)
{
	if (ac->log_level > 1)
		printf("PIDfile check %s\n", find_pid);

	string* pid = get_file_content(find_pid);
	if (!pid)
		return;

	int64_t allfilesnum = 0;
	process_states *states = calloc(1, sizeof(*states));

	char pid_strict[6];
	size_t pid_size = strspn(pid->s, "0123456789") + 1;
	size_t copy_size = pid_size > 6 ? 6 : pid_size;
	strlcpy(pid_strict, pid->s, copy_size);
	
	if (ac->log_level > 1)
		printf("check PID '%s'\n", pid_strict);

	uint64_t rc = get_pid_info(pid_strict, &allfilesnum, 0, states, 0);
	metric_add_labels("process_match", &rc, DATATYPE_UINT, ac->system_carg, "name", find_pid);
	string_free(pid);
	free(states);
	
}

void cgroup_procs_scrape(char *cgroup_path)
{
	if (ac->log_level > 1)
		printf("Cgroup procs file check %s\n", cgroup_path);

	FILE *fd = fopen(cgroup_path, "r");
	if (!fd)
		return;

	char pid[10];
	int64_t rc = 0;
	int64_t allfilesnum = 0;
	process_states *states = calloc(1, sizeof(*states));

	while (fgets(pid, 10, fd))
	{
		char pid_strict[6];
		size_t pid_size = strspn(pid, "0123456789") + 1;
		size_t copy_size = pid_size > 6 ? 6 : pid_size;
		strlcpy(pid_strict, pid, copy_size);

		if (ac->log_level > 1)
			 printf("check PID '%s' from '%s'\n", pid_strict, cgroup_path);

		rc += get_pid_info(pid_strict, &allfilesnum, 0, states, 0);
	}
	metric_add_labels("process_match", &rc, DATATYPE_UINT, ac->system_carg, "name", cgroup_path);
	free(states);
	fclose(fd);
}

// 0 is classic pidfile
// 1 is cgroup.procs file with many pids
void get_pidfile_stats()
{
	if (!ac->system_pidfile)
		return;

	pidfile_node *node = ac->system_pidfile->head;
	while (node)
	{
		if (node->type == 0)
			simple_pidfile_scrape(node->pidfile);
		else if (node->type == 1)
		{
			char cgrouppath[1024];
			snprintf(cgrouppath, 1023, "%s/%s/cgroup.procs", ac->system_sysfs, node->pidfile);
			cgroup_procs_scrape(cgrouppath);
		}

		node = node->next;
	}
}

void userprocess_push(alligator_ht *userprocess, char *user)
{
	if (!userprocess)
		return;

	uid_t uid = get_uid_by_username(user);
	userprocess_node *upn = alligator_ht_search(userprocess, userprocess_compare, &uid, uid);
	if (upn)
		return;

	upn = calloc(1, sizeof(*upn));
	upn->uid = uid;
	upn->name = strdup(user);

	alligator_ht_insert(userprocess, &(upn->node), upn, upn->uid);
}

void userprocess_del(alligator_ht* userprocess, char *user)
{
	if (!userprocess)
		return;

	uid_t uid = get_uid_by_username(user);
	userprocess_node *upn = alligator_ht_search(userprocess, userprocess_compare, &uid, uid);
	if (!upn)
		return;

	alligator_ht_remove_existing(userprocess, &(upn->node));

	free(upn->name);
	free(upn);
}

void find_pid(int8_t lightweight)
{
	if (ac->log_level > 2)
		puts("system scrape metrics: processes");


	struct dirent *entry;
	DIR *dp;

	dp = opendir(ac->system_procfs);
	if (!dp)
	{
		//perror("opendir");
		return;
	}

	process_states *states = calloc(1, sizeof(*states));
	int64_t allfilesnum = 0;

	uint64_t tasks = 0;
	uint64_t processes = 0;
	while((entry = readdir(dp)))
	{
		if ( !isdigit(entry->d_name[0]) )
			continue;

		++tasks;
		++processes;

		get_pid_info(entry->d_name, &allfilesnum, lightweight, states, 1);
	}

	metric_add_labels("process_states", &states->running, DATATYPE_UINT, ac->system_carg, "state", "running");
	metric_add_labels("process_states", &states->sleeping, DATATYPE_UINT, ac->system_carg, "state", "sleeping");
	metric_add_labels("process_states", &states->uninterruptible, DATATYPE_UINT, ac->system_carg, "state", "uninterruptible");
	metric_add_labels("process_states", &states->zombie, DATATYPE_UINT, ac->system_carg, "state", "zombie");
	metric_add_labels("process_states", &states->stopped, DATATYPE_UINT, ac->system_carg, "state", "stopped");
	metric_add_auto("open_files_process", &allfilesnum, DATATYPE_INT, ac->system_carg);
	metric_add_auto("tasks_total", &tasks, DATATYPE_UINT, ac->system_carg);
	metric_add_auto("processes_total", &processes, DATATYPE_UINT, ac->system_carg);

	char threadmax[255];
	snprintf(threadmax, 255, "%s/sys/kernel/threads-max", ac->system_procfs);
	int64_t threads_max = getkvfile(threadmax);
	metric_add_auto("tasks_max", &threads_max, DATATYPE_INT, ac->system_carg);

	double threads_usage = tasks * 100.0 / threads_max;
	metric_add_auto("tasks_usage", &threads_usage, DATATYPE_DOUBLE, ac->system_carg);
	free(states);

	closedir(dp);
}

void get_mem(int8_t platform)
{
	if (ac->log_level > 2)
		puts("system scrape metrics: base: mem");

	char pathbuf[255];
	snprintf(pathbuf, 255, "%s/meminfo", ac->system_procfs);

	FILE *fd = fopen(pathbuf, "r");
	if (!fd)
		return;

	char tmp[LINUXFS_LINE_LENGTH];
	char key[LINUXFS_LINE_LENGTH];
	char key_map[LINUXFS_LINE_LENGTH];
	char val[LINUXFS_LINE_LENGTH];
	int64_t ival = 1;
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
	int64_t dirty = 0;
	int64_t pgpgin = 0;
	int64_t pgpgout = 0;
	int64_t pgmajfault = 0;
	int64_t pgfault = 0;
	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		size_t strlen_tmp = strlen(tmp) - 1;
		tmp[strlen_tmp] = 0;
		int i;
		for (i=0; tmp[i]!=' '; i++);
		strlcpy(key, tmp, i);

		for (; i < strlen_tmp && tmp[i]==' '; i++);
		int swap = i;
		for (; i < strlen_tmp && tmp[i]!=' '; i++);
		strlcpy(val, tmp+swap, i-swap+1);

		if ( strstr(tmp+swap, "kB") )
			ival = 1024;

		ival = ival * atoll(val);

		if	( !strcmp(key, "MemTotal") ) {
			strlcpy(key_map, "total", 6);
			memtotal = ival;
		}
		//else if ( !strcmp(key, "MemFree") ) {
		//	strlcpy(key_map, "free", 5);
		//	memfree = ival;
		//}
		else if ( !strcmp(key, "MemAvailable") ) {
			strlcpy(key_map, "available", 9);
			memavailable = ival;
		}
		else if ( !strcmp(key, "Inactive") ) {
			strlcpy(key_map, "inactive", 9);
			inactive = ival;
		}
		else if ( !strcmp(key, "Active") ) {
			strlcpy(key_map, "active", 7);
			active = ival;
		}
		else if ( !strcmp(key, "Inactive(anon)") ) {
			strlcpy(key_map, "inactive_anon", 14);
			inactive_anon = ival;
		}
		else if ( !strcmp(key, "Active(anon)") ) {
			strlcpy(key_map, "active_anon", 12);
			active_anon = ival;
		}
		else if ( !strcmp(key, "Inactive(file)") ) {
			strlcpy(key_map, "inactive_anon", 14);
			inactive_file = ival;
		}
		else if ( !strcmp(key, "Active(file)") ) {
			strlcpy(key_map, "active_anon", 12);
			active_file = ival;
		}
		else if ( !strcmp(key, "SwapTotal") ) {
			strlcpy(key_map, "swap_total", 11);
			totalswap = ival;
		}
		else if ( !strcmp(key, "SwapFree") ) {
			strlcpy(key_map, "swap_free", 10);
			freeswap = ival;
		}
		else if ( !strcmp(key, "Buffers") ) {
			strlcpy(key_map, "buffers", 8);
		}
		else if ( !strcmp(key, "Cached") ) {
			strlcpy(key_map, "cache", 6);
			cache = ival;
		}
		else if ( !strcmp(key, "Mapped") ) {
			strlcpy(key_map, "mapped", 7);
			mapped = ival;
		}
		else if ( !strcmp(key, "Unevictable") ) {
			strlcpy(key_map, "unevictable", 12);
			unevictable = ival;
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
		tmp[strlen(tmp)-1] = 0;
		int i;
		for (i=0; tmp[i]!=' '; i++);
		strlcpy(key, tmp, i+1);

		for (; tmp[i]==' '; i++);
		int swap = i;
		for (; tmp[i] && tmp[i]!=' '; i++);
		strlcpy(val, tmp+swap, i-swap+1);

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
		else if (!platform && !strcmp(key, "pswpin"))
			metric_add_labels("memory_stat", &ival, DATATYPE_INT, ac->system_carg, "type", "pswpin");
		else if (!platform && !strcmp(key, "pswpout"))
			metric_add_labels("memory_stat", &ival, DATATYPE_INT, ac->system_carg, "type", "pswpout");
		else if (!platform && !strncmp(key, "numa_", 5))
			metric_add_labels("numa_stat", &ival, DATATYPE_INT, ac->system_carg, "type", key+5);
		else if (!platform && !strncmp(key, "pgscan_", 7))
			metric_add_labels("pgscan", &ival, DATATYPE_INT, ac->system_carg, "type", key+7);
		else if (!platform && !strncmp(key, "pgsteal_", 8))
			metric_add_labels("pgsteal", &ival, DATATYPE_INT, ac->system_carg, "type", key+8);
		else if (!platform && !strncmp(key, "kswapd_", 7))
			metric_add_labels("pgsteal", &ival, DATATYPE_INT, ac->system_carg, "type", key+7);
	}
	fclose(fd);

	// scrape cgroup
	snprintf(pathbuf, 255, "%s/fs/cgroup/memory/memory.stat", ac->system_sysfs);
	fd = fopen(pathbuf, "r");
	if (!fd)
		return;

	ival = 1;
	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		tmp[strlen(tmp)-1] = 0;
		int i;
		for (i=0; tmp[i]!=' '; i++);
		strlcpy(key, tmp, i+1);

		for (; tmp[i]==' '; i++);
		int swap = i;
		for (; tmp[i] && tmp[i]!=' '; i++);
		strlcpy(val, tmp+swap, i-swap+1);

		ival = atoll(val);
		int8_t cgroup = platform;

		if	(!strcmp(key, "total_cache")) {
			strlcpy(key_map, "cache", 6);
			cache = cgroup ? ival : cache;
			metric_add_labels("memory_usage", &cache, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_rss")) {
			strlcpy(key_map, "usage", 6);
			usagemem = cgroup ? ival : usagemem;
			double percentused = (double)usagemem*100/(double)memtotal;
			double percentfree = 100 - percentused;
			metric_add_labels("memory_usage", &usagemem, DATATYPE_INT, ac->system_carg, "type", key_map);
			metric_add_labels("memory_usage_percent", &percentused, DATATYPE_DOUBLE, ac->system_carg, "type", "used");
			metric_add_labels("memory_usage_percent", &percentfree, DATATYPE_DOUBLE, ac->system_carg, "type", "free");
		}
		else if (!strcmp(key, "total_mapped_file")) {
			strlcpy(key_map, "mapped", 7);
			mapped = cgroup ? ival : mapped;
			metric_add_labels("memory_usage", &mapped, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_dirty")) {
			strlcpy(key_map, "dirty", 6);
			dirty = cgroup ? ival : dirty;
			metric_add_labels("memory_usage", &dirty, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_unevictable")) {
			strlcpy(key_map, "unevictable", 12);
			unevictable = cgroup ? ival : unevictable;
			metric_add_labels("memory_usage", &unevictable, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_active_anon")) {
			strlcpy(key_map, "active_anon", 12);
			active_anon = cgroup ? ival : active_anon;
			metric_add_labels("memory_usage", &active_anon, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_inactive_anon")) {
			strlcpy(key_map, "inactive_anon", 14);
			inactive_anon = cgroup ? ival : inactive_anon;
			metric_add_labels("memory_usage", &inactive_anon, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_active_file")) {
			strlcpy(key_map, "active_file", 12);
			active_file = cgroup ? ival : active_file;
			metric_add_labels("memory_usage", &active_file, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_inactive_file")) {
			strlcpy(key_map, "inactive_file", 14);
			inactive_file = cgroup ? ival : inactive_file;
			metric_add_labels("memory_usage", &inactive_file, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_pgpgin")) {
			strlcpy(key_map, "pgpgin", 7);
			pgpgin = cgroup ? ival : pgpgin;
			metric_add_labels("memory_stat", &pgpgin, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_pgpgout")) {
			strlcpy(key_map, "pgpgout", 8);
			pgpgout = cgroup ? ival : pgpgout;
			metric_add_labels("memory_stat", &pgpgout, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_pgfault")) {
			strlcpy(key_map, "pgfault", 8);
			pgfault = cgroup ? ival : pgfault;
			metric_add_labels("memory_stat", &pgfault, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "total_pgmajfault")) {
			strlcpy(key_map, "pgmajfault", 11);
			pgmajfault = cgroup ? ival : pgmajfault;
			metric_add_labels("memory_stat", &pgmajfault, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else if (!strcmp(key, "hierarchical_memory_limit")) {
			strlcpy(key_map, "total", 6);
			memtotal = memtotal > ival ? ival : memtotal;
			metric_add_labels("memory_usage", &memtotal, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
		else	continue;

		metric_add_labels("memory_usage_cgroup", &ival, DATATYPE_INT, ac->system_carg, "type", key_map);
	}
	fclose(fd);

	inactive = inactive_file+inactive_anon;
	active = active_file+active_anon;
	metric_add_labels("memory_usage", &active, DATATYPE_INT, ac->system_carg, "type", "active");
	metric_add_labels("memory_usage", &inactive, DATATYPE_INT, ac->system_carg, "type", "inactive");

	snprintf(pathbuf, 255, "%s/fs/cgroup/memory/memory.oom_control", ac->system_sysfs);
	fd = fopen(pathbuf, "r");
	if (!fd)
		return;

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

#define TCPUDP_NET_LENREAD 10000000
#define A_TCP_STATE 0
#define A_UDP_STATE 1

int port_usage_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((port_usage*)obj)->key;
        return strcmp(s1, s2);
}

void port_usage_free(void *funcarg, void* arg)
{
	port_usage *pu = arg;
	if (!pu)
		return;

	int state_proto = pu->state_proto;
	int state = pu->state;

	char *proto = state_proto == A_TCP_STATE ? "tcp" : "udp";

	if ((state_proto == A_TCP_STATE && state == 10) || (state_proto == A_UDP_STATE && state == 7))
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "LISTEN", "process", pu->process);
	else if (state == 6)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "TIME_WAIT", "process", pu->process);
	else if (state == 1)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "ESTABLISHED", "process", pu->process);
	else if (state == 2)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "SYN_SENT", "process", pu->process);
	else if (state == 4)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "SYN_RECV", "process", pu->process);
	else if (state == 4)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "FIN_WAIT1", "process", pu->process);
	else if (state == 5)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "FIN_WAIT2", "process", pu->process);
	else if (state == 7)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "CLOSE", "process", pu->process);
	else if (state == 8)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "CLOSE_WAIT", "process", pu->process);
	else if (state == 9)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "LAST_ACK", "process", pu->process);
	else if (state == 11)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "CLOSING", "process", pu->process);

	if (pu->local_port)
		free(pu->local_port);
	if (pu->remote_port)
		free(pu->remote_port);
	if (pu->local_addr)
		free(pu->local_addr);
	if (pu->remote_addr)
		free(pu->remote_addr);

	free(pu->key);
	free(pu);
}

void get_net_tcpudp(char *file, char *name, int state_proto)
{
	r_time ts_start = setrtime();
	if (ac->log_level > 2)
		printf("system scrape metrics: network: get_net_tcpudp '%s'\n", name);

	FILE *fd = fopen(file, "r");
	if (!fd)
		return;

	char *buf = malloc(TCPUDP_NET_LENREAD);

	uint8_t state;
	//uint32_t bucket;//, srcp, destp;
	char srcp[6];
	char destp[6];
	uint64_t src, dest, srcport, destport;

	alligator_ht *addr_port_usage = calloc(1, sizeof(*addr_port_usage));
	alligator_ht_init(addr_port_usage);

	char srcaddrportkey[255];
	char *start, *end;
	uint64_t send_queue = 0;
	uint64_t recv_queue = 0;

	char procsomaxconn[255];
	snprintf(procsomaxconn, 255, "%s/sys/net/core/somaxconn", ac->system_procfs);
	int64_t somaxconn = getkvfile(procsomaxconn);

	size_t rc;
	char *bufend;
	while((rc=fread(buf, 1, TCPUDP_NET_LENREAD, fd)))
	{
		bufend = buf+rc;
		start = buf;
		while(start < bufend)
		{
			end = strstr(start, "\n");
			if (!end)
				break;
			*end = 0;

			char str1[INET_ADDRSTRLEN];
			char str2[INET_ADDRSTRLEN];

			char *pEnd = start;
			pEnd += strspn(pEnd, "\t :");
			strtoul(pEnd, &pEnd, 10);
			pEnd += strspn(pEnd, "\t :");
			src = strtoul(pEnd, &pEnd, 16);
			pEnd += strspn(pEnd, "\t :");
			srcport = strtoul(pEnd, &pEnd, 16);
			pEnd += strspn(pEnd, "\t :");
			dest = strtoul(pEnd, &pEnd, 16);
			pEnd += strspn(pEnd, "\t :");
			destport = strtoul(pEnd, &pEnd, 16);
			pEnd += strspn(pEnd, "\t :");
			state = strtoul(pEnd, &pEnd, 16);

			pEnd += strspn(pEnd, "\t :");
			send_queue = strtoul(pEnd, &pEnd, 16);
			pEnd += strspn(pEnd, "\t :");
			recv_queue = strtoul(pEnd, &pEnd, 16);

			pEnd += strcspn(pEnd, " \t");
			pEnd += strspn(pEnd, " \t");
			pEnd += strcspn(pEnd, " \t");
			pEnd += strspn(pEnd, " \t");
			pEnd += strcspn(pEnd, " \t");
			pEnd += strspn(pEnd, " \t");
			pEnd += strcspn(pEnd, " \t");
			pEnd += strspn(pEnd, " \t");
			pEnd += strcspn(pEnd, " \t");

			uint32_t fdesc = strtoull(pEnd, &pEnd, 10);

			snprintf(srcp, 6, "%lu", srcport);
			snprintf(destp, 6, "%lu", destport);

			inet_ntop(AF_INET, &src, str1, INET_ADDRSTRLEN);
			inet_ntop(AF_INET, &dest, str2, INET_ADDRSTRLEN);


			process_fdescriptors *fdescriptors = NULL;
			if (ac->fdesc)
				fdescriptors = alligator_ht_search(ac->fdesc, process_fdescriptors_compare, &fdesc, tommy_inthash_u32(fdesc));

			if ((state_proto == A_TCP_STATE && state == 10) || (state_proto == A_UDP_STATE && state == 7))
			{
				uint64_t val = 1;

				// case when send_queue is unknown or larger than somaxconn
				if ((send_queue < 1) || (send_queue > somaxconn))
					send_queue = somaxconn;

				if (fdescriptors)
				{
					metric_add_labels7("socket_stat", &val, DATATYPE_UINT, ac->system_carg, "src", str1, "src_port", srcp, "dst", str2, "dst_port", destp, "state", "listen", "proto", name, "process", fdescriptors->procname);
					metric_add_labels7("socket_stat_receive_queue_length", &recv_queue, DATATYPE_UINT, ac->system_carg, "src", str1, "src_port", srcp, "dst", str2, "dst_port", destp, "state", "listen", "proto", name, "process", fdescriptors->procname);
					metric_add_labels7("socket_stat_receive_queue_limit", &send_queue, DATATYPE_UINT, ac->system_carg, "src", str1, "src_port", srcp, "dst", str2, "dst_port", destp, "state", "listen", "proto", name, "process", fdescriptors->procname);
				}
				else
				{
					metric_add_labels6("socket_stat", &val, DATATYPE_UINT, ac->system_carg, "src", str1, "src_port", srcp, "dst", str2, "dst_port", destp, "state", "listen", "proto", name);
					metric_add_labels6("socket_stat_receive_queue_length", &recv_queue, DATATYPE_UINT, ac->system_carg, "src", str1, "src_port", srcp, "dst", str2, "dst_port", destp, "state", "listen", "proto", name);
					metric_add_labels6("socket_stat_receive_queue_limit", &send_queue, DATATYPE_UINT, ac->system_carg, "src", str1, "src_port", srcp, "dst", str2, "dst_port", destp, "state", "listen", "proto", name);
				}
			}

			start = end+1;

			if (fdescriptors)
				snprintf(srcaddrportkey, 255, "%s:%d:%s", str1, state, fdescriptors->procname);
			else
				snprintf(srcaddrportkey, 255, "%s:%d", str1, state);

			uint32_t key_hash = tommy_strhash_u32(0, srcaddrportkey);
			port_usage *pu = alligator_ht_search(addr_port_usage, port_usage_compare, srcaddrportkey, key_hash);
			if (!pu)
			{
				pu = calloc(1, sizeof(*pu));
				pu->local_addr = strdup(str1);
				pu->local_port = strdup(srcp);
				pu->key = strdup(srcaddrportkey);
				pu->count = 1;
				pu->state_proto = state_proto;
				pu->state = state;
				pu->process = (fdescriptors && fdescriptors->procname) ? fdescriptors->procname : "";

				alligator_ht_insert(addr_port_usage, &(pu->node), pu, key_hash);
			}
			else
			{
				++pu->count;
			}
		}
	}
	fclose(fd);
	free(buf);

	alligator_ht_foreach_arg(addr_port_usage, port_usage_free, NULL);
	alligator_ht_done(addr_port_usage);
	free(addr_port_usage);

	r_time ts_end = setrtime();
	int64_t scrape_time = getrtime_ns(ts_start, ts_end);
	if (ac->log_level > 2)
		printf("system scrape metrics: network: get_net_tcpudp time execute '%"d64"'\n", scrape_time);
}

void get_netstat_statistics(char *ns_file)
{
	if (ac->log_level > 2)
		printf("system scrape metrics: network: netstat_statistics '%s'\n", ns_file);

	FILE *fp = fopen(ns_file, "r");
	if(!fp)
		return;

	size_t filesize = 10000;
	char bufheader[filesize];
	char bufbody[filesize];
	uint64_t header_index, body_index, header_update;
	int64_t buf_val;
	char buf[255], proto[255];

	while (1)
	{
		if (!fgets(bufheader, filesize, fp))
		{
			fclose(fp);
			return;
		}
		if (!fgets(bufbody, filesize, fp))
		{
			fclose(fp);
			return;
		}

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
			metric_add_labels2("network_stat", &buf_val, DATATYPE_INT, ac->system_carg, "proto", proto, "stat", buf);
		}
	}
	fclose(fp);
}

void get_network_statistics()
{
	if (ac->log_level > 2)
		puts("system scrape metrics: network: network_statistics");

	int64_t received_bytes;
	int64_t received_packets;
	int64_t received_err;
	int64_t received_drop;
	int64_t received_fifo;
	int64_t received_frame;
	int64_t received_compressed;
	int64_t received_multicast;

	int64_t transmit_bytes;
	int64_t transmit_packets;
	int64_t transmit_err;
	int64_t transmit_drop;
	int64_t transmit_fifo;
	int64_t transmit_colls;
	int64_t transmit_carrier;
	int64_t transmit_compressed;

	char ifdir[1000];
	char procnetdev[255];
	snprintf(procnetdev, 255, "%s/net/dev", ac->system_procfs);
	FILE *fp = fopen(procnetdev, "r");
	if (!fp)
		return;
	char buf[200], ifname[20];

	int i;
	for (i = 0; i < 2; i++) {
		if(!fgets(buf, 200, fp))
		{
			fclose(fp);
			return;
		}
	}

	for (i=0; fgets(buf, 200, fp); i++)
	{
		int from = strspn(buf, " ");
		int to = strcspn(buf+from, ":");

		if (!strncmp(buf+from, "veth", 4))
			continue;
		strlcpy(ifname, buf+from, to+1);

		char *pEnd;
		pEnd = buf+from+to + strcspn(buf+from+to, "\t ");
		pEnd += strspn(pEnd, "\t ");
		received_bytes = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_bytes, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_bytes");

		received_packets = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_packets, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_packets");

		received_err = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_err, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_err");

		received_drop = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_drop, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_drop");

		received_fifo = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_fifo, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_fifo");

		received_frame = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_frame, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_frame");

		received_compressed = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_compressed, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_compressed");

		received_multicast = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_multicast, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_multicast");

		transmit_bytes = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_bytes, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_bytes");

		transmit_packets = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_packets, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_packets");

		transmit_err = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_err, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_err");

		transmit_drop = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_drop, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_drop");

		transmit_fifo = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_fifo, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_fifo");

		transmit_colls = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_colls, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_colls");

		transmit_carrier = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_carrier, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_carrier");

		transmit_compressed = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_compressed, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_compressed");

		snprintf(ifdir, 1000, "%s/class/net/%s/speed", ac->system_sysfs, ifname);
		int64_t interface_speed_bits = getkvfile(ifdir);
		metric_add_labels2("if_stat", &interface_speed_bits, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "speed");

		//double iface_util_transmit = (transmit_bytes*100.0)/(interface_speed_bits/8.0);
		//double iface_util_received = (received_bytes*100.0)/(interface_speed_bits/8.0);
		//metric_add_labels2("if_utilization", &iface_util_transmit, DATATYPE_DOUBLE, ac->system_carg, "ifname", ifname, "type", "transmit");
		//metric_add_labels2("if_utilization", &iface_util_received, DATATYPE_DOUBLE, ac->system_carg, "ifname", ifname, "type", "received");
	}

	fclose(fp);
}

void get_nofile_stat()
{
	if (ac->log_level > 2)
		puts("system scrape metrics: base: nofile_stat");

	char filenr[255];
	snprintf(filenr, 255, "%s/sys/fs/file-nr", ac->system_procfs);
	FILE *fd = fopen(filenr, "r");
	if (!fd)
		return;

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
		metric_add_auto("open_files", &file_open, DATATYPE_INT, ac->system_carg);
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
	if (ac->log_level > 2)
		puts("system scrape metrics: disk: io_stat");

	char diskstats[255];
	snprintf(diskstats, 255, "%s/diskstats", ac->system_procfs);
	FILE *fd = fopen(diskstats, "r");
	if (!fd)
		return;

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
		int64_t io_w = stat[3];
		int64_t io_r = stat[7];
		int64_t disk_busy = stat[12]/1000;
		metric_add_labels2("disk_io", &io_r, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "transfers_read");
		metric_add_labels2("disk_io", &io_w, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "transfers_write");
		metric_add_labels2("disk_io", &read_bytes, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "bytes_read");
		metric_add_labels2("disk_io", &write_bytes, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "bytes_write");
		metric_add_labels2("disk_io_await", &stat[10], DATATYPE_INT, ac->system_carg, "dev", devname, "type", "write");
		metric_add_labels2("disk_io_await", &stat[6], DATATYPE_INT, ac->system_carg, "dev", devname, "type", "read");
		metric_add_labels2("disk_io_await", &stat[13], DATATYPE_INT, ac->system_carg, "dev", devname, "type", "queue");
		metric_add_labels("disk_busy", &disk_busy, DATATYPE_INT, ac->system_carg, "dev", devname);
		if (j>14)
			metric_add_labels2("disk_io", &stat[14], DATATYPE_INT, ac->system_carg, "dev", devname, "type", "transfers_discard");
	}
	fclose(fd);
}

void get_loadavg()
{
	if (ac->log_level > 2)
		puts("system scrape metrics: base: loadavg");

	char loadavg[255];
	snprintf(loadavg, 255, "%s/loadavg", ac->system_procfs);
	FILE *fd = fopen(loadavg, "r");
	if (!fd)
		return;

	char str[LINUXFS_LINE_LENGTH];
	if(!fgets(str, LINUXFS_LINE_LENGTH, fd))
	{
		fclose(fd);
		return;
	}
	double load1, load5, load15;
	sscanf(str, "%lf %lf %lf", &load1, &load5, &load15);
	metric_add_labels("load_average", &load1, DATATYPE_DOUBLE, ac->system_carg, "type", "load1");
	metric_add_labels("load_average", &load5, DATATYPE_DOUBLE, ac->system_carg, "type", "load5");
	metric_add_labels("load_average", &load15, DATATYPE_DOUBLE, ac->system_carg, "type", "load15");

	fclose(fd);
}

void get_uptime()
{
	if (ac->log_level > 2)
		puts("system scrape metrics: base: uptime");
	r_time time1 = setrtime();

	time_t t = time(NULL);
	struct tm lt = {0};
	localtime_r(&t, &lt);
	time1.sec += lt.tm_gmtoff;

	char firstproc[255];
	snprintf(firstproc, 255, "%s/1", ac->system_procfs);
	uint64_t uptime = time1.sec - get_file_atime(firstproc);
	metric_add_auto("uptime", &uptime, DATATYPE_UINT, ac->system_carg);
}

void get_mdadm()
{
	if (ac->log_level > 2)
		puts("system scrape metrics: disk: mdadm");

	char mdstat[255];
	snprintf(mdstat, 255, "%s/mdstat", ac->system_procfs);
	FILE *fd;
	fd = fopen(mdstat, "r");
	if (!fd)
		return;

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
	if (ac->log_level > 2)
		printf("system scrape metrics: base: platform %d\n", mode);

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
			fclose(env);
			return PLATFORM_NSPAWN;
		}
		else if (strstr(env_str, "docker"))
		{
			if (mode)
				metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", "docker");
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
			return PLATFORM_OPENVZ;
		}
		else
		{
			if (mode)
				metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", "bare-metal");
			return 0;
		}
	}
	else
	{
		if (mode)
			metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", "bare-metal");
		return PLATFORM_BAREMETAL;
	}
	fclose(env);
	return 0;
}

void interface_stats()
{
	if (ac->log_level > 2)
		puts("system scrape metrics: network: interface_stats");

	struct dirent *entry;
	DIR *dp;

	char classpath[255];
	snprintf(classpath, 255, "%s/class/net", ac->system_sysfs);
	dp = opendir(classpath);
	if (!dp)
	{
		//perror("opendir");
		return;
	}

	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;

		if (!strncmp(entry->d_name, "veth", 4))
			continue;

		char operfile[255];
		char ifacestatistics[255];
		char operstate[100];
		snprintf(operfile, 255, "%s/class/net/%s/operstate", ac->system_sysfs, entry->d_name);
		FILE *fd = fopen(operfile, "r");
		if (!fd)
			continue;
		
		if(!fgets(operstate, 100, fd))
		{
			fclose(fd);
			continue;
		}
		fclose(fd);
		operstate[strlen(operstate)-1] = 0;

		int64_t vl = 1;

		metric_add_labels2("link_status", &vl, DATATYPE_INT, ac->system_carg, "ifname", entry->d_name, "state", operstate);

		struct dirent *entry_statistics;
		DIR *dp_statistics;
		snprintf(ifacestatistics, 255, "%s/class/net/%s/statistics/", ac->system_sysfs, entry->d_name);
		dp_statistics = opendir(ifacestatistics);
		if (!dp_statistics)
		{
			//perror("opendir");
			continue;
		}

		while((entry_statistics = readdir(dp_statistics)))
		{
			if ( entry_statistics->d_name[0] == '.' )
				continue;

			char statisticsfile[255];
			snprintf(statisticsfile, 255, "%s%s", ifacestatistics, entry_statistics->d_name);

			int64_t stat = getkvfile(statisticsfile);

			metric_add_labels2("interface_stats", &stat, DATATYPE_INT, ac->system_carg, "ifname", entry->d_name, "type", entry_statistics->d_name);
		}
		closedir(dp_statistics);
	}
	closedir(dp);
}

void get_memory_errors(char *edac)
{
	struct dirent *entry;
	DIR *dp;

	dp = opendir(edac);
	if (!dp)
		return;

	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;
		if (strncmp(entry->d_name, "csrow", 5))
			continue;

		struct dirent *rowentry;
		DIR *rowdp;

		char rowname[255];
		char chname[255];
		snprintf(rowname, 255, "%s%s/", edac, entry->d_name);
		rowdp = opendir(rowname);
		if (!rowdp)
			continue;

		while((rowentry = readdir(rowdp)))
		{
			if ( rowentry->d_name[0] == '.' )
				continue;

			if (strstr(rowentry->d_name, "_ce_count"))
			{
				snprintf(chname, 255, "%s/%s", rowname, rowentry->d_name);
				int cur = strcspn(rowentry->d_name+2, "_");
				char channel[100];
				strlcpy(channel, rowentry->d_name+2, cur+1);
				int64_t correrrors = getkvfile(chname);
				metric_add_labels3("memory_errors", &correrrors, DATATYPE_INT, ac->system_carg, "type", "correctable", "row", entry->d_name+5, "channel", channel);
			}
		}
		closedir(rowdp);

		snprintf(chname, 255, "%s/ue_count", rowname);
		int64_t uncorrerrors = getkvfile(chname);
		metric_add_labels2("memory_errors", &uncorrerrors, DATATYPE_INT, ac->system_carg, "type", "uncorrectable", "row", entry->d_name+5);
	}
	closedir(dp);
}

void get_thermal()
{
	char fname[255];
	char monname[255];
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

		snprintf(monname, 255, "%s/class/hwmon/%s/", ac->system_sysfs, entry->d_name);
		mondp = opendir(monname);
		if (!mondp)
			continue;

		// get device name
		snprintf(fname, 255, "%s/name", monname);
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
				snprintf(fname, 255, "%s/%s", monname, monentry->d_name);
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
				
				metric_add_labels3("core_temp", &temp, DATATYPE_INT, ac->system_carg, "name", name, "component", devname, "hwmon", entry->d_name);
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
		size_t node_size = strcspn(cur, " \t");
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

	fclose(fd);
}

void get_alligator_info()
{
	char genpath[255];
	char val[255];
	int64_t ival;
	snprintf(genpath, 255, "%s/%"d64"/status", ac->system_procfs, (int64_t)getpid());
	FILE *fd = fopen(genpath, "r");
	if (!fd)
		return;

	char tmp[LINUXFS_LINE_LENGTH];
	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		size_t tmp_len = strlen(tmp)-1;
		tmp[tmp_len] = 0;
		int64_t i = strcspn(tmp, " \t");

		int swap = strspn(tmp+i, " \t")+i;
		for (; tmp[i] && (tmp[i]!=' ' || tmp[i]!='\t'); i++);
		strlcpy(val, tmp+swap, i-swap+1);

		ival = atoll(val);

		if ( strstr(tmp+swap, "kB") )
			ival *= 1024;

		if ( !strncmp(tmp, "VmRSS", 5) )
		{
			metric_add_labels("alligator_memory_usage", &ival, DATATYPE_INT, ac->system_carg, "type", "rss");
		}
		if ( !strncmp(tmp, "VmSize", 6) )
		{
			metric_add_labels("alligator_memory_usage", &ival, DATATYPE_INT, ac->system_carg, "type", "vsz");
		}
	}
	fclose(fd);
}

void get_packages_info()
{
	get_rpm_info();
	dpkg_crawl("/var/lib/dpkg/available");
}

void clear_counts_for(void* arg)
{
	match_string* ms = arg;

	ms->count = 0;
}

void clear_counts_process()
{
	if (!ac->process_match)
		return;
	alligator_ht *hash = ac->process_match->hash;
	alligator_ht_foreach(hash, clear_counts_for);

	regex_list *node = ac->process_match->head;
	while (node)
	{
		node->count = 0;
		node = node->next;
	}
}

void fill_counts_for(void* arg)
{
	match_string* ms = arg;

	metric_add_labels("process_match", &ms->count, DATATYPE_UINT, ac->system_carg, "name", ms->s);
}

void fill_counts_process()
{
	if (!ac->process_match)
		return;
	alligator_ht *hash = ac->process_match->hash;
	alligator_ht_foreach(hash, fill_counts_for);

	regex_list *node = ac->process_match->head;
	while (node)
	{
		metric_add_labels("process_match", &node->count, DATATYPE_UINT, ac->system_carg, "name", node->name);
		node = node->next;
	}
}

void get_conntrack_info()
{
	char path[255];

	snprintf(path, 255, "%s/sys/net/netfilter/nf_conntrack_max", ac->system_procfs);
	int64_t conntrack_max = getkvfile(path);

	snprintf(path, 255, "%s/sys/net/netfilter/nf_conntrack_count", ac->system_procfs);
	int64_t conntrack_count = getkvfile(path);

	double conntrack_usage = conntrack_count*100.0/conntrack_max;

	metric_add_auto("conntrack_max", &conntrack_max, DATATYPE_INT, ac->system_carg);
	metric_add_auto("conntrack_count", &conntrack_count, DATATYPE_INT, ac->system_carg);
	metric_add_auto("conntrack_usage", &conntrack_usage, DATATYPE_DOUBLE, ac->system_carg);
}

void get_cpu_avg()
{
	double result = ac->system_cpuavg_sum / ac->system_cpuavg_period;
	metric_add_auto("cpu_avg", &result, DATATYPE_DOUBLE, ac->system_carg);
}

void baseboard_info()
{
	uint64_t val = 1;
	string *board_vendor = get_file_content("/sys/devices/virtual/dmi/id/board_vendor");
	if (board_vendor)
	{
		board_vendor->s[strcspn(board_vendor->s, "\n\r")] = 0;
		metric_add_labels("baseboard_vendor", &val, DATATYPE_UINT, ac->system_carg, "vendor", board_vendor->s);
		string_free(board_vendor);
	}

	string *product_name = get_file_content("/sys/devices/virtual/dmi/id/product_name");
	if (product_name)
	{
		product_name->s[strcspn(product_name->s, "\n\r")] = 0;
		metric_add_labels("baseboard_product_name", &val, DATATYPE_UINT, ac->system_carg, "name", product_name->s);
		string_free(product_name);
	}

	string *asset_tag = get_file_content("/sys/devices/virtual/dmi/id/board_asset_tag");
	if (asset_tag)
	{
		asset_tag->s[strcspn(asset_tag->s, "\n\r")] = 0;
		metric_add_labels("baseboard_asset_tag", &val, DATATYPE_UINT, ac->system_carg, "name", asset_tag->s);
		string_free(asset_tag);
	}

	string *board_version = get_file_content("/sys/devices/virtual/dmi/id/board_version");
	if (board_version)
	{
		board_version->s[strcspn(board_version->s, "\n\r")] = 0;
		metric_add_labels("baseboard_version", &val, DATATYPE_UINT, ac->system_carg, "version", board_version->s);
		string_free(board_version);
	}

	string *board_serial = get_file_content("/sys/devices/virtual/dmi/id/board_serial");
	if (board_serial)
	{
		board_serial->s[strcspn(board_serial->s, "\n\r")] = 0;
		metric_add_labels("baseboard_serial", &val, DATATYPE_UINT, ac->system_carg, "serial", board_serial->s);
		string_free(board_serial);
	}

	string *bios_vendor = get_file_content("/sys/devices/virtual/dmi/id/bios_vendor");
	if (bios_vendor)
	{
		bios_vendor->s[strcspn(bios_vendor->s, "\n\r")] = 0;
		metric_add_labels("bios_vendor", &val, DATATYPE_UINT, ac->system_carg, "vendor", bios_vendor->s);
		string_free(bios_vendor);
	}

	string *bios_version = get_file_content("/sys/devices/virtual/dmi/id/bios_version");
	if (bios_version)
	{
		bios_version->s[strcspn(bios_version->s, "\n\r")] = 0;
		metric_add_labels("bios_version", &val, DATATYPE_UINT, ac->system_carg, "version", bios_version->s);
		string_free(bios_version);
	}

	struct dirent *entry;
	DIR *dp;

	dp = opendir("/sys/class/block/");
	if (!dp)
		return;

	uint64_t disks_num = 0;
	while((entry = readdir(dp)))
	{
		if (entry->d_name[0] == '.')
			continue;
		if (strpbrk(entry->d_name, "0123456789"))
			continue;

		char blockpath[255];
		snprintf(blockpath, 254, "/sys/class/block/%s/device/model", entry->d_name);
		string *disk_model = get_file_content(blockpath);
		if (disk_model)
		{
			++disks_num;
			disk_model->s[strcspn(disk_model->s, "\n\r")] = 0;
			metric_add_labels2("disk_model", &val, DATATYPE_UINT, ac->system_carg, "model", disk_model->s, "disk", entry->d_name);
			string_free(disk_model);
		}
	}
	metric_add_auto("disk_num", &disks_num, DATATYPE_UINT, ac->system_carg);
	closedir(dp);
}

void get_activate_status_services(char *service_name)
{
	char svcdir[1000];
	char pathsystemd[1000];
	snprintf(svcdir, 999, "%s/systemd/system/", ac->system_etcdir);

	struct dirent *entry;
	DIR *dp;

	dp = opendir(svcdir);
	if (!dp)
	{
		return;
	}

	uint64_t enabled = 0;
	while((entry = readdir(dp)))
	{
		if (entry->d_name[0] == '.')
			continue;
		if (!strstr(entry->d_name, ".target.wants"))
			continue;

		snprintf(pathsystemd, 999, "%s/%s/%s", svcdir, entry->d_name, service_name);
		FILE *fd = fopen(pathsystemd, "r");
		if (fd)
		{
			enabled = 1;
			fclose(fd);
			break;
		}
	}
	closedir(dp);
	metric_add_labels("service_enabled", &enabled, DATATYPE_UINT, ac->system_carg, "service", service_name);
}

void get_service_tasks_status(char *servicename, char *fname, char *type)
{
	char systemdpath[1000];
	snprintf(systemdpath, 999, "%s/fs/cgroup/systemd/system.slice/%s/%s", ac->system_sysfs, servicename, fname);
	uint64_t cnt;

	FILE *fd = fopen(systemdpath, "r");
	if (!fd)
		return;

	char buf[100];
	for (cnt = 1; fgets(buf, 100, fd); ++cnt);
	fclose(fd);

	metric_add_labels2("service_tasks_count", &cnt, DATATYPE_UINT, ac->system_carg, "service", servicename, "type", type);
}

void service_running_status(char *name)
{
	int match = match_mapper(ac->services_match, name, strlen(name), name);
	//printf("name %s match %d\n", name, match);
	if (!match)
		return;

	uint64_t val;
	char pathsystemd[1000];
	snprintf(pathsystemd, 999, "%s/fs/cgroup/systemd/system.slice/%s", ac->system_sysfs, name);
	FILE *fd = fopen(pathsystemd, "r");
	if (!fd)
	{
		val = 0;
	}
	else
	{
		val = 1;
		fclose(fd);
	}
	metric_add_labels("service_running", &val, DATATYPE_UINT, ac->system_carg, "service", name);

	get_activate_status_services(name);
	get_service_tasks_status(name, "cgroup.procs", "processes");
	get_service_tasks_status(name, "tasks", "threads");

	if (match == 1)
	{
		metric_add_labels("service_match", &val, DATATYPE_UINT, ac->system_carg, "service", name);
		char cgrouppath[1024];
		snprintf(cgrouppath, 1023, "%s/fs/cgroup/systemd/system.slice/%s/cgroup.procs", ac->system_sysfs, name);
		cgroup_procs_scrape(cgrouppath);
	}
}

void get_services()
{
	char svcdir[1000];
	char svcdir_r[1000];
	struct dirent *entry;
	struct dirent *entry_r;
	DIR *dp;
	DIR *dp_r;

	snprintf(svcdir, 999, "%s/lib/systemd/system/", ac->system_usrdir);
	dp = opendir(svcdir);
	if (!dp)
	{
		return;
	}

	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;

		service_running_status(entry->d_name);
	}
	closedir(dp);

	snprintf(svcdir, 999, "%s/systemd/", ac->system_rundir);
	dp = opendir(svcdir);
	if (!dp)
	{
		return;
	}

	while((entry = readdir(dp)))
	{
		if (strncmp(entry->d_name, "generator", 9))
			continue;

		snprintf(svcdir_r, 999, "%s/systemd/%s", ac->system_rundir, entry->d_name);
		dp_r = opendir(svcdir_r);
		if (!dp_r)
		{
			continue;
		}

		while((entry_r = readdir(dp_r)))
		{
			if ( entry_r->d_name[0] == '.' )
				continue;

			service_running_status(entry_r->d_name);
		}
		closedir(dp_r);
	}
	closedir(dp);
}


void stat_userprocess_cb(uv_fs_t *req) {
	uv_stat_t st = req->statbuf;

	userprocess_node *uupn = alligator_ht_search(ac->system_userprocess, userprocess_compare, &st.st_uid, st.st_uid);
	userprocess_node *gupn = alligator_ht_search(ac->system_groupprocess, userprocess_compare, &st.st_gid, st.st_gid);
	char *pid = req->data;

	if (ac->log_level > 2)
		printf("%s: st_uid is %"u64", st_gid is %"u64": %p/%p\n", pid, st.st_uid, st.st_gid, uupn, gupn);

	process_states *states = calloc(1, sizeof(*states));
	int64_t allfilesnum = 0;
	if (uupn || gupn)
		get_pid_info(pid, &allfilesnum, 0, states, 0);

	free(pid);
	free(states);

	uv_fs_req_cleanup(req);
	free(req);
}

void get_userprocess_stats()
{
	if (!ac->system_userprocess && !ac->system_groupprocess)
		return;

	if (!alligator_ht_count(ac->system_userprocess) && !alligator_ht_count(ac->system_groupprocess))
		return;

	DIR *dp;
	struct dirent *entry;

	dp = opendir(ac->system_procfs);
	if (!dp)
	{
		return;
	}

	char dir[FILENAME_MAX];
	while((entry = readdir(dp)))
	{
		if (!isdigit(entry->d_name[0]))
			continue;

		snprintf(dir, FILENAME_MAX-1, "%s/%s", ac->system_procfs, entry->d_name);
		uv_fs_t* req_stat = malloc(sizeof(*req_stat));
		req_stat->data = strdup(entry->d_name);
		uv_fs_stat(uv_default_loop(), req_stat, dir, stat_userprocess_cb);
	}
	closedir(dp);
}

void utmp_parse(struct utmp *log) {
	if (log && log->ut_type == USER_PROCESS)
	{
		//printf("{ ut_type: %i, ut_pid: %i, ut_line: %s, ut_user: %s, ut_host:   %s, ut_exit: { e_termination: %i, e_exit: %i }, ut_session: %i, timeval: { tv_sec: %i, tv_usec: %i }, ut_addr_v6: %i }\n\n", log->ut_type, log->ut_pid, log->ut_line, log->ut_user, log->ut_host, log->ut_exit.e_termination, log->ut_exit.e_exit, log->ut_session, log->ut_tv.tv_sec, log->ut_tv.tv_usec, log->ut_addr_v6);
		int64_t time = log->ut_tv.tv_sec;
		if (!strncmp(log->ut_line, "tty", 3))
		{
			//printf("user: %s, host %s, logged %u, line %s, type=\"terminal\"\n", log->ut_user, log->ut_host, log->ut_tv.tv_sec, log->ut_line);
			metric_add_labels4("utmp_logged_in_timestamp", &time, DATATYPE_INT, ac->system_carg, "user", log->ut_user, "host", log->ut_host, "type", "terminal", "terminal", log->ut_line);
		}
		if (!strncmp(log->ut_line, "pts", 3))
		{
			//printf("user: %s, host %s, logged %u, line %s, type=\"pseudo-terminal\"\n", log->ut_user, log->ut_host, log->ut_tv.tv_sec, log->ut_line);
			metric_add_labels4("utmp_logged_in_timestamp", &time, DATATYPE_INT, ac->system_carg, "user", log->ut_user, "host", log->ut_host, "type", "pseudo-terminal", "terminal", log->ut_line);
		}
	}
}

void get_utmp_info()
{
	//int logsize = 10;
	FILE *file;
	//struct utmp log[logsize];
	struct utmp log;
	int i = 0;

	uint64_t btmp_size = get_file_size("/var/log/btmp");
	metric_add_auto("btmp_file_size", &btmp_size, DATATYPE_UINT, ac->system_carg);

	file = fopen("/var/run/utmp", "rb");

	if (!file) {
		return;
	}

	//fread(&log, sizeof(struct utmp), logsize, file);
	size_t rc = 1;
	for(i = 0; rc; i++) {
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
		if (!platform)
		{
			char edacdir[255];

			snprintf(edacdir, 255, "%s/devices/system/edac/mc/mc0/", ac->system_sysfs);
			get_memory_errors(edacdir);

			snprintf(edacdir, 255, "%s/devices/system/edac/mc/mc1/", ac->system_sysfs);
			get_memory_errors(edacdir);
			get_thermal();
			get_buddyinfo();
			baseboard_info();
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

	if (ac->system_network)
	{
		get_network_statistics();
		char dirname[255];

		snprintf(dirname, 255, "%s/net/netstat", ac->system_procfs);
		get_netstat_statistics(dirname);

		snprintf(dirname, 255, "%s/net/snmp", ac->system_procfs);
		get_netstat_statistics(dirname);

		snprintf(dirname, 255, "%s/net/tcp", ac->system_procfs);
		get_net_tcpudp(dirname, "tcp", A_TCP_STATE);

		snprintf(dirname, 255, "%s/net/tcp6", ac->system_procfs);
		get_net_tcpudp(dirname, "tcp6", A_TCP_STATE);

		snprintf(dirname, 255, "%s/net/udp", ac->system_procfs);
		get_net_tcpudp(dirname, "udp", A_UDP_STATE);

		snprintf(dirname, 255, "%s/net/udp6", ac->system_procfs);
		get_net_tcpudp(dirname, "udp6", A_UDP_STATE);

		interface_stats();
	}
	if (ac->system_disk)
	{
		get_disk_io_stat();
		get_disk();
		if (platform == -1)
			platform = get_platform(0);
		if (!platform)
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
		get_iptables_info("filter", ac->system_carg);
		get_iptables_info("nat", ac->system_carg);
		get_iptables6_info("filter", ac->system_carg);
		get_iptables6_info("nat", ac->system_carg);
	}
	if (ac->system_cpuavg)
		get_cpu_avg();

	if (ac->system_cadvisor)
		cadvisor_metrics();

	if (ac->system_services)
		get_services();

	get_pidfile_stats();
	get_userprocess_stats();
}

void system_fast_scrape()
{
	if (ac->system_base)
	{
		get_cpu(get_platform(0));
	}
}

void system_slow_scrape()
{
	int8_t platform = -1;
	if (ac->system_packages)
	{
		get_packages_info();
	}

	if (ac->system_smart)
	{
		if (platform == -1)
			platform = get_platform(0);
		if (!platform)
			get_smart_info();
	}

}

#endif
