#ifdef __APPLE__
#include "main.h"
#include "system/macosx/parsers.h"
#include "common/selector.h"
#include "common/pw.h"
#include "common/logs.h"
#include "common/rtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/proc_info.h>
#include <libproc.h>

extern aconf *ac;

/* Cap libproc FD / thread list allocations (fail soft). */
#define MACOSX_MAX_FDINFO_BYTES  (4U * 1024U * 1024U)
#define MACOSX_MAX_THRINFO_BYTES (4U * 1024U * 1024U)

typedef struct macosx_fd_counts {
	int64_t files;
	int64_t sockets;
	int64_t pipes;
	int64_t vnodes;
	int64_t other;
} macosx_fd_counts;

static int sysctl_read_u64_flexible(const char *name, uint64_t *out)
{
	size_t sz = 0;

	*out = 0;
	if (sysctlbyname(name, NULL, &sz, NULL, 0) != 0 || !sz)
		return -1;
	if (sz == sizeof(uint64_t)) {
		return sysctlbyname(name, out, &sz, NULL, 0);
	}
	if (sz == sizeof(uint32_t)) {
		uint32_t v32 = 0;
		sz = sizeof(v32);
		if (sysctlbyname(name, &v32, &sz, NULL, 0) != 0)
			return -1;
		*out = v32;
		return 0;
	}
	if (sz == sizeof(int)) {
		int v = 0;
		sz = sizeof(v);
		if (sysctlbyname(name, &v, &sz, NULL, 0) != 0 || v < 0)
			return -1;
		*out = (uint64_t)v;
		return 0;
	}
	return -1;
}

int userprocess_compare(const void *arg, const void *obj)
{
	uint32_t s1 = *(uint32_t *)arg;
	uint32_t s2 = ((userprocess_node *)obj)->uid;
	return s1 != s2;
}

int system_string_compare(const void *arg, const void *obj)
{
	char *s1 = (char *)arg;
	char *s2 = ((system_string_node *)obj)->name;
	return strcmp(s1, s2);
}

static int macosx_get_cmdline(pid_t pid, char *buf, size_t bufsz)
{
	int mib[4];
	size_t len = bufsz;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROCARGS2;
	mib[2] = pid;

	if (sysctl(mib, 3, buf, &len, NULL, 0) == -1 || len < sizeof(int))
		return 0;

	{
		int nargs = *(int *)buf;
		char *p = buf + sizeof(int);
		size_t pos = 0;

		(void)nargs;
		while (*p && pos + 1 < bufsz) {
			if (*p == '\0')
				buf[pos++] = ' ';
			else
				buf[pos++] = *p;
			p++;
		}
		buf[pos] = 0;
	}
	return buf[0] != 0;
}

static void macosx_count_fds(pid_t pid, macosx_fd_counts *cnt)
{
	struct proc_fdinfo *fds;
	int fdbytes;
	int nfd;
	int i;

	memset(cnt, 0, sizeof(*cnt));
	fdbytes = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0);
	if (fdbytes <= 0)
		return;
	if ((size_t)fdbytes > MACOSX_MAX_FDINFO_BYTES)
		return;

	fds = malloc((size_t)fdbytes);
	if (!fds)
		return;

	nfd = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds, fdbytes);
	if (nfd <= 0) {
		free(fds);
		return;
	}
	nfd /= (int)sizeof(*fds);
	cnt->files = nfd;
	for (i = 0; i < nfd; i++) {
		switch (fds[i].proc_fdtype) {
		case PROX_FDTYPE_SOCKET:
			cnt->sockets++;
			break;
		case PROX_FDTYPE_PIPE:
			cnt->pipes++;
			break;
		case PROX_FDTYPE_VNODE:
			cnt->vnodes++;
			break;
		default:
			cnt->other++;
			break;
		}
	}
	free(fds);
}

