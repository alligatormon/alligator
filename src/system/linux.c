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
#include <fcntl.h>
#include "main.h"
#define LINUXFS_LINE_LENGTH 300
#define d64 PRId64

//#include "rbtree.h"
#include "dstructures/metric.h"

typedef struct cpuusage_cgroup
{
	int64_t sys;
	int64_t user;
} cpuusage_cgroup;

int64_t getkvfile(char *file)
{
	char temp[20];
	FILE *fd = fopen(file, "r");
	if (!fd)
		return 0;

	if ( !fgets(temp, 20, fd) )
		return 0;

	fclose(fd);

	return atoll(temp);
}

void print_mount(const struct mntent *fs)
{
	if ( !strcmp(fs->mnt_type,"tmpfs") || !strcmp(fs->mnt_type,"xfs") || !strcmp(fs->mnt_type,"ext4") || !strcmp(fs->mnt_type,"btrfs") || !strcmp(fs->mnt_type,"ext3") || !strcmp(fs->mnt_type,"ext2") )
	{
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
				metric_labels_add_lbl2("disk_usage", &total, ALLIGATOR_DATATYPE_INT, 0, "mountpoint", fs->mnt_dir, "type", "total");
				metric_labels_add_lbl2("disk_usage", &avail, ALLIGATOR_DATATYPE_INT, 0, "mountpoint", fs->mnt_dir, "type", "avail");
				metric_labels_add_lbl2("disk_usage", &used, ALLIGATOR_DATATYPE_INT, 0, "mountpoint", fs->mnt_dir, "type", "used");
			}

			close(f_d);
		}
	}
}

