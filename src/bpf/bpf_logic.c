#include "bpf/bpf.h"
#include <stdio.h>
#include <string.h>

int bpf_filter_match(int op, const char *rule, const char *text)
{
	size_t rl, tl;

	if (op == BPF_FILTER_NONE)
		return 1;
	if (!text)
		text = "";
	if (!rule)
		rule = "";
	if (op == BPF_FILTER_EQ || op == BPF_FILTER_ALLOW)
		return strcmp(text, rule) == 0 && rule[0];
	if (op == BPF_FILTER_PREFIX) {
		rl = strlen(rule);
		if (!rl)
			return 0;
		return strncmp(text, rule, rl) == 0;
	}
	if (op == BPF_FILTER_SUFFIX) {
		rl = strlen(rule);
		tl = strlen(text);
		if (!rl || tl < rl)
			return 0;
		return strcmp(text + tl - rl, rule) == 0;
	}
	return 0;
}

int bpf_filter_allow(const char *const *list, size_t n, const char *text)
{
	size_t i;

	if (!text)
		text = "";
	for (i = 0; i < n; i++) {
		if (list[i] && list[i][0] && !strcmp(list[i], text))
			return 1;
	}
	return 0;
}

size_t bpf_str_copy(char *dst, size_t cap, const char *src, int *truncated)
{
	size_t i;

	if (truncated)
		*truncated = 0;
	if (!dst || !cap)
		return 0;
	if (!src) {
		dst[0] = 0;
		return 0;
	}
	for (i = 0; i + 1 < cap && src[i]; i++)
		dst[i] = src[i];
	dst[i] = 0;
	if (src[i]) {
		if (truncated)
			*truncated = 1;
	}
	return i;
}

int bpf_hist_bucket(uint64_t ns)
{
	int i;

	for (i = 0; i < BPF_HIST_LAST; i++) {
		uint64_t lim = (1ULL << i) * 1000ULL;
		if (ns < lim)
			return i;
	}
	return BPF_HIST_LAST;
}

double bpf_hist_le(int bucket)
{
	if (bucket < 0 || bucket >= BPF_HIST_LAST)
		return 0;
	return (double)(1ULL << bucket) * 1e-6;
}

int bpf_lru_note(uint32_t *len, uint32_t cap, uint64_t *dropped, int is_new)
{
	if (!len || !dropped)
		return 0;
	if (!is_new)
		return 1;
	if (*len >= cap) {
		(*dropped)++;
		return 0;
	}
	(*len)++;
	return 1;
}

int bpf_kernel_at_least(const char *release, int maj, int min)
{
	int major = 0, minor = 0;

	if (!release)
		return 0;
	if (sscanf(release, "%d.%d", &major, &minor) < 2)
		return 0;
	if (major > maj)
		return 1;
	if (major < maj)
		return 0;
	return minor >= min;
}

struct sys_row {
	const char *name;
	int x86_64;
	int aarch64;
};

static const struct sys_row sys_rows[] = {
	{ "read", 0, 63 },
	{ "write", 1, 64 },
	{ "close", 3, 57 },
	{ "poll", 7, 73 },
	{ "connect", 42, 203 },
	{ "accept", 43, 202 },
	{ "execve", 59, 221 },
	{ "setuid", 105, 146 },
	{ "futex", 202, 98 },
	{ "openat", 257, 56 },
	{ "accept4", 288, 242 },
};

int bpf_syscall_nr(const char *name, const char *machine)
{
	size_t i;
	int arm;

	if (!name || !machine)
		return -1;
	arm = !strcmp(machine, "aarch64") || !strcmp(machine, "arm64");
	if (!arm && strcmp(machine, "x86_64"))
		return -1;
	for (i = 0; i < sizeof(sys_rows) / sizeof(sys_rows[0]); i++) {
		if (!strcmp(sys_rows[i].name, name))
			return arm ? sys_rows[i].aarch64 : sys_rows[i].x86_64;
	}
	return -1;
}

const char *bpf_syscall_name(int nr, const char *machine)
{
	size_t i;
	int arm;

	if (!machine)
		return NULL;
	arm = !strcmp(machine, "aarch64") || !strcmp(machine, "arm64");
	if (!arm && strcmp(machine, "x86_64"))
		return NULL;
	for (i = 0; i < sizeof(sys_rows) / sizeof(sys_rows[0]); i++) {
		int row = arm ? sys_rows[i].aarch64 : sys_rows[i].x86_64;
		if (row == nr)
			return sys_rows[i].name;
	}
	return NULL;
}

int bpf_path_label(const char *label)
{
	if (!label)
		return 0;
	return !strcmp(label, "path") || !strcmp(label, "filename") || !strcmp(label, "file") || !strcmp(label, "uid");
}

int bpf_event_label_ok(const char *label, int op, const char *rule, const char *text,
	const char *const *allow, size_t nallow)
{
	if (bpf_path_label(label) && op == BPF_FILTER_NONE && !nallow)
		return 0;
	if (op == BPF_FILTER_NONE)
		return 1;
	if (op == BPF_FILTER_ALLOW)
		return bpf_filter_allow(allow, nallow, text);
	return bpf_filter_match(op, rule, text);
}

