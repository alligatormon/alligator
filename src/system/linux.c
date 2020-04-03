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
#include "common/rpm.h"
#define LINUXFS_LINE_LENGTH 300
#define d64 PRId64
#define PLATFORM_BAREMETAL 0
#define PLATFORM_LXC 1
#define PLATFORM_OPENVZ 2
#define PLATFORM_NSPAWN 3
#define PLATFORM_DOCKER 4
#define LINUX_MEMORY 1
#define LINUX_CPU 2

typedef struct process_states
{
	uint64_t running;
	uint64_t sleeping;
	uint64_t uninterruptible;
	uint64_t zombie;
	uint64_t stopped;
} process_states;

typedef struct process_fdescriptors
{
	uint32_t fd;
	char *procname;

	tommy_node node;
} process_fdescriptors;

void print_mount(const struct mntent *fs)
{
	extern aconf *ac;
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
			}

			close(f_d);
		}
	}
}

void get_disk()
{
	extern aconf *ac;
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

void get_cpu(int8_t platform)
{
	extern aconf *ac;
	if (ac->log_level > 2)
		puts("fast scrape metrics: base: cpu");

	int64_t effective_cores;
	int64_t num_cpus = sysconf( _SC_NPROCESSORS_ONLN );
	int64_t num_cpus_cgroup = (getkvfile("/sys/fs/cgroup/cpu/cpu.cfs_quota_us")/100000);
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
		ac->scs->cores = calloc(1, sizeof(system_cpu_cores_stats)*effective_cores);


	metric_add_auto("cores_num_hw", &num_cpus, DATATYPE_INT, ac->system_carg);
	metric_add_auto("cores_num_cgroup", &num_cpus_cgroup, DATATYPE_INT, ac->system_carg);
	metric_add_auto("cores_num", &effective_cores, DATATYPE_INT, ac->system_carg);

	char temp[LINUXFS_LINE_LENGTH];
	uint64_t core_num = 0;
	system_cpu_cores_stats *sccs;

	if (!platform)
	{
		FILE *fd = fopen("/proc/stat", "r");
		if ( !fd )
			return;

		while ( fgets(temp, LINUXFS_LINE_LENGTH, fd) )
		{
			if ( !strncmp(temp, "cpu", 3) )
			{
				dividecpu = 1;
				int64_t t1, t2, t3, t4, t5;
				char cpuname[6];

				sscanf(temp, "%s %"d64" %"d64" %"d64" %"d64" %"d64"", cpuname, &t1, &t2, &t3, &t4, &t5);
				core_num = atoll(cpuname+3);
				if (!strcmp(cpuname, "cpu"))
					sccs = &ac->scs->hw;
				else
					sccs = &ac->scs->cores[core_num];

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

				double usage = ((t1 + t3)*100.0/t12345)/dividecpu;
				if (usage<0)
					usage = 0;
				double user = ((t1)*100.0/t12345)/dividecpu;
				if (user<0)
					user = 0;
				double nice = ((t2)*100.0/t12345)/dividecpu;
				if (nice<0)
					nice = 0;
				double system = ((t3)*100.0/t12345)/dividecpu;
				if (system<0)
					system = 0;
				double idle = ((t4)*100.0/t12345)/dividecpu;
				if (idle<0)
					idle = 0;
				double iowait = ((t5)*100.0/t12345)/dividecpu;
				if (iowait<0)
					iowait = 0;

				if (!strcmp(cpuname, "cpu"))
				{
					//printf("CPU: usage %lf, user %lf, system %lf, nice %lf, idle %lf, iowait %lf\n", usage, user, system, nice, idle, iowait);
					metric_add_labels("cpu_usage", &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "total");
					metric_add_labels("cpu_usage", &user, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
					metric_add_labels("cpu_usage", &system, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
					metric_add_labels("cpu_usage", &nice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice");
					metric_add_labels("cpu_usage", &idle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle");
					metric_add_labels("cpu_usage", &iowait, DATATYPE_DOUBLE, ac->system_carg, "type", "iowait");
				}
				else
				{
					//printf("core: '%s' usage %lf, user %lf, system %lf, nice %lf, idle %lf, iowait %lf\n", cpuname, usage, user, system, nice, idle, iowait);
					metric_add_labels2("cpu_usage_core", &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "total", "cpu", cpuname);
					metric_add_labels2("cpu_usage_core", &user, DATATYPE_DOUBLE, ac->system_carg, "type", "user", "cpu", cpuname);
					metric_add_labels2("cpu_usage_core", &system, DATATYPE_DOUBLE, ac->system_carg, "type", "system", "cpu", cpuname);
					metric_add_labels2("cpu_usage_core", &nice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice", "cpu", cpuname);
					metric_add_labels2("cpu_usage_core", &idle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle", "cpu", cpuname);
					metric_add_labels2("cpu_usage_core", &iowait, DATATYPE_DOUBLE, ac->system_carg, "type", "iowait", "cpu", cpuname);
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
	}
	else
	{
		sccs = &ac->scs->cgroup;
		int64_t cfs_period = getkvfile("/sys/fs/cgroup/cpu/cpu.cfs_period_us");
		int64_t cfs_quota = getkvfile("/sys/fs/cgroup/cpu/cpu.cfs_quota_us");
		if ( cfs_quota < 0 )
		{
			cfs_quota = cfs_period*dividecpu;
		}

		FILE *fd = fopen("/sys/fs/cgroup/cpuacct/cpuacct.stat", "r");
		if (!fd)
			return;

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

		double cgroup_system_usage = cgroup_system_ticks*1.0/dividecpu;
		double cgroup_user_usage = cgroup_user_ticks*1.0/dividecpu;
		//printf("cgroup_system_ticks %lld, cgroup_user_ticks %lld dividecpu %lf\n", cgroup_system_ticks, cgroup_user_ticks, dividecpu);
		double cgroup_total_usage = cgroup_system_usage + cgroup_user_usage;
		if (!cgroup_system_usage && !cgroup_user_usage)
			cgroup_total_usage = 0;
		else if (!cgroup_total_usage)
			cgroup_total_usage = getkvfile("/sys/fs/cgroup/cpuacct/cpuacct.usage")*cfs_period/1000000000/cfs_quota*100.0;

		//printf("CPU: usage %lf, user %lf, system %lf\n", cgroup_total_usage, cgroup_user_usage, cgroup_system_usage);
		metric_add_labels("cpu_usage_cgroup", &cgroup_system_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
		metric_add_labels("cpu_usage_cgroup", &cgroup_user_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
		metric_add_labels("cpu_usage_cgroup", &cgroup_total_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "total");
		metric_add_labels("cpu_usage", &cgroup_total_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "total");
		metric_add_labels("cpu_usage", &cgroup_system_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
		metric_add_labels("cpu_usage", &cgroup_user_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
	}
}

void get_process_extra_info(char *file, char *name, char *pid)
{
	extern aconf *ac;
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
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "stack_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "VmData", 6) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "data_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "VmLck", 5) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "lock_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "RssAnon", 7) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "anon_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "RssFile", 7) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "file_bytes", "name", name, "pid", pid);
		else if ( !strncmp(key, "VmRSS", 5) )
			metric_add_labels3("process_memory", &ival, DATATYPE_INT, ac->system_carg, "type", "rss", "name", name, "pid", pid);
		else if ( !strncmp(key, "VmSize", 6) )
			metric_add_labels3("process_memory", &ival, DATATYPE_INT, ac->system_carg, "type", "vsz", "name", name, "pid", pid);
		else if ( !strncmp(key, "RssShmem", 8) )
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "shmem_bytes", "name", name, "pid", pid);
		else	continue;
	}

	fclose(fd);
}


void get_proc_info(char *szFileName, char *exName, char *pid_number, int8_t lightweight, process_states *states, int8_t match)
{
	extern aconf *ac;
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

	int cnt = 10;
	while (cnt--)
	{
		if (cnt == 8)
		{
			state = t[cursor];
			if (state == 'R')
			{
				int64_t val = 1;
				metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
				++states->running;
			}
			else if (state == 'S')
			{
				int64_t val = 1;
				metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
				++states->sleeping;
			}
			else if (state == 'D')
			{
				int64_t val = 1;
				metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
				++states->uninterruptible;
			}
			else if (state == 'Z')
			{
				int64_t val = 1;
				metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
				++states->zombie;
			}
			else if (state == 'T')
			{
				int64_t val = 1;
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

	//struct stat st;
	//stat(szFileName, &st);

	//double sys_cpu_usage = 100 * (stotal_time / Hertz);
	//double user_cpu_usage = 100 * (utotal_time / Hertz);
	//double total_cpu_usage = 100 * (total_time / Hertz);

	//metric_add_labels3("process_cpu", &sys_cpu_usage, DATATYPE_DOUBLE, "name", exName, "pid", pid_number, "type", "system");
	//metric_add_labels3("process_cpu", &user_cpu_usage, DATATYPE_DOUBLE, "name", exName, "pid", pid_number, "type", "user");
	//metric_add_labels3("process_cpu", &total_cpu_usage, DATATYPE_DOUBLE, "name", exName, "pid", pid_number, "type", "total");
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

	//tommy_hashdyn_remove_existing(funcarg, &(fdescriptors->node));
	free(fdescriptors);
}

void get_proc_socket_number(char *path, char *procname)
{
	extern aconf *ac;
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
	for (; !isdigit(*cur); ++cur);
	uint32_t fdesc = strtoull(cur, NULL, 10);
	//printf("%s/%s: %"PRIu32"\n", buf, cur, fdesc);

	tommy_hashdyn *hash = ac->fdesc;
	if (!hash)
	{
		hash = ac->fdesc = malloc(sizeof(*hash));
		tommy_hashdyn_init(hash);
	}

	uint32_t fdesc_hash = tommy_inthash_u32(fdesc);
	process_fdescriptors *fdescriptors = tommy_hashdyn_search(hash, process_fdescriptors_compare, &fdesc, fdesc_hash);
	if (!fdescriptors)
	{
		fdescriptors = malloc(sizeof(*fdescriptors));
		fdescriptors->fd = fdesc;
		fdescriptors->procname = strdup(procname);
		//printf("DEB: alloc %p: %llu with name %s\n", fdescriptors, fdescriptors->fd, fdescriptors->procname);
		tommy_hashdyn_insert(hash, &(fdescriptors->node), fdescriptors, fdesc_hash);
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
	extern aconf *ac;
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
	extern aconf *ac;
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

void find_pid(int8_t lightweight)
{
	extern aconf *ac;
	if (ac->log_level > 2)
		puts("system scrape metrics: processes");

	struct dirent *entry;
	DIR *dp;

	dp = opendir("/proc");
	if (!dp)
	{
		//perror("opendir");
		return;
	}

	process_states *states = calloc(1, sizeof(*states));
	int64_t allfilesnum = 0;

	char dir[FILENAME_MAX];
	while((entry = readdir(dp)))
	{
		if ( !isdigit(entry->d_name[0]) )
			continue;

		// get comm name
		snprintf(dir, FILENAME_MAX, "/proc/%s/comm", entry->d_name);
		FILE *fd = fopen(dir, "r");
		if (!fd)
			continue;

		char procname[_POSIX_PATH_MAX];
		if(!fgets(procname, _POSIX_PATH_MAX, fd))
		{
			fclose(fd);
			continue;
		}
		size_t procname_size = strlen(procname)-1;
		procname[procname_size] = '\0';
		fclose(fd);

		// get cmdline
		snprintf(dir, FILENAME_MAX, "/proc/%s/cmdline", entry->d_name);
		fd = fopen(dir, "r");
		if (!fd)
			continue;

		char cmdline[_POSIX_PATH_MAX];
		int64_t rc;
		if(!(rc=fread(cmdline, 1, _POSIX_PATH_MAX, fd)))
		{
			fclose(fd);
			continue;
		}
		for(int64_t iter = 0; iter < rc-1; iter++)
			if (!cmdline[iter])
				cmdline[iter] = ' ';
		size_t cmdline_size = strlen(cmdline);
		fclose(fd);

		extern aconf *ac;
		int8_t match = 1;
		if (!match_mapper(ac->process_match, procname, procname_size, procname))
			if (!match_mapper(ac->process_match, cmdline, cmdline_size, procname))
				match = 0;

		snprintf(dir, FILENAME_MAX, "/proc/%s/stat", entry->d_name);
		get_proc_info(dir, procname, entry->d_name, lightweight, states, match);

		snprintf(dir, FILENAME_MAX, "/proc/%s/fd/", entry->d_name);
		int64_t filesnum = get_fd_info_process(dir, procname);
		if (match && filesnum && !lightweight)
			metric_add_labels3("process_stats", &filesnum, DATATYPE_INT, ac->system_carg, "name", procname, "type", "open_files", "pid", entry->d_name);
		allfilesnum += filesnum;

		if (!match)
			continue;

		if (lightweight)
			continue;

		snprintf(dir, FILENAME_MAX, "/proc/%s/status", entry->d_name);
		get_process_extra_info(dir, procname, entry->d_name);

		snprintf(dir, FILENAME_MAX, "/proc/%s/io", entry->d_name);
		get_process_io_stat(dir, procname, entry->d_name);
	}

	metric_add_labels("process_states", &states->running, DATATYPE_UINT, ac->system_carg, "state", "running");
	metric_add_labels("process_states", &states->sleeping, DATATYPE_UINT, ac->system_carg, "state", "sleeping");
	metric_add_labels("process_states", &states->uninterruptible, DATATYPE_UINT, ac->system_carg, "state", "uninterruptible");
	metric_add_labels("process_states", &states->zombie, DATATYPE_UINT, ac->system_carg, "state", "zombie");
	metric_add_labels("process_states", &states->stopped, DATATYPE_UINT, ac->system_carg, "state", "stopped");
	metric_add_auto("open_files_process", &allfilesnum, DATATYPE_INT, ac->system_carg);
	free(states);

	closedir(dp);
}

void get_mem(int8_t platform)
{
	extern aconf *ac;
	if (ac->log_level > 2)
		puts("system scrape metrics: base: mem");

	FILE *fd = fopen("/proc/meminfo", "r");
	if (!fd)
		return;

	char tmp[LINUXFS_LINE_LENGTH];
	char key[LINUXFS_LINE_LENGTH];
	char key_map[LINUXFS_LINE_LENGTH];
	char val[LINUXFS_LINE_LENGTH];
	int64_t ival = 1;
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

	fd = fopen("/proc/vmstat", "r");
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
			metric_add_auto("oom_kill", &ival, DATATYPE_INT, ac->system_carg);
		else if (!platform && !strcmp(key, "pswpin"))
			metric_add_labels("memory_stat", &ival, DATATYPE_INT, ac->system_carg, "type", "pswpin");
		else if (!platform && !strcmp(key, "pswpout"))
			metric_add_labels("memory_stat", &ival, DATATYPE_INT, ac->system_carg, "type", "pswpout");
		else if (!platform && !strncmp(key, "numa_", 5))
			metric_add_labels("numa_stat", &ival, DATATYPE_INT, ac->system_carg, "type", key+5);
	}
	fclose(fd);

	// scrape cgroup
	fd = fopen("/sys/fs/cgroup/memory/memory.stat", "r");
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

	inactive = inactive_file+inactive_anon;
	active = active_file+active_anon;
	metric_add_labels("memory_usage", &active, DATATYPE_INT, ac->system_carg, "type", "active");
	metric_add_labels("memory_usage", &inactive, DATATYPE_INT, ac->system_carg, "type", "inactive");
	
	fclose(fd);
}

void cgroup_mem()
{
	extern aconf *ac;
	FILE *fd = fopen("/sys/fs/cgroup/memory/memory.stat", "r");
	if (fd)
	{
		char tmp[LINUXFS_LINE_LENGTH];
		char key[LINUXFS_LINE_LENGTH];
		char key_map[LINUXFS_LINE_LENGTH];
		char val[LINUXFS_LINE_LENGTH];
		int64_t ival = 1;
		while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
		{
			tmp[strlen(tmp)-1] = 0;
			int i;
			for (i=0; tmp[i]!=' '; i++);
			strlcpy(key, tmp, i+1);

			if	(!strcmp(key, "total_cache"))
				strlcpy(key_map, "cache", 6);
			else if (!strcmp(key, "total_rss"))
				strlcpy(key_map, "rss", 4);
			else if (!strcmp(key, "hierarchical_memory_limit"))
				strlcpy(key_map, "limit", 6);
			else	continue;

			for (; tmp[i]==' '; i++);
			int swap = i;
			for (; tmp[i] && tmp[i]!=' '; i++);
			strlcpy(val, tmp+swap, i-swap+1);

			ival = atoll(val);
			metric_add_labels("memory_cgroup_usage", &ival, DATATYPE_INT, ac->system_carg, "type", key_map);
		}
	}
	fclose(fd);

	int64_t mval;
	mval = getkvfile("/sys/fs/cgroup/memory/memory.memsw.failcnt");
	metric_add_labels("memory_cgroup_fails", &mval, DATATYPE_INT, ac->system_carg, "type", "memsw");

	mval = getkvfile("/sys/fs/cgroup/memory/memory.failcnt");
	metric_add_labels("memory_cgroup_fails", &mval, DATATYPE_INT, ac->system_carg, "type", "mem");

	mval = getkvfile("/sys/fs/cgroup/memory/memory.kmem.failcnt");
	metric_add_labels("memory_cgroup_fails", &mval, DATATYPE_INT, ac->system_carg, "type", "kmem");

	mval = getkvfile("/sys/fs/cgroup/memory/memory.kmem.tcp.failcnt");
	metric_add_labels("memory_cgroup_fails", &mval, DATATYPE_INT, ac->system_carg, "type", "kmem_tcp");
}

#define TCPUDP_NET_LENREAD 10000000
void get_net_tcpudp(char *file, char *name)
{
	extern aconf *ac;
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

	uint64_t established = 0;
	uint64_t syn_sent = 0;
	uint64_t syn_recv = 0;
	uint64_t fin_wait1 = 0;
	uint64_t fin_wait2 = 0;
	uint64_t time_wait = 0;
	uint64_t close_v = 0;
	uint64_t close_wait = 0;
	uint64_t last_ack = 0;
	uint64_t listen = 0;
	uint64_t closing = 0;
	char *start, *end;

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

			pEnd += strcspn(pEnd, " \t");
			pEnd += strspn(pEnd, " \t");
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
			if (state == 10)
			{
				uint64_t val = 1;

				process_fdescriptors *fdescriptors = NULL;
				if (ac->fdesc)
					fdescriptors = tommy_hashdyn_search(ac->fdesc, process_fdescriptors_compare, &fdesc, tommy_inthash_u32(fdesc));

				if (fdescriptors)
					metric_add_labels7("socket_stat", &val, DATATYPE_UINT, ac->system_carg, "src", str1, "src_port", srcp, "dst", str2, "dst_port", destp, "state", "listen", "proto", name, "process", fdescriptors->procname);
				else
					metric_add_labels6("socket_stat", &val, DATATYPE_UINT, ac->system_carg, "src", str1, "src_port", srcp, "dst", str2, "dst_port", destp, "state", "listen", "proto", name);
				++listen;
			}
			else if (state == 6)
				++time_wait;
			else if (state == 1)
				++established;
			else if (state == 2)
				++syn_sent;
			else if (state == 3)
				++syn_recv;
			else if (state == 4)
				++fin_wait1;
			else if (state == 5)
				++fin_wait2;
			else if (state == 7)
				++close_v;
			else if (state == 8)
				++close_wait;
			else if (state == 9)
				++last_ack;
			else if (state == 11)
				++closing;

			start = end+1;
		}
	}
	fclose(fd);
	if (established>0)
		metric_add_labels2("socket_counters", &established, DATATYPE_UINT, ac->system_carg, "state", "ESTABLISHED", "proto", name);
	if (syn_sent>0)
		metric_add_labels2("socket_counters", &syn_sent, DATATYPE_UINT, ac->system_carg, "state", "SYN_SENT", "proto", name);
	if (syn_recv>0)
		metric_add_labels2("socket_counters", &syn_recv, DATATYPE_UINT, ac->system_carg, "state", "SYN_RECV", "proto", name);
	if (fin_wait1>0)
		metric_add_labels2("socket_counters", &fin_wait1, DATATYPE_UINT, ac->system_carg, "state", "FIN_WAIT1", "proto", name);
	if (fin_wait2>0)
		metric_add_labels2("socket_counters", &fin_wait2, DATATYPE_UINT, ac->system_carg, "state", "FIN_WAIT2", "proto", name);
	if (time_wait>0)
		metric_add_labels2("socket_counters", &time_wait, DATATYPE_UINT, ac->system_carg, "state", "TIME_WAIT", "proto", name);
	if (close_v>0)
		metric_add_labels2("socket_counters", &close_v, DATATYPE_UINT, ac->system_carg, "state", "CLOSE", "proto", name);
	if (close_wait>0)
		metric_add_labels2("socket_counters", &close_wait, DATATYPE_UINT, ac->system_carg, "state", "CLOSE_WAIT", "proto", name);
	if (last_ack>0)
		metric_add_labels2("socket_counters", &last_ack, DATATYPE_UINT, ac->system_carg, "state", "LAST_ACK", "proto", name);
	if (listen>0)
		metric_add_labels2("socket_counters", &listen, DATATYPE_UINT, ac->system_carg, "state", "LISTEN", "proto", name);
	if (closing>0)
		metric_add_labels2("socket_counters", &closing, DATATYPE_UINT, ac->system_carg, "state", "CLOSING", "proto", name);
	free(buf);

	r_time ts_end = setrtime();
	int64_t scrape_time = getrtime_i(ts_start, ts_end);
	if (ac->log_level > 2)
		printf("system scrape metrics: network: get_net_tcpudp time execute '%"d64"'\n", scrape_time);
}

void get_netstat_statistics(char *ns_file)
{
	extern aconf *ac;
	if (ac->log_level > 2)
		printf("system scrape metrics: network: netstat_statistics '%s'\n", ns_file);

	FILE *fp = fopen(ns_file, "r");
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
	extern aconf *ac;
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

	FILE *fp = fopen("/proc/net/dev", "r");
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
	}

	fclose(fp);
}

void get_nofile_stat()
{
	extern aconf *ac;
	if (ac->log_level > 2)
		puts("system scrape metrics: base: nofile_stat");

	FILE *fd = fopen("/proc/sys/fs/file-nr", "r");
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
	extern aconf *ac;
	if (ac->log_level > 2)
		puts("system scrape metrics: disk: io_stat");

	FILE *fd = fopen("/proc/diskstats", "r");
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
		snprintf(bldevname, UCHAR_MAX, "/sys/block/%s/queue/hw_sector_size", devname);
		int64_t sectorsize = getkvfile(bldevname);

		int64_t read_bytes = stat[5] * sectorsize;
		int64_t read_timing = stat[6];
		int64_t write_bytes = stat[9] * sectorsize;
		int64_t write_timing = stat[10];
		int64_t io_w = stat[3];
		int64_t io_r = stat[7];
		int64_t disk_busy = stat[12]/1000;
		metric_add_labels2("disk_io", &io_r, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "transfers_read");
		metric_add_labels2("disk_io", &io_w, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "transfers_write");
		metric_add_labels2("disk_io", &read_timing, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "read_timing");
		metric_add_labels2("disk_io", &write_timing, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "write_timing");
		metric_add_labels2("disk_io", &read_bytes, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "bytes_read");
		metric_add_labels2("disk_io", &write_bytes, DATATYPE_INT, ac->system_carg, "dev", devname, "type", "bytes_write");
		metric_add_labels("disk_busy", &disk_busy, DATATYPE_INT, ac->system_carg, "dev", devname);
		if (j>14)
			metric_add_labels2("disk_io", &stat[14], DATATYPE_INT, ac->system_carg, "dev", devname, "type", "transfers_discard");
	}
	fclose(fd);
}

void get_loadavg()
{
	extern aconf *ac;
	if (ac->log_level > 2)
		puts("system scrape metrics: base: loadavg");

	FILE *fd = fopen("/proc/loadavg", "r");
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
	extern aconf *ac;
	if (ac->log_level > 2)
		puts("system scrape metrics: base: uptime");
	r_time time1 = setrtime();

	time_t t = time(NULL);
	struct tm lt = {0};
	localtime_r(&t, &lt);
	time1.sec += lt.tm_gmtoff;

	uint64_t uptime = time1.sec - get_file_atime("/proc/1");
	metric_add_auto("uptime", &uptime, DATATYPE_UINT, ac->system_carg);
}

void get_mdadm()
{
	extern aconf *ac;
	if (ac->log_level > 2)
		puts("system scrape metrics: disk: mdadm");

	FILE *fd;
	fd = fopen("/proc/mdstat", "r");
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
		tmp = str2+pt;

		pt = strcspn(tmp, "[")+1;
		int64_t arr_sz = atoll(tmp+pt);
		tmp = str2+pt;

		pt = strcspn(tmp, "/")+1;
		int64_t arr_cur = atoll(tmp+pt);

		//printf("size %lld, sz %lld, cur %lld\n", size, arr_sz, arr_cur);
		metric_add_labels3("raid_configuration", &vl, DATATYPE_INT, ac->system_carg, "array", name, "status", status, "level", level);
		metric_add_labels("raid_size_bytes", &size, DATATYPE_INT, ac->system_carg, "array", name);
		metric_add_labels2("raid_devices", &arr_cur, DATATYPE_INT, ac->system_carg, "array", name, "type", "current");
		metric_add_labels2("raid_devices", &arr_sz, DATATYPE_INT, ac->system_carg, "array", name, "type", "full");

		if (arr_cur != arr_sz)
			metric_add_labels("raid_status", &nvl, DATATYPE_INT, ac->system_carg, "array", name);
		else
			metric_add_labels("raid_status", &vl, DATATYPE_INT, ac->system_carg, "array", name);
	}
}

int8_t get_platform(int8_t mode)
{
	extern aconf *ac;
	if (ac->log_level > 2)
		printf("system scrape metrics: base: platform %d\n", mode);

	int64_t vl = 1;
	FILE *env = fopen("/proc/1/cgroup", "r");
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
	env = fopen("/proc/1/environ", "r");
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

	env = fopen("/proc/user_beancounters", "r");
	if (env)
	{
		mbopenvz = 1;
	}

	env = fopen("/proc/vz/version", "r");
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
	extern aconf *ac;
	if (ac->log_level > 2)
		puts("system scrape metrics: network: interface_stats");

	struct dirent *entry;
	DIR *dp;

	dp = opendir("/sys/class/net");
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
		snprintf(operfile, 255, "/sys/class/net/%s/operstate", entry->d_name);
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
		snprintf(ifacestatistics, 255, "/sys/class/net/%s/statistics/", entry->d_name);
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

void cgroup_vm(char *dir, char *template, uint8_t stat)
{
	extern aconf *ac;
	struct dirent *entry;
	DIR *dp;
	char cntpath[1000];
	char memory_stat[1000];
	char buf[1000];
	char mname[255];
	char cntname[255];
	int64_t mval;
	uint64_t cur1;

	dp = opendir(dir);
	if (!dp)
	{
		return;
	}

	while((entry = readdir(dp)))
	{
		if (entry->d_name[0] == '.' || entry->d_type!=DT_DIR)
			continue;

		if (!strcmp(entry->d_name, "user.slice") || !strcmp(entry->d_name, "system.slice"))
			continue;

		snprintf(cntpath, 1000, template, entry->d_name);

		struct dirent *cntentry;
		DIR *cntdp;

		cntdp = opendir(cntpath);
		if (!cntdp)
		{
			return;
		}

		while((cntentry = readdir(cntdp)))
		{
			if (cntentry->d_name[0] == '.' || cntentry->d_type!=DT_DIR)
				continue;

			if(!strncmp(cntentry->d_name, "systemd-nspawn@", 15))
				strlcpy(cntname, cntentry->d_name+15, strlen(cntentry->d_name+15)-7);
			else
				strlcpy(cntname, cntentry->d_name, strlen(cntentry->d_name)+1);

			if (stat == LINUX_MEMORY)
			{
				snprintf(memory_stat, 1000, "%s%s/memory.stat", cntpath, cntentry->d_name);

				FILE *fd = fopen(memory_stat, "r");
				if (!fd)
				{
					closedir(dp);
					closedir(cntdp);
					return;
				}

				while(fgets(buf, 1000, fd))
				{
					if (strncmp(buf, "total_", 6))
						continue;

					cur1 = strcspn(buf+6, " \t");
					strlcpy(mname, buf+6, cur1+1);
					cur1 += strspn(buf+6+cur1, " \t");
					mval = atoll(buf+6+cur1);
					metric_add_labels2("memory_vm", &mval, DATATYPE_INT, ac->system_carg, "vm", cntname, "type", mname);
				}

				snprintf(memory_stat, 1000, "%s%s/memory.memsw.failcnt", cntpath, cntentry->d_name);
				mval = getkvfile(memory_stat);
				metric_add_labels2("memory_vm_fails", &mval, DATATYPE_INT, ac->system_carg, "vm", cntname, "type", "memsw");

				snprintf(memory_stat, 1000, "%s%s/memory.failcnt", cntpath, cntentry->d_name);
				mval = getkvfile(memory_stat);
				metric_add_labels2("memory_vm_fails", &mval, DATATYPE_INT, ac->system_carg, "vm", cntname, "type", "mem");

				snprintf(memory_stat, 1000, "%s%s/memory.kmem.failcnt", cntpath, cntentry->d_name);
				mval = getkvfile(memory_stat);
				metric_add_labels2("memory_vm_fails", &mval, DATATYPE_INT, ac->system_carg, "vm", cntname, "type", "kmem");

				snprintf(memory_stat, 1000, "%s%s/memory.kmem.tcp.failcnt", cntpath, cntentry->d_name);
				mval = getkvfile(memory_stat);
				metric_add_labels2("memory_vm_fails", &mval, DATATYPE_INT, ac->system_carg, "vm", cntname, "type", "kmem_tcp");
			}
			else if (stat == LINUX_CPU)
			{
				snprintf(memory_stat, 1000, "%s%s/cpu.cfs_period_us", cntpath, cntentry->d_name);
				int64_t cfs_period = getkvfile(memory_stat);
				snprintf(memory_stat, 1000, "%s%s/cpu.cfs_quota_us", cntpath, cntentry->d_name);
				int64_t cfs_quota = getkvfile(memory_stat);
				if ( cfs_quota < 0 )
					cfs_quota = 1;


				snprintf(memory_stat, 1000, "%s%s/cpuacct.usage_sys", cntpath, cntentry->d_name);
				int64_t cgroup_system_ticks = getkvfile(memory_stat);
				double cgroup_system_usage = cgroup_system_ticks*cfs_period/1000000000/cfs_quota*100.0;

				snprintf(memory_stat, 1000, "%s%s/cpuacct.usage_user", cntpath, cntentry->d_name);
				int64_t cgroup_user_ticks = getkvfile(memory_stat);
				double cgroup_user_usage = cgroup_user_ticks*cfs_period/1000000000/cfs_quota*100.0;

				double cgroup_total_usage = cgroup_system_usage + cgroup_user_usage;
				metric_add_labels2("cpu_usage_vm", &cgroup_system_usage, DATATYPE_DOUBLE, ac->system_carg, "vm", cntname, "type", "system");
				metric_add_labels2("cpu_usage_vm", &cgroup_user_usage, DATATYPE_DOUBLE, ac->system_carg, "vm", cntname, "type", "user");
				metric_add_labels2("cpu_usage_vm", &cgroup_total_usage, DATATYPE_DOUBLE, ac->system_carg, "vm", cntname, "type", "total");
			}
		}
		closedir(cntdp);
	}

	closedir(dp);
}

void get_memory_errors()
{
	extern aconf *ac;
	struct dirent *entry;
	DIR *dp;

	dp = opendir("/sys/devices/system/edac/mc/mc0/");
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
		snprintf(rowname, 255, "/sys/devices/system/edac/mc/mc0/%s/", entry->d_name);
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
	extern aconf *ac;
	char fname[255];
	char monname[255];
	char devname[255];
	char *tmp;
	char name[255];
	int64_t temp;
	struct dirent *entry;
	DIR *dp;

	dp = opendir("/sys/class/hwmon/");
	if (!dp)
		return;

	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;
	
		struct dirent *monentry;
		DIR *mondp;

		snprintf(monname, 255, "/sys/class/hwmon/%s/", entry->d_name);
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
	FILE *fd = fopen("/proc/partitions", "r");
	if (!fd)
		return;

	char buf[1000];
	char dev[255];
	strlcpy(dev, "/dev/", 6);
	uint64_t cur;
	fgets(buf, 1000, fd);
	fgets(buf, 1000, fd);
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
	extern aconf *ac;
	FILE *fd = fopen("/proc/buddyinfo", "r");
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
	extern aconf *ac;
	FILE *fd = fopen("/proc/version", "r");
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
	extern aconf *ac;
	char genpath[255];
	char val[255];
	int64_t ival;
	snprintf(genpath, 255, "/proc/%"d64"/status", (int64_t)getpid());
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
			metric_add_labels("alligator_memory_usage", &ival, DATATYPE_INT, ac->system_carg, "type", "rss");
		if ( !strncmp(tmp, "VmSize", 6) )
			metric_add_labels("alligator_memory_usage", &ival, DATATYPE_INT, ac->system_carg, "type", "vsz");
	}
	fclose(fd);
}

void get_packages_info()
{
	extern aconf *ac;
	if (ac->rpmlib)
		get_rpm_info(ac->rpmlib);
}

void clear_counts_for(void* arg)
{
	match_string* ms = arg;

	ms->count = 0;
}

void clear_counts_process()
{
	extern aconf *ac;
	if (!ac->process_match)
		return;
	tommy_hashdyn *hash = ac->process_match->hash;
	tommy_hashdyn_foreach(hash, clear_counts_for);

	regex_list *node = ac->process_match->head;
	while (node)
	{
		node->count = 0;
		node = node->next;
	}
}

void fill_counts_for(void* arg)
{
	extern aconf *ac;
	match_string* ms = arg;

	metric_add_labels("process_match", &ms->count, DATATYPE_UINT, ac->system_carg, "name", ms->s);
}

void fill_counts_process()
{
	extern aconf *ac;
	if (!ac->process_match)
		return;
	tommy_hashdyn *hash = ac->process_match->hash;
	tommy_hashdyn_foreach(hash, fill_counts_for);

	regex_list *node = ac->process_match->head;
	while (node)
	{
		metric_add_labels("process_match", &node->count, DATATYPE_UINT, ac->system_carg, "name", node->name);
		node = node->next;
	}
}

void get_system_metrics()
{
	extern aconf *ac;
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
		if (!platform)
		{
			get_memory_errors();
			get_thermal();
			get_buddyinfo();
		}
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
		get_netstat_statistics("/proc/net/netstat");
		get_netstat_statistics("/proc/net/snmp");
		interface_stats();
		get_net_tcpudp("/proc/net/tcp", "tcp");
		get_net_tcpudp("/proc/net/tcp6", "tcp6");
		get_net_tcpudp("/proc/net/udp", "udp");
		get_net_tcpudp("/proc/net/udp6", "udp6");
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

	if (ac->system_vm)
	{
		cgroup_vm("/sys/fs/cgroup/memory/", "/sys/fs/cgroup/memory/%s/", LINUX_MEMORY);
		cgroup_vm("/sys/fs/cgroup/cpuacct/", "/sys/fs/cgroup/cpuacct/%s/", LINUX_CPU);
	}

	if (ac->system_smart)
	{
		if (platform == -1)
			platform = get_platform(0);
		if (!platform)
			get_smart_info();
	}

	if (ac->fdesc)
	{
		tommy_hashdyn_foreach_arg(ac->fdesc, process_fdescriptors_free, ac->fdesc);
		tommy_hashdyn_done(ac->fdesc);
		tommy_hashdyn_init(ac->fdesc);
	}
	if (ac->system_firewall)
	{
		get_iptables_info("filter", ac->system_carg);
		get_iptables_info("nat", ac->system_carg);
		get_iptables6_info("filter", ac->system_carg);
		get_iptables6_info("nat", ac->system_carg);
	}
}

void system_fast_scrape()
{
	extern aconf *ac;
	if (ac->system_base)
	{
		get_cpu(get_platform(0));
	}
}

void system_slow_scrape()
{
	extern aconf *ac;
	if (ac->system_packages)
	{
		get_packages_info();
	}
}

#endif
