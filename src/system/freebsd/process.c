#ifdef __FreeBSD__
#include "main.h"
#include "system/freebsd/parsers.h"
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
#include <libproc.h>

extern aconf *ac;

typedef struct freebsd_fd_counts {
	int64_t files;
	int64_t sockets;
	int64_t pipes;
	int64_t vnodes;
	int64_t other;
} freebsd_fd_counts;

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

static int freebsd_get_cmdline(pid_t pid, char *buf, size_t bufsz)
{
	int mib[4];
	size_t len = bufsz;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_ARGS;
	mib[3] = pid;

	if (sysctl(mib, 4, buf, &len, NULL, 0) == -1 || len == 0) {
		buf[0] = 0;
		return 0;
	}

	for (size_t i = 0; i + 1 < len; i++) {
		if (buf[i] == '\0')
			buf[i] = ' ';
	}
	buf[bufsz - 1] = 0;
	return 1;
}

static rlim_t freebsd_get_rlimit_val(pid_t pid, int which)
{
	struct proc *p = NULL;
	rlim_t lim = 0;

	if (!proc_open(pid, P_PID, &p))
		return 0;
	if (proc_getrlimit(p, which, &lim) != 0)
		lim = 0;
	proc_close(p);
	return lim;
}

static void freebsd_count_fds(pid_t pid, freebsd_fd_counts *cnt)
{
	int fcnt = 0, fi;
	struct kinfo_file *files;

	memset(cnt, 0, sizeof(*cnt));
	files = kinfo_getfile(pid, &fcnt);
	if (!files)
		return;

	cnt->files = fcnt;
	for (fi = 0; fi < fcnt; fi++) {
		switch (files[fi].kf_type) {
		case KF_TYPE_SOCKET:
			cnt->sockets++;
			break;
		case KF_TYPE_PIPE:
			cnt->pipes++;
			break;
		case KF_TYPE_VNODE:
			cnt->vnodes++;
			break;
		default:
			cnt->other++;
			break;
		}
	}
	free(files);
}

static void freebsd_emit_process_state(const struct kinfo_proc *proc, const char *name,
	const char *pidstr, int8_t emit_per_process)
{
	int64_t val = 1, unval = 0;

	if (!emit_per_process)
		return;

	switch (proc->ki_stat) {
	case SRUN:
		metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "running");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "sleeping");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "uninterruptible");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "zombie");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "stopped");
		break;
	case SSLEEP:
	case SIDL:
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "running");
		metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "sleeping");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "uninterruptible");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "zombie");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "stopped");
		break;
	case SWAIT:
	case SLOCK:
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "running");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "sleeping");
		metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "uninterruptible");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "zombie");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "stopped");
		break;
	case SSTOP:
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "running");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "sleeping");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "uninterruptible");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "zombie");
		metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "stopped");
		break;
	case SZOMB:
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "running");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "sleeping");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "uninterruptible");
		metric_add_labels3("process_state", &val, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "zombie");
		metric_add_labels3("process_state", &unval, DATATYPE_INT, ac->system_carg, "name", name, "pid", pidstr, "type", "stopped");
		break;
	default:
		break;
	}
}

static void freebsd_emit_process_io(const struct kinfo_proc *proc, const char *name, const char *pidstr)
{
	int64_t read_syscalls = (int64_t)proc->ki_rusage.ru_inblock;
	int64_t write_syscalls = (int64_t)proc->ki_rusage.ru_oublock;
	int64_t read_bytes = read_syscalls * 512;
	int64_t write_bytes = write_syscalls * 512;

	metric_add_labels3("process_io_syscalls_total", &read_syscalls, DATATYPE_INT, ac->system_carg,
		"name", name, "op", "read", "pid", pidstr);
	metric_add_labels3("process_io_syscalls_total", &write_syscalls, DATATYPE_INT, ac->system_carg,
		"name", name, "op", "write", "pid", pidstr);
	metric_add_labels3("process_io_bytes_total", &read_bytes, DATATYPE_INT, ac->system_carg,
		"name", name, "op", "read", "pid", pidstr);
	metric_add_labels3("process_io_bytes_total", &write_bytes, DATATYPE_INT, ac->system_carg,
		"name", name, "op", "write", "pid", pidstr);
	metric_add_labels3("process_io_chars_total", &read_bytes, DATATYPE_INT, ac->system_carg,
		"name", name, "op", "read", "pid", pidstr);
	metric_add_labels3("process_io_chars_total", &write_bytes, DATATYPE_INT, ac->system_carg,
		"name", name, "op", "write", "pid", pidstr);
}

