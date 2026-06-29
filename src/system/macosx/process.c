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
#include <libproc.h>

extern aconf *ac;

typedef struct macosx_fd_counts {
	int64_t files;
	int64_t pipes;
} macosx_fd_counts;

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
	int fcnt;

	memset(cnt, 0, sizeof(*cnt));
	fcnt = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0);
	if (fcnt <= 0)
		return;
	cnt->files = fcnt;
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
	metric_add_labels3("process_memory", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "type", "vsz", "pid", pidstr);
	val = (int64_t)task->pti_resident_size;
	metric_add_labels3("process_memory", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "type", "rss", "pid", pidstr);

	val = task->pti_threadnum;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "type", "threads", "pid", pidstr);
	val = proc->pbi_status;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "type", "status", "pid", pidstr);
	val = proc->pbi_nfiles;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "type", "open_files", "pid", pidstr);
	val = task->pti_cow_faults;
	metric_add_labels3("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", (char *)proc->pbi_name, "type", "COWfaults", "pid", pidstr);

	if (!full)
		return;

	uptime = (uint64_t)(now - proc->pbi_start_tvsec);
	metric_add_labels2("process_uptime", &uptime, DATATYPE_UINT, ac->system_carg, "name", (char *)proc->pbi_name, "pid", pidstr);
	macosx_emit_process_state(proc->pbi_status, (char *)proc->pbi_name, pidstr, 1);
	macosx_emit_process_io(pid, (char *)proc->pbi_name, pidstr);
	macosx_count_fds(pid, &fds);
	macosx_emit_process_io(pid, (char *)proc->pbi_name, pidstr);
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
	size_t sz;
	int8_t need_match = ac->system_process ? 1 : 0;
	macosx_fd_counts fds;
	uint64_t proc_count = 0;

	if (bufsize <= 0)
		return;

	pids = malloc((size_t)bufsize);
	if (!pids)
		return;

	bufsize = proc_listpids(PROC_ALL_PIDS, 0, pids, bufsize);
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
