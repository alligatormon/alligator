#ifdef __linux__

#include "cadvisor/metrics.h"
#include "cadvisor/parsers.h"
#include "main.h"
#include "common/logs.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

extern aconf *ac;

#ifndef PERF_FLAG_PID_CGROUP
#define PERF_FLAG_PID_CGROUP (1UL << 2)
#endif
#ifndef PERF_FLAG_FD_CLOEXEC
#define PERF_FLAG_FD_CLOEXEC (1UL << 3)
#endif

#define CADVISOR_PERF_MAX_CGROUPS 128
#define CADVISOR_UNCORE_EVENTS_PER_PMU 8

typedef struct {
	const char *name;
	uint32_t type;
	uint64_t config;
} cadvisor_perf_def;

static const cadvisor_perf_def cadvisor_perf_defs[] = {
	{ "cpu_clock", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK },
	{ "task_clock", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK },
	{ "context_switches", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES },
	{ "page_faults", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS },
	{ "cpu_cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES },
	{ "instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS },
	{ "cache_misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES },
};

#define CADVISOR_PERF_EVENT_COUNT (sizeof(cadvisor_perf_defs) / sizeof(cadvisor_perf_defs[0]))

typedef struct {
	int fd;
	int cpu;
	int event_idx;
} cadvisor_core_fd;

typedef struct {
	char *id;
	char *dir;
	char *mon_group;
	cadvisor_core_fd *fds;
	size_t nfds;
	int core_tried;
	time_t seen;
} cadvisor_hw_group;

typedef struct {
	int fd;
	char pmu[64];
	char event[64];
	char socket[16];
} cadvisor_uncore_fd;

static cadvisor_hw_group *hw_groups;
static size_t hw_ngroups;
static cadvisor_uncore_fd *uncore_fds;
static size_t uncore_nfds;
static int uncore_ready;
static int perf_cgroup_logged;
static int resctrl_logged;
static int perf_cap_logged;
static time_t hw_last_gc;

static long cadvisor_perf_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static int cadvisor_cpu_online(int cpu)
{
	char path[128];
	char buf[16];
	FILE *fd;

	snprintf(path, sizeof(path), "%s/devices/system/cpu/cpu%d/online", ac->system_sysfs, cpu);
	fd = fopen(path, "r");
	if (!fd)
		return 1;
	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return 0;
	}
	fclose(fd);
	return buf[0] != '0';
}

static int cadvisor_is_root_id(const char *id)
{
	return !id || !id[0] || (id[0] == '/' && id[1] == '\0');
}

static void cadvisor_join_cgroup(char *out, size_t outsz, const char *root, const char *id)
{
	if (cadvisor_is_root_id(id))
		snprintf(out, outsz, "%s", root);
	else if (id[0] == '/')
		snprintf(out, outsz, "%s%s", root, id);
	else
		snprintf(out, outsz, "%s/%s", root, id);
}

static int cadvisor_cgroup_fs(const char *id, char *out, size_t outsz)
{
	char controllers[512];
	char root[512];
	struct stat st;

	snprintf(controllers, sizeof(controllers), "%s/fs/cgroup/cgroup.controllers", ac->system_sysfs);
	if (access(controllers, R_OK) == 0)
		snprintf(root, sizeof(root), "%s/fs/cgroup", ac->system_sysfs);
	else
		snprintf(root, sizeof(root), "%s/fs/cgroup/perf_event", ac->system_sysfs);
	cadvisor_join_cgroup(out, outsz, root, id);
	if (stat(out, &st) == 0 && S_ISDIR(st.st_mode))
		return 1;

	snprintf(root, sizeof(root), "%s/fs/cgroup", ac->system_sysfs);
	cadvisor_join_cgroup(out, outsz, root, id);
	if (stat(out, &st) == 0 && S_ISDIR(st.st_mode))
		return 1;
	return 0;
}

static void cadvisor_emit_scaled(int fd, char *total_name, char *ratio_name,
	char *cntid, char *name, char *image, char *cad_id,
	char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id,
	const cadvisor_label_pair *extra, size_t nextra)
{
	struct {
		uint64_t value;
		uint64_t time_enabled;
		uint64_t time_running;
	} data;
	ssize_t n;
	double ratio = 1.0;
	uint64_t scaled;

	memset(&data, 0, sizeof(data));
	n = read(fd, &data, sizeof(data));
	if (n != (ssize_t)sizeof(data))
		return;
	scaled = data.value;
	if (data.time_running && data.time_enabled) {
		ratio = (double)data.time_running / (double)data.time_enabled;
		if (ratio != 0.0)
			scaled = (uint64_t)((double)data.value / ratio);
	}
	add_cadvisor_metric_labels_uint(total_name, scaled, cntid, name, image, cad_id,
		kubenamespace, kubepod, kubecontainer, libvirt_id, extra, nextra);
	add_cadvisor_metric_labels_double(ratio_name, ratio, cntid, name, image, cad_id,
		kubenamespace, kubepod, kubecontainer, libvirt_id, extra, nextra);
}

static void cadvisor_core_close(cadvisor_hw_group *g)
{
	size_t i;

	if (!g->fds)
		return;
	for (i = 0; i < g->nfds; ++i) {
		if (g->fds[i].fd >= 0)
			close(g->fds[i].fd);
	}
	free(g->fds);
	g->fds = NULL;
	g->nfds = 0;
}

static void cadvisor_rm_rf(const char *path)
{
	DIR *dp;
	struct dirent *ent;
	char child[1100];
	struct stat st;

	if (!path || !strstr(path, "/mon_groups/cadvisor-"))
		return;
	dp = opendir(path);
	if (!dp) {
		rmdir(path);
		return;
	}
	while ((ent = readdir(dp))) {
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;
		snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
		if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
			cadvisor_rm_rf(child);
		else
			unlink(child);
	}
	closedir(dp);
	rmdir(path);
}

static void cadvisor_group_free(cadvisor_hw_group *g)
{
	cadvisor_core_close(g);
	if (g->mon_group)
		cadvisor_rm_rf(g->mon_group);
	free(g->id);
	free(g->dir);
	free(g->mon_group);
	g->id = NULL;
	g->dir = NULL;
	g->mon_group = NULL;
}

static void cadvisor_hw_gc(time_t now)
{
	size_t i;
	struct stat st;

	if (hw_last_gc && now - hw_last_gc < 60)
		return;
	hw_last_gc = now;
	i = 0;
	while (i < hw_ngroups) {
		if ((now - hw_groups[i].seen) > 120 || (hw_groups[i].dir && stat(hw_groups[i].dir, &st) != 0)) {
			cadvisor_group_free(&hw_groups[i]);
			if (i + 1 < hw_ngroups)
				hw_groups[i] = hw_groups[hw_ngroups - 1];
			hw_ngroups--;
			continue;
		}
		++i;
	}
}

static cadvisor_hw_group *cadvisor_group_get(const char *id, const char *dir, time_t now)
{
	size_t i;
	cadvisor_hw_group *n;

	for (i = 0; i < hw_ngroups; ++i) {
		if (hw_groups[i].id && !strcmp(hw_groups[i].id, id)) {
			hw_groups[i].seen = now;
			return &hw_groups[i];
		}
	}
	if (hw_ngroups >= CADVISOR_PERF_MAX_CGROUPS) {
		if (!perf_cap_logged && ac->cadvisor_carg) {
			carglog(ac->cadvisor_carg, L_DEBUG, "cadvisor perf: cgroup cache full (%d)\n", CADVISOR_PERF_MAX_CGROUPS);
			perf_cap_logged = 1;
		}
		return NULL;
	}
	n = realloc(hw_groups, (hw_ngroups + 1) * sizeof(*hw_groups));
	if (!n)
		return NULL;
	hw_groups = n;
	memset(&hw_groups[hw_ngroups], 0, sizeof(hw_groups[hw_ngroups]));
	hw_groups[hw_ngroups].id = strdup(id);
	hw_groups[hw_ngroups].dir = strdup(dir);
	hw_groups[hw_ngroups].seen = now;
	if (!hw_groups[hw_ngroups].id || !hw_groups[hw_ngroups].dir) {
		free(hw_groups[hw_ngroups].id);
		free(hw_groups[hw_ngroups].dir);
		return NULL;
	}
	hw_ngroups++;
	return &hw_groups[hw_ngroups - 1];
}

static int cadvisor_core_push(cadvisor_hw_group *g, int fd, int cpu, int event_idx)
{
	cadvisor_core_fd *n;

	n = realloc(g->fds, (g->nfds + 1) * sizeof(*g->fds));
	if (!n)
		return -1;
	g->fds = n;
	g->fds[g->nfds].fd = fd;
	g->fds[g->nfds].cpu = cpu;
	g->fds[g->nfds].event_idx = event_idx;
	g->nfds++;
	return 0;
}

static void cadvisor_core_open(cadvisor_hw_group *g)
{
	int cgroup_fd;
	long ncpu;
	int cpu;
	size_t e;

	g->core_tried = 1;
	cgroup_fd = open(g->dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (cgroup_fd < 0) {
		if (!perf_cgroup_logged && ac->cadvisor_carg) {
			carglog(ac->cadvisor_carg, L_DEBUG, "cadvisor perf: open %s: %s\n", g->dir, strerror(errno));
			perf_cgroup_logged = 1;
		}
		return;
	}
	ncpu = sysconf(_SC_NPROCESSORS_CONF);
	if (ncpu <= 0) {
		close(cgroup_fd);
		return;
	}
	for (cpu = 0; cpu < (int)ncpu; ++cpu) {
		if (!cadvisor_cpu_online(cpu))
			continue;
		for (e = 0; e < CADVISOR_PERF_EVENT_COUNT; ++e) {
			struct perf_event_attr attr;
			int fd;

			memset(&attr, 0, sizeof(attr));
			attr.type = cadvisor_perf_defs[e].type;
			attr.size = sizeof(attr);
			attr.config = cadvisor_perf_defs[e].config;
			attr.disabled = 0;
			attr.exclude_kernel = 0;
			attr.exclude_hv = 0;
			attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
			fd = (int)cadvisor_perf_open(&attr, cgroup_fd, cpu, -1, PERF_FLAG_FD_CLOEXEC | PERF_FLAG_PID_CGROUP);
			if (fd < 0)
				continue;
			if (cadvisor_core_push(g, fd, cpu, (int)e) < 0) {
				close(fd);
				close(cgroup_fd);
				return;
			}
		}
	}
	close(cgroup_fd);
}

static void cadvisor_core_read(cadvisor_hw_group *g, char *cntid, char *name, char *image, char *cad_id,
	char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	size_t i;

	for (i = 0; i < g->nfds; ++i) {
		char cpu_lbl[16];
		cadvisor_label_pair extra[2];

		snprintf(cpu_lbl, sizeof(cpu_lbl), "%d", g->fds[i].cpu);
		extra[0].name = "cpu";
		extra[0].value = cpu_lbl;
		extra[1].name = "event";
		extra[1].value = cadvisor_perf_defs[g->fds[i].event_idx].name;
		cadvisor_emit_scaled(g->fds[i].fd,
			"container_perf_events_total",
			"container_perf_events_scaling_ratio",
			cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id,
			extra, 2);
	}
}

static int cadvisor_first_cpu(const char *cpumask)
{
	char *end = NULL;
	long cpu;

	if (!cpumask)
		return 0;
	while (*cpumask && (*cpumask < '0' || *cpumask > '9'))
		++cpumask;
	if (!*cpumask)
		return 0;
	cpu = strtol(cpumask, &end, 10);
	if (end == cpumask || cpu < 0)
		return 0;
	return (int)cpu;
}

static void cadvisor_package_id(int cpu, char *out, size_t outsz)
{
	char path[256];
	char buf[32];
	FILE *fd;

	snprintf(path, sizeof(path), "%s/devices/system/cpu/cpu%d/topology/physical_package_id", ac->system_sysfs, cpu);
	fd = fopen(path, "r");
	if (!fd || !fgets(buf, sizeof(buf), fd)) {
		if (fd)
			fclose(fd);
		snprintf(out, outsz, "0");
		return;
	}
	fclose(fd);
	buf[strcspn(buf, "\r\n")] = '\0';
	snprintf(out, outsz, "%s", buf[0] ? buf : "0");
}

static int cadvisor_uncore_push(int fd, const char *pmu, const char *event, const char *socket)
{
	cadvisor_uncore_fd *n;

	n = realloc(uncore_fds, (uncore_nfds + 1) * sizeof(*uncore_fds));
	if (!n)
		return -1;
	uncore_fds = n;
	uncore_fds[uncore_nfds].fd = fd;
	snprintf(uncore_fds[uncore_nfds].pmu, sizeof(uncore_fds[uncore_nfds].pmu), "%s", pmu);
	snprintf(uncore_fds[uncore_nfds].event, sizeof(uncore_fds[uncore_nfds].event), "%s", event);
	snprintf(uncore_fds[uncore_nfds].socket, sizeof(uncore_fds[uncore_nfds].socket), "%s", socket);
	uncore_nfds++;
	return 0;
}

static void cadvisor_uncore_open_pmu(const char *devices, const char *pmu)
{
	char path[1100];
	char typebuf[64];
	char maskbuf[256];
	FILE *fd;
	DIR *ev;
	struct dirent *ent;
	uint32_t type;
	int cpu;
	int opened = 0;
	char socket[16];

	snprintf(path, sizeof(path), "%s/%s/type", devices, pmu);
	fd = fopen(path, "r");
	if (!fd || !fgets(typebuf, sizeof(typebuf), fd)) {
		if (fd)
			fclose(fd);
		return;
	}
	fclose(fd);
	type = (uint32_t)strtoul(typebuf, NULL, 0);

	snprintf(path, sizeof(path), "%s/%s/cpumask", devices, pmu);
	fd = fopen(path, "r");
	if (!fd || !fgets(maskbuf, sizeof(maskbuf), fd)) {
		if (fd)
			fclose(fd);
		return;
	}
	fclose(fd);
	cpu = cadvisor_first_cpu(maskbuf);
	cadvisor_package_id(cpu, socket, sizeof(socket));

	snprintf(path, sizeof(path), "%s/%s/events", devices, pmu);
	ev = opendir(path);
	if (!ev)
		return;
	while ((ent = readdir(ev))) {
		char body[512];
		uint64_t config;
		struct perf_event_attr attr;
		int pfd;
		FILE *ef;

		if (ent->d_name[0] == '.')
			continue;
		if (opened >= CADVISOR_UNCORE_EVENTS_PER_PMU)
			break;
		snprintf(path, sizeof(path), "%s/%s/events/%s", devices, pmu, ent->d_name);
		ef = fopen(path, "r");
		if (!ef)
			continue;
		if (!fgets(body, sizeof(body), ef)) {
			fclose(ef);
			continue;
		}
		fclose(ef);
		if (!cadvisor_parse_perf_event_config(body, &config))
			continue;
		memset(&attr, 0, sizeof(attr));
		attr.type = type;
		attr.size = sizeof(attr);
		attr.config = config;
		attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
		pfd = (int)cadvisor_perf_open(&attr, -1, cpu, -1, PERF_FLAG_FD_CLOEXEC);
		if (pfd < 0)
			continue;
		if (cadvisor_uncore_push(pfd, pmu, ent->d_name, socket) < 0) {
			close(pfd);
			break;
		}
		opened++;
	}
	closedir(ev);
}

static void cadvisor_uncore_close(void)
{
	size_t i;

	for (i = 0; i < uncore_nfds; ++i) {
		if (uncore_fds[i].fd >= 0)
			close(uncore_fds[i].fd);
	}
	free(uncore_fds);
	uncore_fds = NULL;
	uncore_nfds = 0;
	uncore_ready = 0;
}

static void cadvisor_uncore_setup(void)
{
	char devices[512];
	DIR *dp;
	struct dirent *ent;

	if (uncore_ready)
		return;
	uncore_ready = 1;
	snprintf(devices, sizeof(devices), "%s/devices", ac->system_sysfs);
	dp = opendir(devices);
	if (!dp)
		return;
	while ((ent = readdir(dp))) {
		if (strncmp(ent->d_name, "uncore", 6))
			continue;
		cadvisor_uncore_open_pmu(devices, ent->d_name);
	}
	closedir(dp);
}

static void cadvisor_uncore_read(char *cntid, char *name, char *image, char *cad_id,
	char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	size_t i;

	cadvisor_uncore_setup();
	for (i = 0; i < uncore_nfds; ++i) {
		cadvisor_label_pair extra[3];

		extra[0].name = "socket";
		extra[0].value = uncore_fds[i].socket;
		extra[1].name = "event";
		extra[1].value = uncore_fds[i].event;
		extra[2].name = "pmu";
		extra[2].value = uncore_fds[i].pmu;
		cadvisor_emit_scaled(uncore_fds[i].fd,
			"container_perf_uncore_events_total",
			"container_perf_uncore_events_scaling_ratio",
			cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id,
			extra, 3);
	}
}

static void cadvisor_mon_name(const char *id, char *out, size_t outsz)
{
	size_t i = 0;
	size_t o;
	const char *p = id;

	snprintf(out, outsz, "cadvisor-");
	o = strlen(out);
	if (p && *p == '/')
		++p;
	if (!p)
		p = "";
	while (p[i] && o + 1 < outsz && o < 200) {
		unsigned char c = (unsigned char)p[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
			out[o++] = (char)c;
		else
			out[o++] = '-';
		++i;
	}
	out[o] = '\0';
	if (!out[9])
		snprintf(out, outsz, "cadvisor-root");
}

static FILE *cadvisor_open_procs(const char *id)
{
	char path[1100];
	FILE *fd;
	const char *roots[] = { "fs/cgroup", "fs/cgroup/pids", "fs/cgroup/cpu,cpuacct" };
	size_t i;

	for (i = 0; i < 3; ++i) {
		if (id && id[0] == '/')
			snprintf(path, sizeof(path), "%s/%s%s/cgroup.procs", ac->system_sysfs, roots[i], id);
		else
			snprintf(path, sizeof(path), "%s/%s/%s/cgroup.procs", ac->system_sysfs, roots[i], id ? id : "");
		fd = fopen(path, "r");
		if (fd)
			return fd;
	}
	return NULL;
}

static void cadvisor_write_tid(const char *tasks, int tid)
{
	int fd;
	char buf[32];
	int n;

	fd = open(tasks, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	n = snprintf(buf, sizeof(buf), "%d\n", tid);
	if (n > 0)
		(void)write(fd, buf, (size_t)n);
	close(fd);
}

static void cadvisor_assign_threads(const char *id, const char *tasks)
{
	FILE *procs;
	char buf[64];
	const char *procfs;

	procs = cadvisor_open_procs(id);
	if (!procs)
		return;
	procfs = ac->system_procfs ? ac->system_procfs : "/proc";
	while (fgets(buf, sizeof(buf), procs)) {
		char taskdir[256];
		DIR *dp;
		struct dirent *ent;
		int pid = atoi(buf);

		if (pid <= 0)
			continue;
		snprintf(taskdir, sizeof(taskdir), "%s/%d/task", procfs, pid);
		dp = opendir(taskdir);
		if (!dp) {
			cadvisor_write_tid(tasks, pid);
			continue;
		}
		while ((ent = readdir(dp))) {
			int tid;
			if (ent->d_name[0] == '.')
				continue;
			tid = atoi(ent->d_name);
			if (tid > 0)
				cadvisor_write_tid(tasks, tid);
		}
		closedir(dp);
	}
	fclose(procs);
}

static void cadvisor_node_label(const char *dirname, char *out, size_t outsz)
{
	const char *p = dirname;

	if (!strncmp(p, "mon_L3_", 7))
		p += 7;
	while (*p == '0' && p[1])
		++p;
	if (!*p)
		p = "0";
	snprintf(out, outsz, "%s", p);
}

static void cadvisor_read_mbm(const char *mon_data, char *cntid, char *name, char *image, char *cad_id,
	char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	DIR *dp;
	struct dirent *ent;

	dp = opendir(mon_data);
	if (!dp) {
		if (!resctrl_logged && ac->cadvisor_carg) {
			carglog(ac->cadvisor_carg, L_DEBUG, "cadvisor resctrl: %s: %s\n", mon_data, strerror(errno));
			resctrl_logged = 1;
		}
		return;
	}
	while ((ent = readdir(dp))) {
		char path[1100];
		char node[32];
		FILE *fd;
		char buf[64];
		uint64_t val;
		cadvisor_label_pair extra;

		if (strncmp(ent->d_name, "mon_L3_", 7))
			continue;
		cadvisor_node_label(ent->d_name, node, sizeof(node));
		extra.name = "node";
		extra.value = node;

		snprintf(path, sizeof(path), "%s/%s/mbm_total_bytes", mon_data, ent->d_name);
		fd = fopen(path, "r");
		if (fd && fgets(buf, sizeof(buf), fd)) {
			val = strtoull(buf, NULL, 10);
			add_cadvisor_metric_labels_uint("container_memory_bandwidth_bytes", val, cntid, name, image, cad_id,
				kubenamespace, kubepod, kubecontainer, libvirt_id, &extra, 1);
		}
		if (fd)
			fclose(fd);

		snprintf(path, sizeof(path), "%s/%s/mbm_local_bytes", mon_data, ent->d_name);
		fd = fopen(path, "r");
		if (fd && fgets(buf, sizeof(buf), fd)) {
			val = strtoull(buf, NULL, 10);
			add_cadvisor_metric_labels_uint("container_memory_bandwidth_local_bytes", val, cntid, name, image, cad_id,
				kubenamespace, kubepod, kubecontainer, libvirt_id, &extra, 1);
		}
		if (fd)
			fclose(fd);
	}
	closedir(dp);
}

static void cadvisor_bandwidth(cadvisor_hw_group *g, const char *id, char *cntid, char *name, char *image, char *cad_id,
	char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char resctrl[512];
	char mon[800];
	char tasks[1100];
	char mon_data[1100];
	struct stat st;

	snprintf(resctrl, sizeof(resctrl), "%s/fs/resctrl", ac->system_sysfs);
	if (stat(resctrl, &st) != 0 || !S_ISDIR(st.st_mode))
		return;

	if (cadvisor_is_root_id(id)) {
		snprintf(mon_data, sizeof(mon_data), "%s/mon_data", resctrl);
		cadvisor_read_mbm(mon_data, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		return;
	}

	if (!g->mon_group) {
		char gname[220];
		char groups[640];

		snprintf(groups, sizeof(groups), "%s/mon_groups", resctrl);
		if (stat(groups, &st) != 0 || !S_ISDIR(st.st_mode))
			return;
		cadvisor_mon_name(id, gname, sizeof(gname));
		snprintf(mon, sizeof(mon), "%s/%s", groups, gname);
		if (mkdir(mon, 0755) != 0 && errno != EEXIST)
			return;
		g->mon_group = strdup(mon);
		if (!g->mon_group)
			return;
	}
	snprintf(tasks, sizeof(tasks), "%s/tasks", g->mon_group);
	cadvisor_assign_threads(id, tasks);
	snprintf(mon_data, sizeof(mon_data), "%s/mon_data", g->mon_group);
	cadvisor_read_mbm(mon_data, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

void cadvisor_hw_scrape(char *cgroup_path, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char dir[1024];
	char *id;
	time_t now;
	cadvisor_hw_group *g;

	if (!ac || !ac->cadvisor_carg || !ac->system_sysfs)
		return;
	id = cgroup_path && cgroup_path[0] ? cgroup_path : "/";
	if (!cadvisor_cgroup_fs(id, dir, sizeof(dir)))
		return;
	now = time(NULL);
	cadvisor_hw_gc(now);
	g = cadvisor_group_get(id, dir, now);
	if (!g)
		return;
	if (ac->cadvisor_perf_events) {
		if (!g->core_tried)
			cadvisor_core_open(g);
		cadvisor_core_read(g, cntid, name, image, id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		if (cadvisor_is_root_id(id))
			cadvisor_uncore_read(cntid, name, image, id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	} else {
		if (g->nfds) {
			cadvisor_core_close(g);
			g->core_tried = 0;
		}
		if (uncore_nfds)
			cadvisor_uncore_close();
	}
	cadvisor_bandwidth(g, id, cntid, name, image, id, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

#else

#include "cadvisor/metrics.h"

void cadvisor_hw_scrape(char *cgroup_path, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	(void)cgroup_path;
	(void)cntid;
	(void)name;
	(void)image;
	(void)kubenamespace;
	(void)kubepod;
	(void)kubecontainer;
	(void)libvirt_id;
}

#endif
