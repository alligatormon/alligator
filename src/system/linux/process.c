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
#include <ctype.h>
#include <sys/types.h>
#include <fcntl.h>
#include "main.h"
#include "common/pw.h"
#include "common/selector.h"
#include "system/common.h"
#include "metric/labels.h"
#include "dstructures/ht.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "system/fdescriptors.h"
#include "common/rtime.h"
#include "system/linux/process.h"
#define LINUXFS_LINE_LENGTH 300
extern aconf *ac;

static void system_scrape_fopen_fail(const char *path, int optional)
{
	int pri = (optional && (errno == ENOENT || errno == ENOTDIR)) ? L_DEBUG : L_ERROR;
	carglog(ac->system_carg, pri, "system scrape fopen %s: %s\n", path, strerror(errno));
}

typedef struct process_states
{
	uint64_t running;
	uint64_t sleeping;
	uint64_t uninterruptible;
	uint64_t zombie;
	uint64_t stopped;
	uint64_t idle;
} process_states;

static char parse_proc_stat_state(const char *line)
{
	const char *r = strrchr(line, ')');
	if (!r)
		return 0;
	++r;
	while (*r == ' ')
		++r;
	return *r;
}

static void sched_state_inc(process_states *s, char state)
{
	if (!s)
		return;
	switch (state) {
	case 'R': ++s->running; break;
	case 'S': ++s->sleeping; break;
	case 'D': ++s->uninterruptible; break;
	case 'Z': ++s->zombie; break;
	case 'T':
	case 't': ++s->stopped; break;
	case 'I': ++s->idle; break;
	default: break;
	}
}

static void count_task_stat_file(const char *path, process_states *task_states)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return;
	char line[LINUXFS_LINE_LENGTH];
	if (fgets(line, sizeof(line), fp))
		sched_state_inc(task_states, parse_proc_stat_state(line));
	fclose(fp);
}

static void count_task_states(const char *pid, process_states *task_states)
{
	char taskdir[FILENAME_MAX];
	struct dirent *entry;
	DIR *dp;

	if (!pid || !task_states)
		return;

	snprintf(taskdir, sizeof(taskdir), "%s/%s/task", ac->system_procfs, pid);
	dp = opendir(taskdir);
	if (!dp) {
		char path[FILENAME_MAX];
		snprintf(path, sizeof(path), "%s/%s/stat", ac->system_procfs, pid);
		count_task_stat_file(path, task_states);
		return;
	}

	while ((entry = readdir(dp))) {
		char path[FILENAME_MAX];
		if (!isdigit(entry->d_name[0]))
			continue;
		snprintf(path, sizeof(path), "%s/%s/stat", taskdir, entry->d_name);
		count_task_stat_file(path, task_states);
	}
	closedir(dp);
}

typedef struct ulimit_pid_stat {
	uint64_t datasize;
	uint64_t stacksize;
	uint64_t rsssize;
	uint64_t openfiles;
	uint64_t lockedsize;
	uint64_t addressspace;
} ulimit_pid_stat;

int fd_sock_compare(const void* arg, const void* obj)
{
		char *s1 = (char*)arg;
		char *s2 = ((fd_info*)obj)->key;
		return strcmp(s1, s2);
}


ulimit_pid_stat* get_pid_ulimit_stat(char *path)
{
	FILE *fd = fopen(path, "r");
	if (!fd) {
		system_scrape_fopen_fail(path, 1);
		return NULL;
	}

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
	carglog(ac->system_carg, L_DEBUG, "rlimit: %s: datasize = %"PRIu64"\n", path, ups->datasize);
	carglog(ac->system_carg, L_DEBUG, "rlimit: %s: stacksize = %"PRIu64"\n", path, ups->stacksize);
	carglog(ac->system_carg, L_DEBUG, "rlimit: %s: rsssize = %"PRIu64"\n", path, ups->rsssize);
	carglog(ac->system_carg, L_DEBUG, "rlimit: %s: openfiles = %"PRIu64"\n", path, ups->openfiles);
	carglog(ac->system_carg, L_DEBUG, "rlimit: %s: lockedsize = %"PRIu64"\n", path, ups->lockedsize);
	carglog(ac->system_carg, L_DEBUG, "rlimit: %s: addressspace = %"PRIu64"\n", path, ups->addressspace);
	fclose(fd);
	return ups;
}

