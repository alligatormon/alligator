#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_types.h"
#include "map_help.h"

char LICENSE[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_lock_cfg);
} lock_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, __u64);
	__type(value, __u64);
} lock_start SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 1024);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} lock_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 1024);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} lock_sum SEC(".maps");

SEC("tracepoint/lock/contention_begin")
int lock_begin(struct trace_event_raw_contention_begin *ctx)
{
	__u64 tid = bpf_get_current_pid_tgid();
	__u64 ts = bpf_ktime_get_ns();
	struct bpf_lock_cfg *c;
	char comm[16] = {};
	__u32 z = 0;

	(void)BPF_CORE_READ(ctx, lock_addr);
	c = bpf_map_lookup_elem(&lock_cfg, &z);
	bpf_get_current_comm(comm, sizeof(comm));
	if (c && !bpf_comm_ok(c->comm, comm))
		return 0;
	bpf_map_update_elem(&lock_start, &tid, &ts, BPF_ANY);
	return 0;
}

SEC("tracepoint/lock/contention_end")
int lock_end(struct trace_event_raw_contention_end *ctx)
{
	__u64 tid = bpf_get_current_pid_tgid();
	__u64 *start;
	__u64 now;
	char comm[16] = {};

	(void)BPF_CORE_READ(ctx, lock_addr);
	start = bpf_map_lookup_elem(&lock_start, &tid);
	if (!start)
		return 0;
	now = bpf_ktime_get_ns();
	bpf_get_current_comm(comm, sizeof(comm));
	{
		struct bpf_lock_cfg *c;
		__u32 z = 0;
		c = bpf_map_lookup_elem(&lock_cfg, &z);
		if (c && !bpf_comm_ok(c->comm, comm)) {
			bpf_map_delete_elem(&lock_start, &tid);
			return 0;
		}
	}
	if (now > *start)
		{
			struct bpf_hist_arg ha = {};
			ha.delta = now - *start;
			bpf_hist_bump(&lock_hist, &lock_sum, comm, NULL, &ha);
		}
	bpf_map_delete_elem(&lock_start, &tid);
	return 0;
}