static void macosx_emit_fd_breakdown(const char *name, const char *pidstr, const macosx_fd_counts *fds)
{
	int64_t val;

	val = fds->files;
	metric_add_labels3("process_open_fds", &val, DATATYPE_INT, ac->system_carg, "name", (char *)name, "type", "file", "pid", (char *)pidstr);
	val = fds->sockets;
	metric_add_labels3("process_open_fds", &val, DATATYPE_INT, ac->system_carg, "name", (char *)name, "type", "socket", "pid", (char *)pidstr);
	val = fds->pipes;
	metric_add_labels3("process_open_fds", &val, DATATYPE_INT, ac->system_carg, "name", (char *)name, "type", "pipe", "pid", (char *)pidstr);
	val = fds->vnodes;
	metric_add_labels3("process_open_fds", &val, DATATYPE_INT, ac->system_carg, "name", (char *)name, "type", "vnode", "pid", (char *)pidstr);
	val = fds->other;
	metric_add_labels3("process_open_fds", &val, DATATYPE_INT, ac->system_carg, "name", (char *)name, "type", "other", "pid", (char *)pidstr);
}

static void macosx_emit_page_faults(const struct proc_taskinfo *task, const char *name, const char *pidstr)
{
	int64_t majflt = (int64_t)task->pti_pageins;
	int64_t minflt = (int64_t)task->pti_faults - majflt;

	if (minflt < 0)
		minflt = 0;

	metric_add_labels3("process_page_faults_total", &minflt, DATATYPE_INT, ac->system_carg,
		"name", (char *)name, "pid", (char *)pidstr, "type", "minor");
	metric_add_labels3("process_page_faults_total", &majflt, DATATYPE_INT, ac->system_carg,
		"name", (char *)name, "pid", (char *)pidstr, "type", "major");
}

/* Darwin has no remote rlimit API without task_for_pid; emit only for self. */
static void macosx_emit_process_rlimits(pid_t pid, const char *name, const char *pidstr,
	const macosx_fd_counts *fds, const struct proc_taskinfo *task)
{
	struct rlimit rl;
	uint64_t uval;
	double usage;

	if (pid != getpid())
		return;

	if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur > 0 && rl.rlim_cur != RLIM_INFINITY) {
		uval = (uint64_t)rl.rlim_cur;
		metric_add_labels3("process_rlimit", &uval, DATATYPE_UINT, ac->system_carg,
			"type", "open_files", "name", (char *)name, "pid", (char *)pidstr);
		usage = fds->files * 1.0 / (double)rl.rlim_cur;
		metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg,
			"type", "open_files", "name", (char *)name, "pid", (char *)pidstr);
	}
	if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur > 0 && rl.rlim_cur != RLIM_INFINITY) {
		uval = (uint64_t)rl.rlim_cur;
		metric_add_labels3("process_rlimit", &uval, DATATYPE_UINT, ac->system_carg,
			"type", "stack_bytes", "name", (char *)name, "pid", (char *)pidstr);
	}
	if (getrlimit(RLIMIT_DATA, &rl) == 0 && rl.rlim_cur > 0 && rl.rlim_cur != RLIM_INFINITY) {
		uval = (uint64_t)rl.rlim_cur;
		metric_add_labels3("process_rlimit", &uval, DATATYPE_UINT, ac->system_carg,
			"type", "data_bytes", "name", (char *)name, "pid", (char *)pidstr);
	}
	/*
	 * On Darwin RLIMIT_RSS is an alias of RLIMIT_AS; emitting both would
	 * duplicate the same limit with a misleading RSS usage ratio. Prefer AS/vsz.
	 */
#if defined(RLIMIT_RSS) && defined(RLIMIT_AS) && (RLIMIT_RSS != RLIMIT_AS)
	if (getrlimit(RLIMIT_RSS, &rl) == 0 && rl.rlim_cur > 0 && rl.rlim_cur != RLIM_INFINITY) {
		uval = (uint64_t)rl.rlim_cur;
		metric_add_labels3("process_rlimit", &uval, DATATYPE_UINT, ac->system_carg,
			"type", "rss", "name", (char *)name, "pid", (char *)pidstr);
		usage = (double)task->pti_resident_size / (double)rl.rlim_cur;
		metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg,
			"type", "rss", "name", (char *)name, "pid", (char *)pidstr);
	}
#endif
	if (getrlimit(RLIMIT_AS, &rl) == 0 && rl.rlim_cur > 0 && rl.rlim_cur != RLIM_INFINITY) {
		uval = (uint64_t)rl.rlim_cur;
		metric_add_labels3("process_rlimit", &uval, DATATYPE_UINT, ac->system_carg,
			"type", "vsz", "name", (char *)name, "pid", (char *)pidstr);
		usage = (double)task->pti_virtual_size / (double)rl.rlim_cur;
		metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg,
			"type", "vsz", "name", (char *)name, "pid", (char *)pidstr);
	}
}