void only_calculate_threads(char *file, uint64_t *threads, char *name, char *pid, int match) {
	FILE *fd = fopen(file, "r");
	if (!fd)
		return;

	char tmp[LINUXFS_LINE_LENGTH];
	char key[LINUXFS_LINE_LENGTH];
	char val[LINUXFS_LINE_LENGTH];
	int64_t ival = 1;

	while (fgets(tmp, LINUXFS_LINE_LENGTH, fd))
	{
		size_t tmp_len = strlen(tmp)-1;
		tmp[tmp_len] = 0;
		int64_t i = strcspn(tmp, " \t");
		strlcpy(key, tmp, i+1);

		int swap = strspn(tmp+i, " \t")+i;
		int size = strcspn(tmp+swap, " \t");
		strlcpy(val, tmp+swap, size+1);

		ival = atoll(val);

		if  ( !strncmp(key, "Threads", 7) )
		{
			if (match > 0)
				metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "threads", "name", name, "pid", pid);
			if (threads)
				*threads += (uint64_t)ival;
		}
	}
	fclose(fd);
}

void get_process_extra_info(char *file, char *name, char *pid, ulimit_pid_stat* ups, uint64_t *threads, uint64_t shm_max)
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
		int size = strcspn(tmp+swap, " \t");
		strlcpy(val, tmp+swap, size+1);

		ival = atoll(val);

		if ( strstr(tmp+swap, "kB") )
			ival *= 1024;

		if  ( !strncmp(key, "Threads", 7) )
		{
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "threads", "name", name, "pid", pid);
			if (threads)
				*threads += (uint64_t)ival;
		}
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
		{
			metric_add_labels3("process_stats", &ival, DATATYPE_INT, ac->system_carg, "type", "shmem_bytes", "name", name, "pid", pid);
			double usage = ival * 1.0 / shm_max;
			metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg, "type", "shmem_bytes", "name", name, "pid", pid);
		}
		else	continue;
	}

	fclose(fd);
}

long linux_user_hz(void)
{
	static long hz;
	if (hz == 0)
	{
		long c = sysconf(_SC_CLK_TCK);
		hz = (c > 0) ? c : 100;
	}
	return hz;
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
	uint64_t cursor = 0;
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
				if (match > 0) {
					metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				}
				++states->running;
			}
			else if (state == 'S')
			{
				if (match > 0) {
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
					metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				}
				++states->sleeping;
			}
			else if (state == 'D')
			{
				if (match > 0) {
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
					metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				}
				++states->uninterruptible;
			}
			else if (state == 'Z')
			{
				if (match > 0) {
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
					metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				}
				++states->zombie;
			}
			else if (state == 'T')
			{
				if (match > 0) {
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "running");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "sleeping");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "uninterruptible");
					metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "zombie");
					metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "stopped");
				}
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
			metric_add_labels3("process_page_faults_total", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "minor");
		}
		else if (cnt == 1)
		{
			int64_t val = int_get_next(t+4, sz, ' ', &cursor);
			metric_add_labels3("process_page_faults_total", &val, DATATYPE_INT, ac->system_carg, "name", exName, "pid", pid_number, "type", "major");
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

	{
		long hz = linux_user_hz();
		double inv = 1.0 / (double)hz;
		double stotal_time = (double)(stime + cstime) * inv;
		double utotal_time = (double)(utime + cutime) * inv;
		double total_time = stotal_time + utotal_time;

		metric_add_labels3("process_cpu_seconds_total", &stotal_time, DATATYPE_DOUBLE, ac->system_carg, "name", exName, "pid", pid_number, "mode", "system");
		metric_add_labels3("process_cpu_seconds_total", &utotal_time, DATATYPE_DOUBLE, ac->system_carg, "name", exName, "pid", pid_number, "mode", "user");
		metric_add_labels3("process_cpu_seconds_total", &total_time, DATATYPE_DOUBLE, ac->system_carg, "name", exName, "pid", pid_number, "mode", "total");
	}
}

int userprocess_compare(const void* arg, const void* obj)
{
	uint32_t s1 = *(uint32_t*)arg;
	uint32_t s2 = ((userprocess_node*)obj)->uid;
	return s1 != s2;
}

int system_string_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((system_string_node*)obj)->name;
	return strcmp(s1, s2);
}