void get_disk()
{
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

cpuusage_cgroup* get_cpu_usage_cgroup(size_t num_cpus_cgroup)
{
	cpuusage_cgroup *cgr;
	char temp[LINUXFS_LINE_LENGTH];
	int64_t i;
	FILE *fd = fopen("/sys/fs/cgroup/cpuacct/cpuacct.usage_all", "r");
	if ( !fd )
		return NULL;

	cgr = malloc(sizeof(cpuusage_cgroup)*num_cpus_cgroup);
	if (!fgets(temp, LINUXFS_LINE_LENGTH, fd))
	{
		fclose(fd);
		return;
	}
	for (i=0; fgets(temp, LINUXFS_LINE_LENGTH, fd); i++ )
	{
		int64_t t1, t2, t3;
		sscanf(temp, "%"d64" %"d64" %"d64"", &t1, &t2, &t3);
		cgr[i].user = t2;
		cgr[i].sys = t3;
	}
	fclose(fd);
	return cgr;
}

void get_cpu()
{

	int64_t effective_cores;
	int64_t num_cpus = sysconf( _SC_NPROCESSORS_ONLN );
	int64_t num_cpus_cgroup = (getkvfile("/sys/fs/cgroup/cpu/cpu.cfs_quota_us")/100000);
	if ( num_cpus_cgroup <= 0 )
		effective_cores = num_cpus;
	else if ( num_cpus < num_cpus_cgroup )
		effective_cores = num_cpus;
	else
		effective_cores = num_cpus_cgroup;


	metric_labels_add_auto("cores_num", &num_cpus, ALLIGATOR_DATATYPE_INT, 0);
	metric_labels_add_auto("cgroup_cores_num", &num_cpus_cgroup, ALLIGATOR_DATATYPE_INT, 0);
	metric_labels_add_auto("effective_cores_num", &effective_cores, ALLIGATOR_DATATYPE_INT, 0);

	char temp[LINUXFS_LINE_LENGTH];
	FILE *fd = fopen("/proc/stat", "r");
	if ( !fd )
		return;

	while ( fgets(temp, LINUXFS_LINE_LENGTH, fd) )
	{
		if ( !strncmp(temp, "cpu", 3) )
		{
			int64_t t1, t2, t3, t4, t5;
			char cpuname[5];
			sscanf(temp, "%s %"d64" %"d64" %"d64" %"d64" %"d64"", cpuname, &t1, &t2, &t3, &t4, &t5);
			double usage = (double)(t1 + t3)*100/(t1+t3+t4);
			double user = (double)(t1)*100/(t1+t3+t4);
			double nice = (double)(t2)*100/(t1+t3+t4);
			double system = (double)(t3)*100/(t1+t3+t4);
			double idle = (double)(t4)*100/(t1+t3+t4);
			double iowait = (double)(t5)*100/(t1+t3+t4);
			metric_labels_add_lbl("cpu_usage", &usage, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "usage");
			metric_labels_add_lbl("cpu_usage", &user, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "user");
			metric_labels_add_lbl("cpu_usage", &nice, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "nice");
			metric_labels_add_lbl("cpu_usage", &system, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "system");
			metric_labels_add_lbl("cpu_usage", &idle, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "idle");
			metric_labels_add_lbl("cpu_usage", &iowait, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "iowait");
		}
	}
	fclose(fd);

	int64_t cfs_period = getkvfile("/sys/fs/cgroup/cpu/cpu.cfs_period_us");
	int64_t cfs_quota = getkvfile("/sys/fs/cgroup/cpu/cpu.cfs_quota_us");
	if ( cfs_quota < 0 )
		return;


	int64_t cgroup_system_ticks1 = getkvfile("/sys/fs/cgroup/cpuacct/cpuacct.usage_sys");
	int64_t cgroup_user_ticks1 = getkvfile("/sys/fs/cgroup/cpuacct/cpuacct.usage_user");
	sleep(1);
	int64_t cgroup_system_ticks2 = getkvfile("/sys/fs/cgroup/cpuacct/cpuacct.usage_sys");
	int64_t cgroup_user_ticks2 = getkvfile("/sys/fs/cgroup/cpuacct/cpuacct.usage_user");

	double cgroup_system_usage = (double)(cgroup_system_ticks2 - cgroup_system_ticks1)*cfs_period/1000000000/cfs_quota*100;
	double cgroup_user_usage = (double)(cgroup_user_ticks2 - cgroup_user_ticks1)*cfs_period/1000000000/cfs_quota*100;
	double cgroup_total_usage = cgroup_system_usage + cgroup_user_usage;
	metric_labels_add_lbl("cpu_cgroup_usage", &cgroup_system_usage, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "system");
	metric_labels_add_lbl("cpu_cgroup_usage", &cgroup_user_usage, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "user");
	metric_labels_add_lbl("cpu_cgroup_usage", &cgroup_total_usage, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "total");
	//cpuusage_cgroup *cgr1 = get_cpu_usage_cgroup(num_cpus_cgroup);

	//sleep(1);
	//cpuusage_cgroup *cgr2;
	//get_cpu_usage_cgroup(&cgr2, num_cpus_cgroup);
	//for ( i=0; i<num_cpus_cgroup; i++)
	//{
	//	double cgroup_system_usage = (double)(cgr2[i].sys - cgr1[i].sys)*cfs_period/1000000000/cfs_quota*100;
	//	printf("system (%"d64"-%"d64")*%"d64"/1000000000/%"d64"*100\n", cgr2[i].user, cgr1[i].user, cfs_period, cfs_quota );
	//	double cgroup_user_usage = (double)(cgr2[i].user - cgr1[i].user)*cfs_period/1000000000/cfs_quota*100;
	//	printf("cgroup cpu%lld: has usage user %lf sys %lf\n", i, cgroup_user_usage, cgroup_system_usage);
	//	char cpusel[10];
	//	snprintf(cpusel, 10, "cpu%"d64"\n", i);
	//	metric_labels_add_auto("cpu_cgroup_usage", &cgroup_system_usage, ALLIGATOR_DATATYPE_DOUBLE, 0);
	//	metric_labels_add_auto("cpu_cgroup_usage", &cgroup_user_usage, ALLIGATOR_DATATYPE_DOUBLE, 0);
	//	puts("===========");
	//}
}

void get_process_extra_info(char *file)
{
	FILE *fd = fopen(file, "r");
	if (!fd)
		return;
	char tmp[LINUXFS_LINE_LENGTH];
	char key[LINUXFS_LINE_LENGTH];
	char val[LINUXFS_LINE_LENGTH];
	int64_t ival = 1;
	int64_t ctxt_switches = 0;
	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		tmp[strlen(tmp)-1] = 0;
		int i;
		for (i=0; tmp[i] && tmp[i]!=' '; i++);
		strlcpy(key, tmp, i+1);

		int swtch = 0;

		if	  ( !strcmp(key, "Threads") ) { swtch = 1; }
		else if ( !strcmp(key, "voluntary_ctxt_switches") ) { swtch = 2; }
		else if ( !strcmp(key, "nonvoluntary_ctxt_switches") ) { swtch = 2; }
		else	continue;

		for (; tmp[i]==' '; i++);
		int swap = i;
		for (; tmp[i] && tmp[i]!=' '; i++);
		strlcpy(val, tmp+swap, i-swap+1);

		ival = atoll(val);
		switch ( swtch )
		{
			case '1':
				metric_labels_add_lbl("process_stats", &ival, ALLIGATOR_DATATYPE_INT, 0, "type", "threads" );
				break;
			case '2':
				if ( ctxt_switches != 0 )
				{
					ctxt_switches += ival;
					metric_labels_add_lbl("process_stats", &ival, ALLIGATOR_DATATYPE_INT, 0, "type", "ctx_switches" );
				}
				else
				{
					ctxt_switches += ival;
				}
				break;
			default: break;
		}
	}

	fclose(fd);
}