static void macosx_count_task_states(uint64_t *running, uint64_t *sleeping,
	uint64_t *stopped, uint64_t *zombie, uint64_t *uninterruptible, uint64_t *idle)
{
	int bufsize = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
	pid_t *pids;
	int index;

	*running = *sleeping = *stopped = *zombie = *uninterruptible = *idle = 0;
	if (bufsize <= 0)
		return;
	bufsize *= 2;
	pids = malloc((size_t)bufsize);
	if (!pids)
		return;
	bufsize = proc_listpids(PROC_ALL_PIDS, 0, pids, bufsize);
	if (bufsize <= 0) {
		free(pids);
		return;
	}

	for (index = 0; index < bufsize / (int)sizeof(pid_t); index++) {
		uint64_t *thrbuf = NULL;
		int thrbytes;
		int nthr;
		int t;
		struct proc_bsdinfo proc;
		pid_t pid = pids[index];

		if (pid <= 0)
			continue;

		if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE) > 0 &&
			proc.pbi_status == SZOMB) {
			(*zombie)++;
			continue;
		}

		thrbytes = proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, NULL, 0);
		if (thrbytes <= 0)
			continue;
		if ((size_t)thrbytes > MACOSX_MAX_THRINFO_BYTES)
			continue;
		thrbuf = malloc((size_t)thrbytes);
		if (!thrbuf)
			continue;
		nthr = proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, thrbuf, thrbytes);
		if (nthr <= 0) {
			free(thrbuf);
			continue;
		}
		nthr /= (int)sizeof(uint64_t);

		for (t = 0; t < nthr; t++) {
			struct proc_threadinfo pth;

			if (proc_pidinfo(pid, PROC_PIDTHREADINFO, thrbuf[t], &pth, PROC_PIDTHREADINFO_SIZE) <= 0)
				continue;

			if (pth.pth_flags & TH_FLAGS_IDLE) {
				(*idle)++;
				continue;
			}
			switch (pth.pth_run_state) {
			case TH_STATE_RUNNING:
				(*running)++;
				break;
			case TH_STATE_WAITING:
				(*sleeping)++;
				break;
			case TH_STATE_UNINTERRUPTIBLE:
				(*uninterruptible)++;
				break;
			case TH_STATE_STOPPED:
			case TH_STATE_HALTED:
				(*stopped)++;
				break;
			default:
				break;
			}
		}
		free(thrbuf);
	}
	free(pids);
}

static void macosx_emit_process_state(uint8_t status, const char *name, const char *pidstr, int8_t emit)
{
	int64_t val = 1, unval = 0;

	if (!emit)
		return;

	switch (status) {
	case SRUN:
		metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "running");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "sleeping");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "uninterruptible");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "zombie");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "stopped");
		break;
	case SSLEEP:
	case SIDL:
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "running");
		metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "sleeping");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "uninterruptible");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "zombie");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "stopped");
		break;
	case SSTOP:
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "running");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "sleeping");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "uninterruptible");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "zombie");
		metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "stopped");
		break;
	case SZOMB:
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "running");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "sleeping");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "uninterruptible");
		metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "zombie");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", (char *)name, "pid", (char *)pidstr, "type", "stopped");
		break;
	default:
		break;
	}
}

static void macosx_emit_process_io(pid_t pid, const char *name, const char *pidstr)
{
	struct rusage_info_v4 ru;
	int64_t read_bytes, write_bytes;

	if (proc_pid_rusage(pid, RUSAGE_INFO_V4, (rusage_info_t *)&ru) != 0)
		return;

	read_bytes = (int64_t)ru.ri_diskio_bytesread;
	write_bytes = (int64_t)ru.ri_diskio_byteswritten;

	metric_add_labels3("process_io_bytes_total", &read_bytes, DATATYPE_INT, ac->system_carg,
		"name", (char *)name, "op", "read", "pid", (char *)pidstr);
	metric_add_labels3("process_io_bytes_total", &write_bytes, DATATYPE_INT, ac->system_carg,
		"name", (char *)name, "op", "write", "pid", (char *)pidstr);
	metric_add_labels3("process_io_chars_total", &read_bytes, DATATYPE_INT, ac->system_carg,
		"name", (char *)name, "op", "read", "pid", (char *)pidstr);
	metric_add_labels3("process_io_chars_total", &write_bytes, DATATYPE_INT, ac->system_carg,
		"name", (char *)name, "op", "write", "pid", (char *)pidstr);
}