static void freebsd_emit_process_rlimits(const struct kinfo_proc *proc, const char *name,
	const char *pidstr, const freebsd_fd_counts *fds)
{
	rlim_t openfiles, stacksize, datasize, rsssize, vmemsize;
	uint64_t uopen, ustack, udata, urss, uvmem;
	double usage;

	openfiles = freebsd_get_rlimit_val(proc->ki_pid, RLIMIT_NOFILE);
	if (openfiles > 0 && openfiles != RLIM_INFINITY) {
		uopen = (uint64_t)openfiles;
		metric_add_labels3("process_rlimit", &uopen, DATATYPE_UINT, ac->system_carg,
			"type", "open_files", "name", name, "pid", pidstr);
		usage = fds->files * 1.0 / (double)openfiles;
		metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg,
			"type", "open_files", "name", name, "pid", pidstr);
	}

	stacksize = freebsd_get_rlimit_val(proc->ki_pid, RLIMIT_STACK);
	if (stacksize > 0 && stacksize != RLIM_INFINITY) {
		ustack = (uint64_t)stacksize;
		metric_add_labels3("process_rlimit", &ustack, DATATYPE_UINT, ac->system_carg,
			"type", "stack_bytes", "name", name, "pid", pidstr);
	}

	datasize = freebsd_get_rlimit_val(proc->ki_pid, RLIMIT_DATA);
	if (datasize > 0 && datasize != RLIM_INFINITY) {
		udata = (uint64_t)datasize;
		metric_add_labels3("process_rlimit", &udata, DATATYPE_UINT, ac->system_carg,
			"type", "data_bytes", "name", name, "pid", pidstr);
	}

	rsssize = freebsd_get_rlimit_val(proc->ki_pid, RLIMIT_RSS);
	if (rsssize > 0 && rsssize != RLIM_INFINITY) {
		urss = (uint64_t)rsssize;
		metric_add_labels3("process_rlimit", &urss, DATATYPE_UINT, ac->system_carg,
			"type", "rss", "name", name, "pid", pidstr);
		usage = (double)proc->ki_rssize / (double)rsssize;
		metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg,
			"type", "rss", "name", name, "pid", pidstr);
	}

#ifdef RLIMIT_VMEM
	vmemsize = freebsd_get_rlimit_val(proc->ki_pid, RLIMIT_VMEM);
#else
	vmemsize = freebsd_get_rlimit_val(proc->ki_pid, RLIMIT_AS);
#endif
	if (vmemsize > 0 && vmemsize != RLIM_INFINITY) {
		uvmem = (uint64_t)vmemsize;
		metric_add_labels3("process_rlimit", &uvmem, DATATYPE_UINT, ac->system_carg,
			"type", "vsz", "name", name, "pid", pidstr);
		usage = (double)proc->ki_size / (double)vmemsize;
		metric_add_labels3("process_rlimit_usage", &usage, DATATYPE_DOUBLE, ac->system_carg,
			"type", "vsz", "name", name, "pid", pidstr);
	}
}

static void freebsd_emit_fd_breakdown(const char *name, const char *pidstr, const freebsd_fd_counts *fds)
{
	int64_t val;

	val = fds->files;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", name, "type", "open_files", "pid", pidstr);
	val = fds->sockets;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", name, "type", "open_sockets", "pid", pidstr);
	val = fds->pipes;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", name, "type", "open_pipes", "pid", pidstr);
	val = fds->vnodes;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", name, "type", "open_vnodes", "pid", pidstr);
	val = fds->other;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", name, "type", "open_other", "pid", pidstr);
}

static void freebsd_emit_proc_metrics(const struct kinfo_proc *proc, int8_t full)
{
	char pidstr[16];
	double utime, stime, total_time;
	uint64_t uptime;
	int64_t val;
	time_t now = time(NULL);
	freebsd_fd_counts fds;

	snprintf(pidstr, sizeof(pidstr), "%d", proc->ki_pid);
	utime = (double)proc->ki_utime / 1000000.0;
	stime = (double)proc->ki_stime / 1000000.0;
	total_time = utime + stime;

	metric_add_labels3("process_cpu_seconds_total", &utime, DATATYPE_DOUBLE, ac->system_carg,
		"name", proc->ki_comm, "pid", pidstr, "mode", "user");
	metric_add_labels3("process_cpu_seconds_total", &stime, DATATYPE_DOUBLE, ac->system_carg,
		"name", proc->ki_comm, "pid", pidstr, "mode", "system");
	metric_add_labels3("process_cpu_seconds_total", &total_time, DATATYPE_DOUBLE, ac->system_carg,
		"name", proc->ki_comm, "pid", pidstr, "mode", "total");

	val = proc->ki_size;
	metric_add_labels3("process_memory", &val, DATATYPE_INT, ac->system_carg, "name", proc->ki_comm, "type", "vsz", "pid", pidstr);
	val = proc->ki_rssize;
	metric_add_labels3("process_memory", &val, DATATYPE_INT, ac->system_carg, "name", proc->ki_comm, "type", "rss", "pid", pidstr);
	val = proc->ki_cow;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", proc->ki_comm, "type", "COWfaults", "pid", pidstr);
	val = proc->ki_numthreads;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", proc->ki_comm, "type", "threads", "pid", pidstr);

	if (!full)
		return;

	uptime = (uint64_t)(now - proc->ki_start.tv_sec);
	metric_add_labels2("process_uptime", &uptime, DATATYPE_UINT, ac->system_carg, "name", proc->ki_comm, "pid", pidstr);
	freebsd_emit_process_state(proc, proc->ki_comm, pidstr, 1);
	freebsd_emit_process_io(proc, proc->ki_comm, pidstr);
	freebsd_count_fds(proc->ki_pid, &fds);
	freebsd_emit_fd_breakdown(proc->ki_comm, pidstr, &fds);
	freebsd_emit_process_rlimits(proc, proc->ki_comm, pidstr, &fds);
}