int64_t int_get_next(char *buf, size_t sz, char sep, int64_t *cursor)
{
	for (; *cursor<sz; ++(*cursor))
	{
		for (; *cursor<sz && buf[*cursor]==sep; ++(*cursor));
		if (isdigit(buf[*cursor]) || buf[*cursor] == '-')
		{
			int64_t ret = atoll(buf+(*cursor));
			for (; *cursor<sz && (isdigit(buf[*cursor]) || buf[*cursor] == '-'); ++(*cursor));
			++(*cursor);

			return ret;
		}
	}
	return 0;
}

void get_proc_info(char *szFileName, char *exName, char *pid_number)
{
	char szStatStr[LINUXFS_LINE_LENGTH];
	//char		state;	
	/** 1 **/			/** R is running, S is sleeping,
				D is sleeping in an uninterruptible wait,
				Z is zombie, T is traced or stopped **/
	int64_t	utime;
	int64_t	stime;
	int64_t	cutime;
	int64_t	cstime;
	int64_t	vsize;
	int64_t	rss;

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
		int_get_next(t+4, sz, ' ', &cursor);
	utime = int_get_next(t+4, sz, ' ', &cursor);
	stime = int_get_next(t+4, sz, ' ', &cursor);
	cutime = int_get_next(t+4, sz, ' ', &cursor);
	cstime = int_get_next(t+4, sz, ' ', &cursor);

	cnt = 5;
	while (cnt--)
		int_get_next(t+4, sz, ' ', &cursor);

	//starttime = int_get_next(t+4, sz, ' ', &cursor);
	vsize = int_get_next(t+4, sz, ' ', &cursor);
	rss = int_get_next(t+4, sz, ' ', &cursor);
	fclose (fp);

	int64_t Hertz = sysconf(_SC_CLK_TCK);
	int64_t stotal_time = stime + cstime;
	int64_t utotal_time = utime + cutime;
	int64_t total_time = stotal_time + utotal_time;

	//double seconds = uptime - (starttime / Hertz);
	struct stat st;
	stat(szFileName, &st);
	double seconds = time(0) - st.st_ctime;

	double sys_cpu_usage = 100 * ((stotal_time / Hertz) / seconds);
	double user_cpu_usage = 100 * ((utotal_time / Hertz) / seconds);
	double total_cpu_usage = 100 * ((total_time / Hertz) / seconds);
	//printf("szStatStr %s\n", szStatStr);
	//printf("starttime %"d64"\n", starttime);
	//printf("utime %"d64"\n", utime);
	//printf("stime %"d64"\n", stime);
	//printf("cutime %"d64"\n", cutime);
	//printf("cstime %"d64"\n", cstime);
	//printf("vsize %"d64"\n", vsize);
	//printf("rss %"d64"\n", rss);
	//printf("uptime %"d64"\n", uptime);
	//printf("pid name: %s go\n", pid_number);
	//printf("old seconds = %"d64" - (%"d64" / %"d64") = %lf\n", uptime, starttime, Hertz, seconds);
	//printf("new seconds = %ld - %ld = %lf\n", time(0), st.st_ctime, seconds);
	//printf("cpu usage sys: 100 * ((%"d64" / %"d64") / %lf) = %lf\n", stotal_time, Hertz, seconds, sys_cpu_usage);
	//printf("cpu usage user: 100 * ((%"d64" / %"d64") / %lf) = %lf\n", utotal_time, Hertz, seconds, user_cpu_usage);

	metric_labels_add_lbl3("process_memory", &rss, ALLIGATOR_DATATYPE_INT, 0, "name", exName, "pid", pid_number, "type", "rss");
	metric_labels_add_lbl3("process_memory", &vsize, ALLIGATOR_DATATYPE_INT, 0, "name", exName, "pid", pid_number, "type", "vsz");
	metric_labels_add_lbl3("process_cpu", &sys_cpu_usage, ALLIGATOR_DATATYPE_DOUBLE, 0, "name", exName, "pid", pid_number, "type", "system");
	metric_labels_add_lbl3("process_cpu", &user_cpu_usage, ALLIGATOR_DATATYPE_DOUBLE, 0, "name", exName, "pid", pid_number, "type", "user");
	metric_labels_add_lbl3("process_cpu", &total_cpu_usage, ALLIGATOR_DATATYPE_DOUBLE, 0, "name", exName, "pid", pid_number, "type", "total");
}