static void macosx_emit_proc_metrics(pid_t pid, const struct proc_bsdinfo *proc,
	const struct proc_taskinfo *task, int8_t full)
{
	char pidstr[16];
	double utime, stime, total_time;
	uint64_t uptime;
	int64_t val;
	time_t now = time(NULL);
	macosx_fd_counts fds;

	snprintf(pidstr, sizeof(pidstr), "%d", pid);
	utime = (double)task->pti_total_user / 1e9;
	stime = (double)task->pti_total_system / 1e9;
	total_time = utime + stime;

	metric_add_labels3("process_cpu_seconds_total", &utime, DATATYPE_DOUBLE, ac->system_carg,
		"name", (char *)proc->pbi_name, "pid", pidstr, "mode", "user");
	metric_add_labels3("process_cpu_seconds_total", &stime, DATATYPE_DOUBLE, ac->system_carg,
		"name", (char *)proc->pbi_name, "pid", pidstr, "mode", "system");
	metric_add_labels3("process_cpu_seconds_total", &total_time, DATATYPE_DOUBLE, ac->system_carg,
		"name", (char *)proc->pbi_name, "pid", pidstr, "mode", "total");

	val = (int64_t)task->pti_virtual_size;
	metric_add_labels3("process_memory_bytes", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "type", "vsz", "pid", pidstr);
	val = (int64_t)task->pti_resident_size;
	metric_add_labels3("process_memory_bytes", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "type", "rss", "pid", pidstr);

	val = task->pti_threadnum;
	metric_add_labels2("process_threads", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "pid", pidstr);
	val = proc->pbi_status;
	metric_add_labels2("process_status", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "pid", pidstr);
	val = task->pti_cow_faults;
	metric_add_labels2("process_cow_faults_total", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "pid", pidstr);
	val = (int64_t)(uint32_t)task->pti_csw;
	metric_add_labels3("process_context_switches_total", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "type", "total", "pid", pidstr);

	if (!full)
		return;

	uptime = (uint64_t)(now - proc->pbi_start_tvsec);
	metric_add_labels2("process_uptime_seconds", &uptime, DATATYPE_UINT, ac->system_carg, "name", (char *)proc->pbi_name, "pid", pidstr);
	macosx_emit_process_state(proc->pbi_status, (char *)proc->pbi_name, pidstr, 1);
	macosx_emit_process_io(pid, (char *)proc->pbi_name, pidstr);
	macosx_count_fds(pid, &fds);
	macosx_emit_fd_breakdown((char *)proc->pbi_name, pidstr, &fds);
	macosx_emit_process_rlimits(pid, (char *)proc->pbi_name, pidstr, &fds, task);
	macosx_emit_page_faults(task, (char *)proc->pbi_name, pidstr);
}

static int8_t macosx_process_matches(const char *comm, char *cmdline, size_t cmdline_size)
{
	size_t procname_size = strlen(comm);

	if (!ac->process_match)
		return 0;
	if (match_mapper(ac->process_match, (char *)comm, procname_size, (char *)comm))
		return 1;
	if (cmdline_size && match_mapper(ac->process_match, cmdline, cmdline_size, (char *)comm))
		return 1;
	return 0;
}

static void clear_counts_for(void *arg)
{
	match_string *ms = arg;
	ms->count = 0;
}

static void fill_counts_for(void *arg)
{
	match_string *ms = arg;
	metric_add_labels("process_match", &ms->count, DATATYPE_UINT, ac->system_carg, "name", ms->s);
}

static void clear_counts_process(void)
{
	regex_list *node;

	if (!ac->process_match)
		return;
	alligator_ht_foreach(ac->process_match->hash, clear_counts_for);
	for (node = ac->process_match->head; node; node = node->next)
		node->count = 0;
}

