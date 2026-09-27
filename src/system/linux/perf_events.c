#ifdef __linux__

#include "system/linux/perf_events.h"
#include "main.h"
#include "common/logs.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

extern aconf *ac;

typedef struct {
	const char *name;
	uint32_t type;
	uint64_t config;
} perf_event_def;

static const perf_event_def perf_defs[] = {
	{ "cpu_clock", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK },
	{ "task_clock", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK },
	{ "context_switches", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES },
	{ "page_faults", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS },
	{ "cpu_cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES },
	{ "instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS },
	{ "cache_misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES },
};

#define PERF_EVENT_COUNT (sizeof(perf_defs) / sizeof(perf_defs[0]))

typedef struct {
	int fd;
	int cpu;
	int event_idx;
} perf_fd_entry;

static perf_fd_entry *perf_fds;
static size_t perf_fds_len;
static size_t perf_fds_cap;

static long perf_event_open_wrap(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static void perf_close_all(void)
{
	size_t i;
	if (!perf_fds)
		return;
	for (i = 0; i < perf_fds_len; ++i) {
		if (perf_fds[i].fd >= 0)
			close(perf_fds[i].fd);
	}
	free(perf_fds);
	perf_fds = NULL;
	perf_fds_len = 0;
	perf_fds_cap = 0;
}

static int perf_push_fd(int fd, int cpu, int event_idx)
{
	if (perf_fds_len == perf_fds_cap) {
		size_t ncap = perf_fds_cap ? perf_fds_cap * 2 : 32;
		perf_fd_entry *n = realloc(perf_fds, ncap * sizeof(*n));
		if (!n)
			return -1;
		perf_fds = n;
		perf_fds_cap = ncap;
	}
	perf_fds[perf_fds_len].fd = fd;
	perf_fds[perf_fds_len].cpu = cpu;
	perf_fds[perf_fds_len].event_idx = event_idx;
	perf_fds_len++;
	return 0;
}

static int perf_cpu_online(int cpu)
{
	char path[128];
	char buf[16];
	FILE *fd;
	snprintf(path, sizeof(path), "%s/devices/system/cpu/cpu%d/online", ac->system_sysfs, cpu);
	fd = fopen(path, "r");
	if (!fd)
		return 1; /* cpu0 often has no online file */
	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return 0;
	}
	fclose(fd);
	return buf[0] != '0';
}

static void perf_open_all(void)
{
	long ncpu;
	int cpu;
	size_t e;

	perf_close_all();
	ncpu = sysconf(_SC_NPROCESSORS_CONF);
	if (ncpu <= 0)
		return;

	for (cpu = 0; cpu < (int)ncpu; ++cpu) {
		if (!perf_cpu_online(cpu))
			continue;
		for (e = 0; e < PERF_EVENT_COUNT; ++e) {
			struct perf_event_attr attr;
			int fd;
			memset(&attr, 0, sizeof(attr));
			attr.type = perf_defs[e].type;
			attr.size = sizeof(attr);
			attr.config = perf_defs[e].config;
			attr.disabled = 0;
			attr.exclude_kernel = 0;
			attr.exclude_hv = 0;
			attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
			fd = (int)perf_event_open_wrap(&attr, -1, cpu, -1, 0);
			if (fd < 0) {
				if (errno == ENOENT || errno == EOPNOTSUPP || errno == EACCES)
					continue;
				carglog(ac->system_carg, L_DEBUG, "perf_event_open %s cpu%d: %s\n",
					perf_defs[e].name, cpu, strerror(errno));
				continue;
			}
			if (perf_push_fd(fd, cpu, (int)e) < 0) {
				close(fd);
				return;
			}
		}
	}
}

void get_perf_events_stats(void)
{
	size_t i;
	int need_reopen = 0;

	if (!ac || !ac->system_carg)
		return;

	if (!perf_fds_len)
		need_reopen = 1;
	else {
		for (i = 0; i < perf_fds_len; ++i) {
			if (!perf_cpu_online(perf_fds[i].cpu)) {
				need_reopen = 1;
				break;
			}
		}
	}
	if (need_reopen)
		perf_open_all();

	for (i = 0; i < perf_fds_len; ++i) {
		struct {
			uint64_t value;
			uint64_t time_enabled;
			uint64_t time_running;
		} data;
		ssize_t n;
		char cpu_lbl[16];
		const char *ev;
		double ratio;

		memset(&data, 0, sizeof(data));
		n = read(perf_fds[i].fd, &data, sizeof(data));
		if (n != (ssize_t)sizeof(data))
			continue;

		ev = perf_defs[perf_fds[i].event_idx].name;
		snprintf(cpu_lbl, sizeof(cpu_lbl), "%d", perf_fds[i].cpu);
		metric_add_labels2("perf_events_total", &data.value, DATATYPE_UINT, ac->system_carg,
			"event", (char *)ev, "cpu", cpu_lbl);

		if (data.time_enabled == 0)
			continue;
		ratio = (double)data.time_running / (double)data.time_enabled;
		metric_add_labels2("perf_events_scaling_ratio", &ratio, DATATYPE_DOUBLE, ac->system_carg,
			"event", (char *)ev, "cpu", cpu_lbl);
	}
}

void perf_events_cleanup(void)
{
	perf_close_all();
}

#else

void get_perf_events_stats(void) {}
void perf_events_cleanup(void) {}

#endif