int64_t get_fd_info_process(char *fddir)
{
	struct dirent *entry;
	DIR *dp;

	dp = opendir(fddir);
	if (!dp)
	{
		//perror("opendir");
		return 0;
	}

	char *dir = malloc(FILENAME_MAX);
	int64_t i = 0;
	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;
		i++;
	}

	free(dir);
	closedir(dp);
	return i;
}

void get_process_io_stat(char *file, char *command)
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
		metric_labels_add_lbl2("process_io", &val, ALLIGATOR_DATATYPE_INT, 0, "name", command, "type", buf2 );
	}
	fclose(fd);
}

void find_pid()
{
	struct dirent *entry;
	DIR *dp;

	dp = opendir("/proc");
	if (!dp)
	{
		//perror("opendir");
		return;
	}

	char *dir = malloc(FILENAME_MAX);
	while((entry = readdir(dp)))
	{
		if ( !isdigit(entry->d_name[0]) )
			continue;

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
		procname[strlen(procname)-1] = '\0';
		fclose(fd);

		snprintf(dir, FILENAME_MAX, "/proc/%s/stat", entry->d_name);


		get_proc_info(dir, procname, entry->d_name);

		snprintf(dir, FILENAME_MAX, "/proc/%s/status", entry->d_name);
		get_process_extra_info(dir);

		snprintf(dir, FILENAME_MAX, "/proc/%s/fd", entry->d_name);
		int64_t filesnum = get_fd_info_process(dir);
		if (filesnum)
			metric_labels_add_lbl2("process_stats", &filesnum, ALLIGATOR_DATATYPE_INT, 0, "name", procname, "type", "openfiles" );

		snprintf(dir, FILENAME_MAX, "/proc/%s/io", entry->d_name);
		get_process_io_stat(dir, procname);
	}

	free(dir);
	closedir(dp);
}