static void fill_counts_process(void)
{
	regex_list *node;

	if (!ac->process_match)
		return;
	alligator_ht_foreach(ac->process_match->hash, fill_counts_for);
	for (node = ac->process_match->head; node; node = node->next)
		metric_add_labels("process_match", &node->count, DATATYPE_UINT, ac->system_carg, "name", node->name);
}

int macosx_scrape_pid(pid_t pid)
{
	struct proc_taskinfo task_info;
	struct proc_bsdinfo proc;

	if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task_info, PROC_PIDTASKINFO_SIZE) < 0)
		return 0;
	if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE) < 0)
		return 0;

	macosx_emit_proc_metrics(pid, &proc, &task_info, 1);
	return 1;
}

void get_proc_info(int8_t lightweight)
{
	int bufsize = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
	pid_t *pids;
	size_t num_pids;
	int index;
	uint64_t running = 0, sleeping = 0, stopped = 0, zombie = 0, uninterruptible = 0;
	uint64_t tasks = 0;
	int64_t open_files_process = 0;
	uint64_t open_pipes = 0;
	int64_t threads_max = 0;
	double threads_usage = 0;
	int8_t need_match = ac->system_process ? 1 : 0;
	macosx_fd_counts fds;
	uint64_t proc_count = 0;
	/* Live sum of pti_csw across visible tasks (not boot-monotonic). */
	uint64_t csw_sum = 0;
	uint64_t csw_procs = 0;

	if (bufsize <= 0)
		return;
	/* Process list can grow between size probe and fill. */
	if (bufsize < (int)sizeof(pid_t) * 64)
		bufsize = (int)sizeof(pid_t) * 64;
	bufsize *= 2;

	pids = malloc((size_t)bufsize);
	if (!pids)
		return;

	bufsize = proc_listpids(PROC_ALL_PIDS, 0, pids, bufsize);
	if (bufsize <= 0) {
		free(pids);
		return;
	}
	num_pids = (size_t)bufsize / sizeof(pid_t);

	if (need_match && !lightweight)
		clear_counts_process();

	for (index = 0; index < (int)num_pids; index++) {
		struct proc_taskinfo task_info;
		struct proc_bsdinfo proc;
		pid_t pid = pids[index];
		char pidstr[16];
		char cmdline[8192];
		size_t cmdline_size = 0;
		int8_t match = 1;

		if (pid <= 0)
			continue;
		proc_count++;

		if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task_info, PROC_PIDTASKINFO_SIZE) < 0)
			continue;
		if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE) < 0)
			continue;

		/* Sum before process_match filter so the host gauge covers all visible PIDs. */
		csw_sum += (uint64_t)(uint32_t)task_info.pti_csw;
		csw_procs++;

		tasks += task_info.pti_threadnum;
		if (proc.pbi_status == SRUN)
			++running;
		else if (proc.pbi_status == SSLEEP || proc.pbi_status == SIDL)
			++sleeping;
		else if (proc.pbi_status == SSTOP)
			++stopped;
		else if (proc.pbi_status == SZOMB)
			++zombie;

		macosx_count_fds(pid, &fds);
		open_files_process += fds.files;
		open_pipes += (uint64_t)fds.pipes;

		if (need_match) {
			cmdline[0] = 0;
			if (macosx_get_cmdline(pid, cmdline, sizeof(cmdline)))
				cmdline_size = strlen(cmdline);
			match = macosx_process_matches(proc.pbi_name, cmdline, cmdline_size);
			if (!match)
				continue;
		}

		if (lightweight && need_match) {
			snprintf(pidstr, sizeof(pidstr), "%d", pid);
			macosx_emit_process_state(proc.pbi_status, proc.pbi_name, pidstr, 1);
			macosx_emit_proc_metrics(pid, &proc, &task_info, 0);
			continue;
		}

		if (!lightweight)
			macosx_emit_proc_metrics(pid, &proc, &task_info, need_match ? 1 : 0);
	}

	metric_add_labels("process_states", &running, DATATYPE_UINT, ac->system_carg, "state", "running");
	metric_add_labels("process_states", &sleeping, DATATYPE_UINT, ac->system_carg, "state", "sleeping");
	metric_add_labels("process_states", &stopped, DATATYPE_UINT, ac->system_carg, "state", "stopped");
	metric_add_labels("process_states", &zombie, DATATYPE_UINT, ac->system_carg, "state", "zombie");
	metric_add_labels("process_states", &uninterruptible, DATATYPE_UINT, ac->system_carg, "state", "uninterruptible");

	{
		uint64_t t_running = 0, t_sleeping = 0, t_stopped = 0, t_zombie = 0, t_unint = 0, t_idle = 0;

		macosx_count_task_states(&t_running, &t_sleeping, &t_stopped, &t_zombie, &t_unint, &t_idle);
		metric_add_labels("task_states", &t_running, DATATYPE_UINT, ac->system_carg, "state", "running");
		metric_add_labels("task_states", &t_sleeping, DATATYPE_UINT, ac->system_carg, "state", "sleeping");
		metric_add_labels("task_states", &t_unint, DATATYPE_UINT, ac->system_carg, "state", "uninterruptible");
		metric_add_labels("task_states", &t_zombie, DATATYPE_UINT, ac->system_carg, "state", "zombie");
		metric_add_labels("task_states", &t_stopped, DATATYPE_UINT, ac->system_carg, "state", "stopped");
		metric_add_labels("task_states", &t_idle, DATATYPE_UINT, ac->system_carg, "state", "idle");
	}

	metric_add_labels("process_context_switches", &csw_sum, DATATYPE_UINT, ac->system_carg, "type", "sum");
	metric_add_labels("process_context_switches", &csw_procs, DATATYPE_UINT, ac->system_carg, "type", "processes");

	{
		uint64_t proc_max = 0;

		metric_add_auto("processes_total", &proc_count, DATATYPE_UINT, ac->system_carg);
		if (sysctl_read_u64_flexible("kern.maxproc", &proc_max) == 0 && proc_max > 0) {
			double proc_usage = proc_count * 100.0 / (double)proc_max;
			metric_add_auto("processes_usage", &proc_usage, DATATYPE_DOUBLE, ac->system_carg);
		}
	}

	metric_add_auto("process_open_fds_sum", &open_files_process, DATATYPE_INT, ac->system_carg);
	/* Host file_sockets/max_sockets come from get_vmstat() via sysctl (FreeBSD parity). */
	metric_add_auto("file_pipes", &open_pipes, DATATYPE_UINT, ac->system_carg);
	metric_add_auto("tasks_total", &tasks, DATATYPE_UINT, ac->system_carg);

	{
		uint64_t tmax = 0;

		if (sysctl_read_u64_flexible("kern.maxthread", &tmax) == 0 && tmax > 0) {
			threads_max = (int64_t)tmax;
			metric_add_auto("tasks_max", &threads_max, DATATYPE_INT, ac->system_carg);
			threads_usage = tasks * 100.0 / (double)threads_max;
			metric_add_auto("tasks_usage", &threads_usage, DATATYPE_DOUBLE, ac->system_carg);
		}
	}

	if (need_match && !lightweight)
		fill_counts_process();

	free(pids);
}