static int copy_token(char *dst, size_t cap, const char *s, size_t n)
{
	if (!dst || !cap)
		return 0;
	if (n >= cap)
		n = cap - 1;
	memcpy(dst, s, n);
	dst[n] = 0;
	return dst[0] != 0;
}

int bpf_cgroup_parse(const char *path, char *pod, size_t pod_n, char *ns, size_t ns_n,
	char *container, size_t container_n)
{
	const char *p;
	const char *slash;
	const char *base;
	const char *dot;
	size_t n;

	if (pod && pod_n)
		pod[0] = 0;
	if (ns && ns_n)
		ns[0] = 0;
	if (container && container_n)
		container[0] = 0;
	if (!path || !path[0])
		return 0;

	for (p = path; (p = strstr(p, "pod")); p += 3) {
		const char *s;
		const char *e;
		if (p == path || (p[-1] != '-' && p[-1] != '/'))
			continue;
		s = p + 3;
		if (!*s)
			continue;
		e = s;
		while (*e && *e != '.' && *e != '/')
			e++;
		if (e == s)
			continue;
		copy_token(pod, pod_n, s, (size_t)(e - s));
		break;
	}

	p = strstr(path, "namespace-");
	if (!p)
		p = strstr(path, "namespace_");
	if (p) {
		const char *s = p + strlen("namespace-");
		const char *e = s;
		while (*e && *e != '.' && *e != '/')
			e++;
		copy_token(ns, ns_n, s, (size_t)(e - s));
	}

	slash = strrchr(path, '/');
	base = slash ? slash + 1 : path;
	if (!strcmp(base, "") )
		return pod && pod[0];
	dot = strstr(base, ".scope");
	n = dot ? (size_t)(dot - base) : strlen(base);
	if (n >= 4 && !strncmp(base + n - 4, ".scope", 6))
		n -= 0;
	copy_token(container, container_n, base, n);
	return 1;
}

struct named_row {
	const char *name;
	int id;
	const char *metric;
	int hist;
};

static const struct named_row named_rows[] = {
	{ "runqueue_latency", BPF_NAMED_RUNQUEUE, "bpf_runqueue_latency_seconds", 1 },
	{ "offcpu", BPF_NAMED_OFFCPU, "bpf_offcpu_seconds", 1 },
	{ "sched_latency", BPF_NAMED_SCHED, "bpf_runqueue_latency_seconds", 1 },
	{ "block_io", BPF_NAMED_BLOCK, "bpf_block_io_latency_seconds", 1 },
	{ "lock_contention", BPF_NAMED_LOCK, "bpf_lock_contention_seconds", 1 },
	{ "tcp_retransmit", BPF_NAMED_TCP_RETRANS, "bpf_tcp_retransmits_total", 0 },
	{ "tcp_connect_latency", BPF_NAMED_TCP_CONNECT, "bpf_tcp_connect_latency_seconds", 1 },
	{ "tcp_rtt", BPF_NAMED_TCP_RTT, "bpf_tcp_rtt_seconds", 1 },
	{ "tcp_reset", BPF_NAMED_TCP_RESET, "bpf_tcp_resets_total", 0 },
	{ "dns_latency", BPF_NAMED_DNS, "bpf_dns_latency_seconds", 1 },
	{ "syscall_latency", BPF_NAMED_SYSCALL, "bpf_syscall_latency_seconds", 1 },
	{ "process_lifecycle", BPF_NAMED_LIFE, "bpf_process_events_total", 0 },
	{ "fd_track", BPF_NAMED_FD, "bpf_process_fds", 0 },
	{ "process_io", BPF_NAMED_IO, "bpf_process_io_bytes_total", 0 },
	{ "exec", BPF_NAMED_EXEC, "bpf_exec_total", 0 },
	{ "file_access", BPF_NAMED_FILE, "bpf_file_access_total", 0 },
	{ "net_activity", BPF_NAMED_NET, "bpf_net_activity_total", 0 },
	{ "priv_change", BPF_NAMED_PRIV, "bpf_priv_change_total", 0 },
	{ "cgroup_memory", BPF_NAMED_CGROUP_MEM, "bpf_cgroup_memory_pressure_waiting_seconds_total", 0 },
};

int bpf_named_id(const char *name)
{
	size_t i;

	if (!name)
		return 0;
	for (i = 0; i < sizeof(named_rows) / sizeof(named_rows[0]); i++) {
		if (!strcmp(named_rows[i].name, name))
			return named_rows[i].id;
	}
	return 0;
}

const char *bpf_named_metric(int named)
{
	size_t i;

	for (i = 0; i < sizeof(named_rows) / sizeof(named_rows[0]); i++) {
		if (named_rows[i].id == named)
			return named_rows[i].metric;
	}
	return NULL;
}

int bpf_named_is_hist(int named)
{
	size_t i;

	for (i = 0; i < sizeof(named_rows) / sizeof(named_rows[0]); i++) {
		if (named_rows[i].id == named)
			return named_rows[i].hist;
	}
	return 0;
}