void get_mem()
{
	FILE *fd = fopen("/proc/meminfo", "r");
	if (fd)
	{
		char tmp[LINUXFS_LINE_LENGTH];
		char key[LINUXFS_LINE_LENGTH];
		char val[LINUXFS_LINE_LENGTH];
		int64_t ival = 1;
		int64_t totalswap = 1;
		int64_t freeswap = 1;
		while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
		{
			tmp[strlen(tmp)-1] = 0;
			int i;
			for (i=0; tmp[i]!=' '; i++);
			strlcpy(key, tmp, i);

			if	  ( !strcmp(key, "MemTotal") ) {}
			else if ( !strcmp(key, "MemFree") ) {}
			else if ( !strcmp(key, "Inactive") ) {}
			else if ( !strcmp(key, "Active") ) {}
			else if ( !strcmp(key, "SwapTotal") ) {}
			else if ( !strcmp(key, "SwapFree") ) {}
			else if ( !strcmp(key, "Buffers") ) {}
			else if ( !strcmp(key, "Cached") ) {}
			else if ( !strcmp(key, "Mapped") ) {}
			else if ( !strcmp(key, "Shmem") ) {}
			else	continue;

			for (; tmp[i]==' '; i++);
			int swap = i;
			for (; tmp[i]!=' '; i++);
			strlcpy(val, tmp+swap, i-swap+1);

			if ( strstr(tmp+swap, "kB") )
				ival = 1024;

			ival = ival * atoll(val);
				 if ( !strcmp(key, "SwapTotal") ) { totalswap = ival; }
			else if ( !strcmp(key, "SwapFree") ) { freeswap = ival;  }
			metric_labels_add_lbl("memory_usage", &ival, ALLIGATOR_DATATYPE_INT, 0, "type", key);
		}
		int64_t usageswap = totalswap - freeswap;
		metric_labels_add_lbl("memory_usage", &usageswap, ALLIGATOR_DATATYPE_INT, 0, "type", "SwapUsage");
	}
	fclose(fd);
}

void cgroup_mem()
{
	FILE *fd = fopen("/sys/fs/cgroup/memory/memory.stat", "r");
	if (fd)
	{
		char tmp[LINUXFS_LINE_LENGTH];
		char key[LINUXFS_LINE_LENGTH];
		char val[LINUXFS_LINE_LENGTH];
		int64_t ival = 1;
		while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
		{
			tmp[strlen(tmp)-1] = 0;
			int i;
			for (i=0; tmp[i]!=' '; i++);
			strlcpy(key, tmp, i+1);

			if	  ( !strcmp(key, "total_cache") ) {}
			else if ( !strcmp(key, "total_rss") ) {}
			else if ( !strcmp(key, "hierarchical_memory_limit") ) {}
			else	continue;

			for (; tmp[i]==' '; i++);
			int swap = i;
			for (; tmp[i] && tmp[i]!=' '; i++);
			strlcpy(val, tmp+swap, i-swap+1);

			ival = atoll(val);
			metric_labels_add_lbl("memory_cgroup_usage", &ival, ALLIGATOR_DATATYPE_INT, 0, "type", key );
		}
	}
	fclose(fd);
}

void get_tcpudp_stat(char *file, char *name)
{
	FILE *fd = fopen(file, "r");
	if (!fd)
		return;

	char *buf = malloc(500);
	if(!fgets(buf, 500, fd))
	{
		free(buf);
		fclose(fd);
		return;
	}
	int64_t udpstat[9];

	int64_t retransmit = 0;
	int64_t timeout = 0;
	int64_t temptx = 0;
	int64_t temprx = 0;
	int64_t queuetx = 0;
	int64_t queuerx = 0;
	int64_t i, j, cnt = 0;
	while ( fgets(buf, 500, fd) )
	{
		size_t len = strlen(buf);
		for (i=0, j=0; i<len && j<9; i++, j++)
		{
			int64_t val = atoll(buf+i);
			udpstat[j] = val;
			if ( j == 4 )
			{
				sscanf(buf+i, "%"PRId64":%"PRId64"", &temptx, &temprx);
			}

			i += strcspn(buf+i, " \t");
		}
		retransmit += udpstat[6];
		timeout += udpstat[8];
		queuerx += temprx;
		queuetx += temptx;
		cnt++;
	}
	metric_labels_add_lbl2("socket_stat", &retransmit, ALLIGATOR_DATATYPE_INT, 0, "proto", name, "type", "retransmit");
	metric_labels_add_lbl2("socket_stat", &timeout, ALLIGATOR_DATATYPE_INT, 0, "proto", name, "type", "timeout");
	metric_labels_add_lbl2("socket_stat", &cnt, ALLIGATOR_DATATYPE_INT, 0, "proto", name, "type", "count");
	metric_labels_add_lbl2("socket_stat", &queuetx, ALLIGATOR_DATATYPE_INT, 0, "proto", name, "type", "rx_queue");
	metric_labels_add_lbl2("socket_stat", &queuerx, ALLIGATOR_DATATYPE_INT, 0, "proto", name, "type", "tx_queue");
	free(buf);
	fclose(fd);
}