static int8_t freebsd_process_matches(const struct kinfo_proc *proc, char *cmdline, size_t cmdline_size)
{
	size_t procname_size = strlen(proc->ki_comm);

	if (!ac->process_match)
		return 0;
	if (match_mapper(ac->process_match, proc->ki_comm, procname_size, proc->ki_comm))
		return 1;
	if (cmdline_size && match_mapper(ac->process_match, cmdline, cmdline_size, proc->ki_comm))
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

void get_proc_info(int8_t lightweight)
{
	int cntp = 0, i;
	struct kinfo_proc *proc = kinfo_getallproc(&cntp);
	uint64_t running = 0, sleeping = 0, stopped = 0, zombie = 0, uninterruptible = 0;
	uint64_t tasks = 0;
	int64_t open_files_process = 0;
	uint64_t open_pipes = 0;
	int64_t threads_max = 0;
	double threads_usage = 0;
	size_t sz;
	int8_t need_match = ac->system_process ? 1 : 0;
	freebsd_fd_counts fds;

	if (need_match && !lightweight)
		clear_counts_process();

	if (!proc)
		return;

	for (i = 0; i < cntp; i++) {
		char pidstr[16];
		char cmdline[8192];
		size_t cmdline_size = 0;
		int8_t match = 1;

		tasks += proc[i].ki_numthreads;
		if (proc[i].ki_stat == SRUN)
			++running;
		else if (proc[i].ki_stat == SSLEEP || proc[i].ki_stat == SIDL)
			++sleeping;
		else if (proc[i].ki_stat == SSTOP)
			++stopped;
		else if (proc[i].ki_stat == SZOMB)
			++zombie;
		else if (proc[i].ki_stat == SWAIT || proc[i].ki_stat == SLOCK)
			++uninterruptible;

		freebsd_count_fds(proc[i].ki_pid, &fds);
		open_files_process += fds.files;
		open_pipes += fds.pipes;

		if (need_match) {
			cmdline[0] = 0;
			if (freebsd_get_cmdline(proc[i].ki_pid, cmdline, sizeof(cmdline)))
				cmdline_size = strlen(cmdline);
			match = freebsd_process_matches(&proc[i], cmdline, cmdline_size);
			if (!match)
				continue;
		}

		if (lightweight && need_match) {
			snprintf(pidstr, sizeof(pidstr), "%d", proc[i].ki_pid);
			freebsd_emit_process_state(&proc[i], proc[i].ki_comm, pidstr, 1);
			freebsd_emit_proc_metrics(&proc[i], 0);
			continue;
		}

		if (!lightweight)
			freebsd_emit_proc_metrics(&proc[i], need_match ? 1 : 0);
	}

	metric_add_labels("process_states", &running, DATATYPE_UINT, ac->system_carg, "state", "running");
	metric_add_labels("process_states", &sleeping, DATATYPE_UINT, ac->system_carg, "state", "sleeping");
	metric_add_labels("process_states", &stopped, DATATYPE_UINT, ac->system_carg, "state", "stopped");
	metric_add_labels("process_states", &zombie, DATATYPE_UINT, ac->system_carg, "state", "zombie");
	metric_add_labels("process_states", &uninterruptible, DATATYPE_UINT, ac->system_carg, "state", "uninterruptible");

	{
		uint64_t proc_count = cntp;
		uint64_t proc_max;
		size_t proc_max_sz = sizeof(proc_max);

		metric_add_auto("processes_total", &proc_count, DATATYPE_UINT, ac->system_carg);
		if (sysctlbyname("kern.maxproc", &proc_max, &proc_max_sz, NULL, 0) == 0 && proc_max > 0) {
			double proc_usage = proc_count * 100.0 / (double)proc_max;
			metric_add_auto("processes_usage", &proc_usage, DATATYPE_DOUBLE, ac->system_carg);
		}
	}

	metric_add_auto("open_files_process", &open_files_process, DATATYPE_INT, ac->system_carg);
	metric_add_auto("open_pipes", &open_pipes, DATATYPE_UINT, ac->system_carg);
	metric_add_auto("tasks_total", &tasks, DATATYPE_UINT, ac->system_carg);

	sz = sizeof(threads_max);
	if (sysctlbyname("kern.maxthread", &threads_max, &sz, NULL, 0) == 0 && threads_max > 0) {
		metric_add_auto("tasks_max", &threads_max, DATATYPE_INT, ac->system_carg);
		threads_usage = tasks * 100.0 / (double)threads_max;
		metric_add_auto("tasks_usage", &threads_usage, DATATYPE_DOUBLE, ac->system_carg);
	}

	if (cntp > 1) {
		time_t t = time(NULL);
		struct tm lt = {0};
		r_time time1 = setrtime();

		localtime_r(&t, &lt);
		time1.sec += lt.tm_gmtoff;
		{
			uint64_t uptime = time1.sec - proc[1].ki_start.tv_sec;
			metric_add_auto("system_uptime_seconds", &uptime, DATATYPE_UINT, ac->system_carg);
		}
	}

	if (need_match && !lightweight)
		fill_counts_process();

	free(proc);
}

static int freebsd_get_pid_info(pid_t pid)
{
	int cntp = 0, i;
	struct kinfo_proc *proc = kinfo_getallproc(&cntp);
	int found = 0;

	if (!proc)
		return 0;

	for (i = 0; i < cntp; i++) {
		if (proc[i].ki_pid != pid)
			continue;
		freebsd_emit_proc_metrics(&proc[i], 1);
		found = 1;
		break;
	}

	free(proc);
	return found;
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

	rc = freebsd_get_pid_info((pid_t)atoi(pid_strict));
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
		rc += freebsd_get_pid_info((pid_t)atoi(pid_strict));
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

static void userprocess_free_foreach(void *arg)
{
	userprocess_node *upn = arg;
	free(upn->name);
	free(upn);
}

void userprocess_free(alligator_ht *userprocess)
{
	if (!userprocess)
		return;
	alligator_ht_foreach(userprocess, userprocess_free_foreach);
	alligator_ht_done(userprocess);
	free(userprocess);
}

void service_user_push(alligator_ht *service_users, char *user)
{
	uint32_t hash;
	system_string_node *node;

	if (!service_users || !user)
		return;

	hash = tommy_strhash_u32(0, user);
	node = alligator_ht_search(service_users, system_string_compare, user, hash);
	if (node)
		return;

	node = calloc(1, sizeof(*node));
	if (!node)
		return;
	node->name = strdup(user);
	if (!node->name) {
		free(node);
		return;
	}

	alligator_ht_insert(service_users, &(node->node), node, hash);
}

void service_user_del(alligator_ht *service_users, char *user)
{
	uint32_t hash;
	system_string_node *node;

	if (!service_users || !user)
		return;

	hash = tommy_strhash_u32(0, user);
	node = alligator_ht_search(service_users, system_string_compare, user, hash);
	if (!node)
		return;

	alligator_ht_remove_existing(service_users, &(node->node));
	free(node->name);
	free(node);
}

static void service_user_free_foreach(void *arg)
{
	system_string_node *node = arg;
	free(node->name);
	free(node);
}

void service_user_free(alligator_ht *service_users)
{
	if (!service_users)
		return;
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

void get_userprocess_stats(void)
{
	int cntp = 0, i;
	struct kinfo_proc *proc;

	if (!ac->system_userprocess && !ac->system_groupprocess)
		return;
	if (!alligator_ht_count(ac->system_userprocess) && !alligator_ht_count(ac->system_groupprocess))
		return;

	proc = kinfo_getallproc(&cntp);
	if (!proc)
		return;

	for (i = 0; i < cntp; i++) {
		userprocess_node *uupn = alligator_ht_search(ac->system_userprocess,
			userprocess_compare, &proc[i].ki_uid, proc[i].ki_uid);
		userprocess_node *gupn = alligator_ht_search(ac->system_groupprocess,
			userprocess_compare, &proc[i].ki_rgid, proc[i].ki_rgid);

		if (uupn || gupn)
			freebsd_emit_proc_metrics(&proc[i], 1);
	}

	free(proc);
}

int freebsd_scrape_pid(pid_t pid)
{
	return freebsd_get_pid_info(pid);
}

#endif