void get_proc_socket_number(char *path, char *procname, alligator_ht *files, int64_t *sockets, int64_t *pipes)
{
	char buf[255];
	ssize_t len = readlink(path, buf, 254);
	if (len < 0)
	{
		carglog(ac->system_carg, L_TRACE, "%s path readlink error: %s: %s\n", __FUNCTION__, path, strerror(errno));
		return;
	}

	buf[len] = 0;

	if (files) {
		char fd_key[512];
		if (!strncmp(buf, "socket:", 7)) {
			++(*sockets);
			strlcpy(fd_key, buf, 512);
		} else if (!strncmp(buf, "pipe:", 5)) {
			++(*pipes);
			strlcpy(fd_key, buf, 512);
		} else {
			strlcpy(fd_key, buf, 512);
			strlcpy(fd_key+len, path, 512 - len);
		}
		uint32_t fd_sock_hash = tommy_strhash_u32(0, fd_key);
		fd_info *fd_sock = alligator_ht_search(files, fd_sock_compare, fd_key, fd_sock_hash);
		if (!fd_sock) {
			fd_sock = malloc(sizeof(*fd_sock));
			fd_sock->key = strdup(fd_key);
			alligator_ht_insert(files, &(fd_sock->node), fd_sock, fd_sock_hash);
		}
	}

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

int64_t get_fd_info_process(char *fddir, char *procname, alligator_ht *files, int64_t *sockets, int64_t *pipes)
{
	char buf[PATH_MAX];
	size_t fddir_len = strlcpy(buf, fddir, sizeof(buf));
	if (fddir_len >= sizeof(buf))
		return 0;

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
			strlcpy(buf + fddir_len, entry->d_name, sizeof(buf) - fddir_len);
			get_proc_socket_number(buf, procname, files, sockets, pipes);
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
		char *sep = strchr(buf, ':');
		if (!sep)
			continue;
		size_t key_len = (size_t)(sep - buf);
		size_t key_copy = key_len < (sizeof(buf2) - 1) ? key_len : (sizeof(buf2) - 1);
		memcpy(buf2, buf, key_copy);
		buf2[key_copy] = 0;
		val = strtoll(sep + 1, NULL, 10);
		if (!strcmp(buf2, "syscr"))
			metric_add_labels3("process_io_syscalls_total", &val, DATATYPE_INT, ac->system_carg, "name", command, "op", "read", "pid", pid);
		else if (!strcmp(buf2, "syscw"))
			metric_add_labels3("process_io_syscalls_total", &val, DATATYPE_INT, ac->system_carg, "name", command, "op", "write", "pid", pid);
		else if (!strcmp(buf2, "rchar"))
			metric_add_labels3("process_io_chars_total", &val, DATATYPE_INT, ac->system_carg, "name", command, "op", "read", "pid", pid);
		else if (!strcmp(buf2, "wchar"))
			metric_add_labels3("process_io_chars_total", &val, DATATYPE_INT, ac->system_carg, "name", command, "op", "write", "pid", pid);
		else if (!strcmp(buf2, "read_bytes"))
			metric_add_labels3("process_io_bytes_total", &val, DATATYPE_INT, ac->system_carg, "name", command, "op", "read", "pid", pid);
		else if (!strcmp(buf2, "write_bytes"))
			metric_add_labels3("process_io_bytes_total", &val, DATATYPE_INT, ac->system_carg, "name", command, "op", "write", "pid", pid);
		else if (!strcmp(buf2, "cancelled_write_bytes"))
			metric_add_labels3("process_io_bytes_total", &val, DATATYPE_INT, ac->system_carg, "name", command, "op", "cancelled_write", "pid", pid);
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
		carglog(ac->system_carg, L_ERROR, "fgets %s: %s\n", fpath, strerror(errno));
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

	metric_add_labels2("process_schedstat_run_time_nanoseconds_total", &run_time, DATATYPE_UINT, ac->system_carg, "name", name, "pid", pid);
	metric_add_labels2("process_schedstat_runqueue_time_nanoseconds_total", &runqueue_time, DATATYPE_UINT, ac->system_carg, "name", name, "pid", pid);
	metric_add_labels2("process_schedstat_run_periods_total", &run_periods, DATATYPE_UINT, ac->system_carg, "name", name, "pid", pid);
}


void get_vmap_info(char *filename, char *pid, char *exName, uint64_t max_map_count) {
	uint64_t vmap_count = count_file_lines(filename);

	if (!vmap_count)
		carglog(ac->system_carg, L_ERROR, "%s not found vmaps for process from file: \n", __FUNCTION__, filename);
	else
		carglog(ac->system_carg, L_DEBUG, "%s found %"u64" vmaps from '%s'\n", __FUNCTION__, vmap_count, filename);

	metric_add_labels2("process_vmap_count", &vmap_count, DATATYPE_UINT, ac->system_carg, "name", exName, "pid", pid);
	if (max_map_count)
	{
		double usage = vmap_count * 1.0 / max_map_count;
		metric_add_labels2("process_vmap_usage", &usage, DATATYPE_DOUBLE, ac->system_carg, "name", exName, "pid", pid);
	}
}

int get_pid_info(char *pid, int64_t *allfilesnum, int8_t lightweight, process_states *states, process_states *task_states, int8_t need_match, alligator_ht *files, uint64_t *threads, uint64_t shm_max, uint64_t max_map_count)
{
	carglog(ac->system_carg, L_DEBUG, "get_pid_info for process %s, need match: %"PRId8"\n", pid, need_match);
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

	int8_t match = -1;
	if (need_match)
	{
		carglog(ac->system_carg, L_DEBUG, "%s check for match: '%s' by procname\n", __FUNCTION__, procname);
		if (!match_mapper(ac->process_match, procname, procname_size, procname))
		{
			carglog(ac->system_carg, L_DEBUG, "not matched, %s check for match '%s' by cmdline\n", __FUNCTION__, cmdline);
			if (!match_mapper(ac->process_match, cmdline, cmdline_size, procname))
			{
				carglog(ac->system_carg, L_DEBUG, "not matched\n");
				match = 0;
			}
			else
				carglog(ac->system_carg, L_DEBUG, "matched\n");
		}
		else
			carglog(ac->system_carg, L_DEBUG, "matched\n");
	}

	snprintf(dir, FILENAME_MAX, "%s/%s/stat", ac->system_procfs, pid);
	get_proc_info(dir, procname, pid, lightweight, states, match);
	count_task_states(pid, task_states);

	snprintf(dir, FILENAME_MAX, "%s/%s/fd/", ac->system_procfs, pid);
	int64_t socketsnum = 0;
	int64_t pipesnum = 0;
	int64_t filesnum = get_fd_info_process(dir, procname, files, &socketsnum, &pipesnum);
	if (match && filesnum && !lightweight)
	{
		metric_add_labels3("process_stats", &filesnum, DATATYPE_INT, ac->system_carg, "name", procname, "type", "open_files", "pid", pid);
		metric_add_labels3("process_stats", &socketsnum, DATATYPE_INT, ac->system_carg, "name", procname, "type", "open_sockets", "pid", pid);
		metric_add_labels3("process_stats", &pipesnum, DATATYPE_INT, ac->system_carg, "name", procname, "type", "open_pipes", "pid", pid);
	}
	*allfilesnum += filesnum;

	if (!match)
	{
		if (!lightweight)
		{
			snprintf(dir, FILENAME_MAX, "%s/%s/status", ac->system_procfs, pid);
			only_calculate_threads(dir, threads, procname, pid, match);
		}
		return 1;
	}

	if (lightweight)
	{
		if (match)
		{
			snprintf(dir, FILENAME_MAX, "%s/%s/status", ac->system_procfs, pid);
			only_calculate_threads(dir, threads, procname, pid, match);
		}
		return 1;
	}

	snprintf(dir, FILENAME_MAX, "%s/%s/maps", ac->system_procfs, pid);
	get_vmap_info(dir, pid, procname, max_map_count);

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
	get_process_extra_info(dir, procname, pid, ups, threads, shm_max);
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
	carglog(ac->system_carg, L_DEBUG, "PIDfile check %s\n", find_pid);

	string* pid = get_file_content(find_pid, 1);
	if (!pid)
		return;

	int64_t allfilesnum = 0;
	process_states *states = calloc(1, sizeof(*states));

	char pid_strict[21];
	size_t pid_size = strspn(pid->s, "0123456789") + 1;
	size_t copy_size = pid_size > 21 ? 21 : pid_size;
	strlcpy(pid_strict, pid->s, copy_size);
	
	carglog(ac->system_carg, L_DEBUG, "check PID '%s'\n", pid_strict);

	uint64_t rc = get_pid_info(pid_strict, &allfilesnum, 0, states, NULL, 0, NULL, NULL, 0, 0);
	metric_add_labels("process_match", &rc, DATATYPE_UINT, ac->system_carg, "name", find_pid);
	string_free(pid);
	free(states);
	
}

void cgroup_procs_scrape(char *cgroup_path)
{
	carglog(ac->system_carg, L_DEBUG, "Cgroup procs file check %s\n", cgroup_path);

	FILE *fd = fopen(cgroup_path, "r");
	if (!fd)
		return;

	char pid[10];
	int64_t rc = 0;
	int64_t allfilesnum = 0;
	process_states *states = calloc(1, sizeof(*states));

	while (fgets(pid, 10, fd))
	{
		char pid_strict[21];
		size_t pid_size = strspn(pid, "0123456789") + 1;
		size_t copy_size = pid_size > 21 ? 21 : pid_size;
		strlcpy(pid_strict, pid, copy_size);

		carglog(ac->system_carg, L_DEBUG, "check PID '%s' from '%s'\n", pid_strict, cgroup_path);

		rc += get_pid_info(pid_strict, &allfilesnum, 0, states, NULL, 0, NULL, NULL, 0, 0);
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

void userprocess_free_foreach(void *arg)
{
	userprocess_node *upn = arg;
	free(upn->name);
	free(upn);
}

void userprocess_free(alligator_ht* userprocess)
{
	alligator_ht_foreach(userprocess, userprocess_free_foreach);
	alligator_ht_done(userprocess);
	free(userprocess);
}

void service_user_push(alligator_ht *service_users, char *user)
{
	if (!service_users || !user)
		return;

	uint32_t hash = tommy_strhash_u32(0, user);
	system_string_node *node = alligator_ht_search(service_users, system_string_compare, user, hash);
	if (node)
		return;

	node = calloc(1, sizeof(*node));
	node->name = strdup(user);

	alligator_ht_insert(service_users, &(node->node), node, hash);
}

void service_user_del(alligator_ht *service_users, char *user)
{
	if (!service_users || !user)
		return;

	uint32_t hash = tommy_strhash_u32(0, user);
	system_string_node *node = alligator_ht_search(service_users, system_string_compare, user, hash);
	if (!node)
		return;

	alligator_ht_remove_existing(service_users, &(node->node));
	free(node->name);
	free(node);
}

void service_user_free_foreach(void *arg)
{
	system_string_node *node = arg;
	free(node->name);
	free(node);
}

void service_user_free(alligator_ht *service_users)
{
	alligator_ht_foreach(service_users, service_user_free_foreach);
	alligator_ht_done(service_users);
	free(service_users);
}

void service_user_clear(alligator_ht *service_users)
{
	if (!service_users)
		return;

	alligator_ht_clear(service_users, service_user_free_foreach);
}

void files_fd_free(void *funcarg, void* arg)
{
	fd_info *files = arg;
	if (!files)
		return;

	fd_info_summarize *sum = funcarg;

	if (!strncmp(files->key, "socket:", 7))
		++sum->sockets;
	else if (!strncmp(files->key, "pipe:", 5))
		++sum->pipes;

	++sum->files;

	free(files->key);
	free(files);
}

void find_pid(int8_t lightweight)
{
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: processes\n");


	struct dirent *entry;
	DIR *dp;

	dp = opendir(ac->system_procfs);
	if (!dp)
	{
		//perror("opendir");
		return;
	}

	process_states *states = calloc(1, sizeof(*states));
	process_states *task_states = calloc(1, sizeof(*task_states));
	int64_t allfilesnum = 0;
	uint64_t tasks = 0;
	uint64_t processes = 0;

	char shmmax[255];
	snprintf(shmmax, 255, "%s/sys/kernel/shmmax", ac->system_procfs);
	uint64_t shmem_max = getkvfile_uint(shmmax);

	char maxmapcount[255];
	snprintf(maxmapcount, 255, "%s/sys/vm/max_map_count", ac->system_procfs);
	uint64_t max_map_count = getkvfile_uint(maxmapcount);

	alligator_ht *files_open = alligator_ht_init(NULL);
	while((entry = readdir(dp)))
	{
		if ( !isdigit(entry->d_name[0]) )
			continue;

		++processes;

		get_pid_info(entry->d_name, &allfilesnum, lightweight, states, task_states, 1, files_open, &tasks, shmem_max, max_map_count);
	}

	fd_info_summarize sum = { 0 };
	alligator_ht_foreach_arg(files_open, files_fd_free, &sum);
	alligator_ht_done(files_open);
	free(files_open);
	metric_add_auto("open_files", &sum.files, DATATYPE_UINT, ac->system_carg);
	metric_add_auto("open_sockets", &sum.sockets, DATATYPE_UINT, ac->system_carg);
	metric_add_auto("open_pipes", &sum.pipes, DATATYPE_UINT, ac->system_carg);

	metric_add_labels("process_states", &states->running, DATATYPE_UINT, ac->system_carg, "state", "running");
	metric_add_labels("process_states", &states->sleeping, DATATYPE_UINT, ac->system_carg, "state", "sleeping");
	metric_add_labels("process_states", &states->uninterruptible, DATATYPE_UINT, ac->system_carg, "state", "uninterruptible");
	metric_add_labels("process_states", &states->zombie, DATATYPE_UINT, ac->system_carg, "state", "zombie");
	metric_add_labels("process_states", &states->stopped, DATATYPE_UINT, ac->system_carg, "state", "stopped");
	metric_add_labels("task_states", &task_states->running, DATATYPE_UINT, ac->system_carg, "state", "running");
	metric_add_labels("task_states", &task_states->sleeping, DATATYPE_UINT, ac->system_carg, "state", "sleeping");
	metric_add_labels("task_states", &task_states->uninterruptible, DATATYPE_UINT, ac->system_carg, "state", "uninterruptible");
	metric_add_labels("task_states", &task_states->zombie, DATATYPE_UINT, ac->system_carg, "state", "zombie");
	metric_add_labels("task_states", &task_states->stopped, DATATYPE_UINT, ac->system_carg, "state", "stopped");
	metric_add_labels("task_states", &task_states->idle, DATATYPE_UINT, ac->system_carg, "state", "idle");
	metric_add_auto("open_files_process", &allfilesnum, DATATYPE_INT, ac->system_carg);
	metric_add_auto("tasks_total", &tasks, DATATYPE_UINT, ac->system_carg);

	char threadmax[255];
	snprintf(threadmax, 255, "%s/sys/kernel/threads-max", ac->system_procfs);
	int64_t threads_max = getkvfile(threadmax);
	metric_add_auto("tasks_max", &threads_max, DATATYPE_INT, ac->system_carg);

	double threads_usage = tasks * 100.0 / threads_max;
	metric_add_auto("tasks_usage", &threads_usage, DATATYPE_DOUBLE, ac->system_carg);

	metric_add_auto("processes_total", &processes, DATATYPE_UINT, ac->system_carg);

	char pidmax[255];
	snprintf(pidmax, 255, "%s/sys/kernel/pid_max", ac->system_procfs);
	int64_t pid_max = getkvfile(pidmax);
	metric_add_auto("processes_max", &pid_max, DATATYPE_INT, ac->system_carg);

	double pids_usage = processes * 100.0 / pid_max;
	metric_add_auto("processes_usage", &pids_usage, DATATYPE_DOUBLE, ac->system_carg);

	free(states);
	free(task_states);

	closedir(dp);
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

	carglog(ac->system_carg, L_DEBUG, "counted process with name '%s' and count: %"PRIu64"\n", ms->s, ms->count);
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


void stat_userprocess_cb(uv_fs_t *req) {
	uv_stat_t st = req->statbuf;

	userprocess_node *uupn = alligator_ht_search(ac->system_userprocess, userprocess_compare, &st.st_uid, st.st_uid);
	userprocess_node *gupn = alligator_ht_search(ac->system_groupprocess, userprocess_compare, &st.st_gid, st.st_gid);
	char *pid = req->data;

	carglog(ac->system_carg, L_DEBUG, "%s: st_uid=%"u64" st_gid=%"u64"\n", pid, st.st_uid, st.st_gid);

	process_states *states = calloc(1, sizeof(*states));
	int64_t allfilesnum = 0;
	if (uupn || gupn)
		get_pid_info(pid, &allfilesnum, 0, states, NULL, 0, NULL, NULL, 0, 0);

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

#endif