void get_netstat_statistics()
{
	FILE *fp = fopen("/proc/net/netstat", "r");
	char *buf = malloc(10000);
	int64_t netstat[118];
	if(!fgets(buf, 10000, fp))
	{
		fclose(fp);
		return;
	}
	if(!fgets(buf, 10000, fp))
	{
		fclose(fp);
		return;
	}
	int64_t i;
	int64_t j;
	size_t len = strlen(buf) -1;
	buf[len] = '\0';
	for (i=0, j=0; i<len && j<118; i++, j++)
	{
		int64_t val = atoll(buf+i);
		netstat[j] = val;
		metric_labels_add_lbl("network_stat", &netstat[0], ALLIGATOR_DATATYPE_INT, 0, "type", "SyncookiesSent");
		metric_labels_add_lbl("network_stat", &netstat[1], ALLIGATOR_DATATYPE_INT, 0, "type", "SyncookiesRecv");
		metric_labels_add_lbl("network_stat", &netstat[2], ALLIGATOR_DATATYPE_INT, 0, "type", "SyncookiesFailed");
		metric_labels_add_lbl("network_stat", &netstat[10], ALLIGATOR_DATATYPE_INT, 0, "type", "TW");
		metric_labels_add_lbl("network_stat", &netstat[11], ALLIGATOR_DATATYPE_INT, 0, "type", "TWRecycled");
		metric_labels_add_lbl("network_stat", &netstat[12], ALLIGATOR_DATATYPE_INT, 0, "type", "TWKilled");
		metric_labels_add_lbl("network_stat", &netstat[16], ALLIGATOR_DATATYPE_INT, 0, "type", "DelayedACKs");
		metric_labels_add_lbl("network_stat", &netstat[17], ALLIGATOR_DATATYPE_INT, 0, "type", "DelayedACKLocked");
		metric_labels_add_lbl("network_stat", &netstat[18], ALLIGATOR_DATATYPE_INT, 0, "type", "DelayedACKLost");
		metric_labels_add_lbl("network_stat", &netstat[19], ALLIGATOR_DATATYPE_INT, 0, "type", "ListenOverflows");
		metric_labels_add_lbl("network_stat", &netstat[20], ALLIGATOR_DATATYPE_INT, 0, "type", "ListenDrops");
		metric_labels_add_lbl("network_stat", &netstat[111], ALLIGATOR_DATATYPE_INT, 0, "type", "TCPMemoryPressures");
		metric_labels_add_lbl("network_stat", &netstat[110], ALLIGATOR_DATATYPE_INT, 0, "type", "TCPAbortFailed");
		metric_labels_add_lbl("network_stat", &netstat[108], ALLIGATOR_DATATYPE_INT, 0, "type", "TCPAbortOnTimeout");
		metric_labels_add_lbl("network_stat", &netstat[107], ALLIGATOR_DATATYPE_INT, 0, "type", "TCPAbortOnMemory");

		i += strcspn(buf+i, " \t");
	}

	free(buf);
	fclose(fp);
}