void pidfile_push(char *file, int type)
{
	pidfile_node *node;

	if (!ac->system_pidfile || !file)
		return;

	node = calloc(1, sizeof(*node));
	if (!node)
		return;

	if (!ac->system_pidfile->head)
		ac->system_pidfile->head = ac->system_pidfile->tail = node;
	else {
		ac->system_pidfile->tail->next = node;
		ac->system_pidfile->tail = node;
	}

	node->pidfile = strdup(file);
	node->type = type;
}

void pidfile_del(char *file, int type)
{
	pidfile_node *prev = NULL;
	pidfile_node *node;

	(void)type;

	if (!ac->system_pidfile || !file)
		return;

	node = ac->system_pidfile->head;
	while (node) {
		pidfile_node *next = node->next;

		if (node->pidfile && !strcmp(node->pidfile, file)) {
			if (ac->system_pidfile->head == node)
				ac->system_pidfile->head = next;
			if (ac->system_pidfile->tail == node)
				ac->system_pidfile->tail = prev;
			if (prev)
				prev->next = next;
			free(node->pidfile);
			free(node);
		} else {
			prev = node;
		}
		node = next;
	}
}

static void simple_pidfile_scrape(char *find_pid)
{
	string *pid;
	char pid_strict[21];
	size_t pid_size, copy_size;
	uint64_t rc;

	carglog(ac->system_carg, L_DEBUG, "PIDfile check %s\n", find_pid);

	pid = get_file_content(find_pid, 1);
	if (!pid)
		return;

	pid_size = strspn(pid->s, "0123456789") + 1;
	copy_size = pid_size > sizeof(pid_strict) ? sizeof(pid_strict) : pid_size;
	strlcpy(pid_strict, pid->s, copy_size);

	rc = macosx_scrape_pid((pid_t)atoi(pid_strict));
	metric_add_labels("process_match", &rc, DATATYPE_UINT, ac->system_carg, "name", find_pid);
	string_free(pid);
}

