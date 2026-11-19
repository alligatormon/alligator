#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_types.h"
#include "map_help.h"

char LICENSE[] SEC("license") = "GPL";

#define BPF_NR_SOFTIRQS 10

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_softirq_cfg);
} softirq_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, BPF_NR_SOFTIRQS);
	__type(key, __u32);
	__type(value, __u64);
} softirq_raise_ts SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, BPF_NR_SOFTIRQS);
	__type(key, __u32);
	__type(value, __u64);
} softirq_entry_ts SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} entry_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} entry_sum SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} service_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} service_sum SEC(".maps");

static __always_inline struct bpf_softirq_cfg *cfg(void)
{
	__u32 key = 0;
	return bpf_map_lookup_elem(&softirq_cfg, &key);
}

static __always_inline void kind_name(__u32 vec, char *dst)
{
	switch (vec) {
	case 0:
		__builtin_memcpy(dst, "hi", 3);
		break;
	case 1:
		__builtin_memcpy(dst, "timer", 6);
		break;
	case 2:
		__builtin_memcpy(dst, "net_tx", 7);
		break;
	case 3:
		__builtin_memcpy(dst, "net_rx", 7);
		break;
	case 4:
		__builtin_memcpy(dst, "block", 6);
		break;
	case 5:
		__builtin_memcpy(dst, "irq_poll", 9);
		break;
	case 6:
		__builtin_memcpy(dst, "tasklet", 8);
		break;
	case 7:
		__builtin_memcpy(dst, "sched", 6);
		break;
	case 8:
		__builtin_memcpy(dst, "hrtimer", 8);
		break;
	case 9:
		__builtin_memcpy(dst, "rcu", 4);
		break;
	default:
		__builtin_memcpy(dst, "other", 6);
		break;
	}
}

SEC("tp_btf/softirq_raise")
int BPF_PROG(softirq_raise, unsigned int vec_nr)
{
	struct bpf_softirq_cfg *c = cfg();
	__u32 key = vec_nr;
	__u64 *ts;
	__u64 now;

	if (!c || !c->on || vec_nr >= BPF_NR_SOFTIRQS)
		return 0;
	ts = bpf_map_lookup_elem(&softirq_raise_ts, &key);
	if (!ts)
		return 0;
	/* Keep the oldest pending raise (Cloudflare softirq-latency). */
	if (!*ts) {
		now = bpf_ktime_get_ns();
		*ts = now;
	}
	return 0;
}

SEC("tp_btf/softirq_entry")
int BPF_PROG(softirq_entry, unsigned int vec_nr)
{
	struct bpf_softirq_cfg *c = cfg();
	__u32 key = vec_nr;
	__u64 *raise_ts;
	__u64 *entry_ts;
	__u64 now;
	char kind[32] = {};
	char empty[16] = {};

	if (!c || !c->on || vec_nr >= BPF_NR_SOFTIRQS)
		return 0;
	now = bpf_ktime_get_ns();
	raise_ts = bpf_map_lookup_elem(&softirq_raise_ts, &key);
	if (!raise_ts || !*raise_ts || *raise_ts > now)
		return 0;
	kind_name(vec_nr, kind);
	{
		struct bpf_hist_arg ha = {};
		ha.delta = now - *raise_ts;
		bpf_hist_bump(&entry_hist, &entry_sum, empty, kind, &ha);
	}
	*raise_ts = 0;
	entry_ts = bpf_map_lookup_elem(&softirq_entry_ts, &key);
	if (entry_ts)
		*entry_ts = now;
	return 0;
}

SEC("tp_btf/softirq_exit")
int BPF_PROG(softirq_exit, unsigned int vec_nr)
{
	struct bpf_softirq_cfg *c = cfg();
	__u32 key = vec_nr;
	__u64 *entry_ts;
	__u64 now;
	char kind[32] = {};
	char empty[16] = {};

	if (!c || !c->on || vec_nr >= BPF_NR_SOFTIRQS)
		return 0;
	now = bpf_ktime_get_ns();
	entry_ts = bpf_map_lookup_elem(&softirq_entry_ts, &key);
	if (!entry_ts || !*entry_ts || *entry_ts > now)
		return 0;
	kind_name(vec_nr, kind);
	{
		struct bpf_hist_arg ha = {};
		ha.delta = now - *entry_ts;
		bpf_hist_bump(&service_hist, &service_sum, empty, kind, &ha);
	}
	*entry_ts = 0;
	return 0;
}