void get_network_statistics()
{
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

		strlcpy(ifname, buf+from, to+1);
		int64_t cursor = 0;
		size_t sz = strlen(buf+from+to);

		received_bytes = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &received_bytes, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "received_bytes");

		received_packets = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &received_packets, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "received_packets");

		received_err = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &received_err, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "received_err");

		received_drop = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &received_drop, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "received_drop");

		received_fifo = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &received_fifo, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "received_fifo");

		received_frame = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &received_frame, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "received_frame");

		received_compressed = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &received_compressed, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "received_compressed");

		received_multicast = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &received_multicast, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "received_multicast");

		transmit_bytes = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &transmit_bytes, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "transmit_bytes");

		transmit_packets = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &transmit_packets, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "transmit_packets");

		transmit_err = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &transmit_err, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "transmit_err");

		transmit_drop = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &transmit_drop, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "transmit_drop");

		transmit_fifo = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &transmit_fifo, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "transmit_fifo");

		transmit_colls = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &transmit_colls, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "transmit_colls");

		transmit_carrier = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &transmit_carrier, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "transmit_carrier");

		transmit_compressed = int_get_next(buf+from+to, sz, ' ', &cursor);
		metric_labels_add_lbl2("if_stat", &transmit_compressed, ALLIGATOR_DATATYPE_INT, 0, "ifname", ifname, "type", "transmit_compressed");
	}

	fclose(fp);
}

void get_nofile_stat()
{
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
	file_open = stat[0];
	kern_file_max = stat[2];
	metric_labels_add_auto("open_files", &file_open, ALLIGATOR_DATATYPE_INT, 0);
	metric_labels_add_auto("max_files", &kern_file_max, ALLIGATOR_DATATYPE_INT, 0);
	fclose(fd);
}

void get_disk_io_stat()
{
	FILE *fd = fopen("/proc/diskstats", "r");
	if (!fd)
		return;

	int64_t stat[14];
	char buf[LINUXFS_LINE_LENGTH];
	while ( fgets(buf, LINUXFS_LINE_LENGTH, fd) )
	{
		int64_t i, j;
		size_t len = strlen(buf) - 1;
		buf[len] = '\0';
		char devname[50];
		for (i=0, j=0; i<len && j<14; i++, j++)
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
		char bldevname[UCHAR_MAX];
		snprintf(bldevname, UCHAR_MAX, "/sys/block/%s/queue/hw_sector_size", devname);
		int64_t sectorsize = getkvfile(bldevname);

		int64_t read_bytes = stat[3] * sectorsize;
		int64_t read_rate = stat[5];
		int64_t read_timing = stat[6];
		int64_t write_bytes = stat[7] * sectorsize;
		int64_t write_rate = stat[9];
		int64_t write_timing = stat[10];
		metric_labels_add_lbl2("disk_io", &read_rate, ALLIGATOR_DATATYPE_INT, 0, "dev", devname, "type", "transfers_read");
		metric_labels_add_lbl2("disk_io", &write_rate, ALLIGATOR_DATATYPE_INT, 0, "dev", devname, "type", "transfers_write");
		metric_labels_add_lbl2("disk_io", &read_timing, ALLIGATOR_DATATYPE_INT, 0, "dev", devname, "type", "read_timing");
		metric_labels_add_lbl2("disk_io", &write_timing, ALLIGATOR_DATATYPE_INT, 0, "dev", devname, "type", "write_timing");
		metric_labels_add_lbl2("disk_io", &read_bytes, ALLIGATOR_DATATYPE_INT, 0, "dev", devname, "type", "bytes_read");
		metric_labels_add_lbl2("disk_io", &write_bytes, ALLIGATOR_DATATYPE_INT, 0, "dev", devname, "type", "bytes_write");
	}
	fclose(fd);
}

void get_loadavg()
{
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
	metric_labels_add_lbl("load_average", &load1, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "load1");
	metric_labels_add_lbl("load_average", &load5, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "load5");
	metric_labels_add_lbl("load_average", &load15, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "load15");

	fclose(fd);
}

void get_system_metrics()
{
	extern aconf *ac;
	if (ac->system_base)
	{
		get_cpu();
		get_mem();
		get_nofile_stat();
		get_loadavg();
		cgroup_mem();
	}

	if (ac->system_network)
	{
		get_network_statistics();
		get_netstat_statistics();
		get_tcpudp_stat("/proc/net/tcp", "tcp");
		get_tcpudp_stat("/proc/net/tcp6", "tcp6");
		get_tcpudp_stat("/proc/net/udp", "udp");
		get_tcpudp_stat("/proc/net/udp6", "udp6");
	}
	if (ac->system_disk)
	{
		get_disk_io_stat();
		get_disk();
	}

	if (ac->system_process)
	{
		find_pid();
	}
}

#endif