static void pid_list_scrape(char *path)
{
	FILE *fd;
	char pid[32];
	uint64_t rc = 0;

	carglog(ac->system_carg, L_DEBUG, "PID list file check %s\n", path);

	fd = fopen(path, "r");
	if (!fd)
		return;

	while (fgets(pid, sizeof(pid), fd)) {
		char pid_strict[21];
		size_t pid_size = strspn(pid, "0123456789") + 1;
		size_t copy_size = pid_size > sizeof(pid_strict) ? sizeof(pid_strict) : pid_size;

		strlcpy(pid_strict, pid, copy_size);
		rc += macosx_scrape_pid((pid_t)atoi(pid_strict));
	}

	metric_add_labels("process_match", &rc, DATATYPE_UINT, ac->system_carg, "name", path);
	fclose(fd);
}

void get_pidfile_stats(void)
{
	pidfile_node *node;

	if (!ac->system_pidfile)
		return;

	for (node = ac->system_pidfile->head; node; node = node->next) {
		if (node->type == 0)
			simple_pidfile_scrape(node->pidfile);
		else
			pid_list_scrape(node->pidfile);
	}
}

void userprocess_push(alligator_ht *userprocess, char *user)
{
	uid_t uid;
	userprocess_node *upn;

	if (!userprocess || !user)
		return;

	if (userprocess == ac->system_groupprocess)
		uid = get_gid_by_groupname(user);
	else
		uid = get_uid_by_username(user);
	upn = alligator_ht_search(userprocess, userprocess_compare, &uid, uid);
	if (upn)
		return;

	upn = calloc(1, sizeof(*upn));
	if (!upn)
		return;
	upn->uid = uid;
	upn->name = strdup(user);
	if (!upn->name) {
		free(upn);
		return;
	}

	alligator_ht_insert(userprocess, &(upn->node), upn, upn->uid);
}

void userprocess_del(alligator_ht *userprocess, char *user)
{
	uid_t uid;
	userprocess_node *upn;

	if (!userprocess || !user)
		return;

	if (userprocess == ac->system_groupprocess)
		uid = get_gid_by_groupname(user);
	else
		uid = get_uid_by_username(user);
	upn = alligator_ht_search(userprocess, userprocess_compare, &uid, uid);
	if (!upn)
		return;

	alligator_ht_remove_existing(userprocess, &(upn->node));
	free(upn->name);
	free(upn);
}

void get_userprocess_stats(void)
{
	int bufsize = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
	pid_t *pids;
	int index;

	if (!ac->system_userprocess && !ac->system_groupprocess)
		return;
	if (!alligator_ht_count(ac->system_userprocess) && !alligator_ht_count(ac->system_groupprocess))
		return;
	if (bufsize <= 0)
		return;

	pids = malloc((size_t)bufsize);
	if (!pids)
		return;

	bufsize = proc_listpids(PROC_ALL_PIDS, 0, pids, bufsize);
	for (index = 0; index < bufsize / (int)sizeof(pid_t); index++) {
		struct proc_taskinfo task_info;
		struct proc_bsdinfo proc;
		pid_t pid = pids[index];
		userprocess_node *uupn;
		userprocess_node *gupn;

		if (pid <= 0)
			continue;
		if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task_info, PROC_PIDTASKINFO_SIZE) < 0)
			continue;
		if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE) < 0)
			continue;

		uupn = alligator_ht_search(ac->system_userprocess, userprocess_compare, &proc.pbi_uid, proc.pbi_uid);
		gupn = alligator_ht_search(ac->system_groupprocess, userprocess_compare, &proc.pbi_gid, proc.pbi_gid);
		if (uupn || gupn)
			macosx_emit_proc_metrics(pid, &proc, &task_info, 1);
	}

	free(pids);
}
#endif
