#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_types.h"
#include "map_help.h"

char LICENSE[] SEC("license") = "GPL";

/* Kernel LCB_F_* from lockdep (stable bit layout since 5.17). Prefer one
 * primary type string to keep cardinality small. */
#define LCB_F_SPIN	(1U << 0)
#define LCB_F_READ	(1U << 1)
#define LCB_F_WRITE	(1U << 2)
#define LCB_F_RT	(1U << 3)
#define LCB_F_PERCPU	(1U << 4)
#define LCB_F_MUTEX	(1U << 5)

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
	__type(value, struct bpf_lock_start);
} lock_start SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} lock_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} lock_sum SEC(".maps");

static __always_inline void lock_type_str(__u32 flags, char *dst)
{
	if (flags & LCB_F_MUTEX)
		__builtin_memcpy(dst, "mutex", 6);
	else if (flags & LCB_F_SPIN)
		__builtin_memcpy(dst, "spin", 5);
	else if (flags & (LCB_F_READ | LCB_F_WRITE))
		__builtin_memcpy(dst, "rwlock", 7);
	else if (flags & LCB_F_RT)
		__builtin_memcpy(dst, "rt", 3);
	else if (flags & LCB_F_PERCPU)
		__builtin_memcpy(dst, "percpu", 7);
	else
		__builtin_memcpy(dst, "other", 6);
}

SEC("tracepoint/lock/contention_begin")
int lock_begin(struct trace_event_raw_contention_begin *ctx)
{
	__u64 tid = bpf_get_current_pid_tgid();
	struct bpf_lock_start start = {};
	struct bpf_lock_cfg *c;
	char comm[16] = {};
	__u32 z = 0;

	(void)BPF_CORE_READ(ctx, lock_addr);
	c = bpf_map_lookup_elem(&lock_cfg, &z);
	bpf_get_current_comm(comm, sizeof(comm));
	if (c && !bpf_comm_ok(c->comm, comm))
		return 0;
	start.ts = bpf_ktime_get_ns();
	start.flags = BPF_CORE_READ(ctx, flags);
	bpf_map_update_elem(&lock_start, &tid, &start, BPF_ANY);
	return 0;
}

SEC("tracepoint/lock/contention_end")
int lock_end(struct trace_event_raw_contention_end *ctx)
{
	__u64 tid = bpf_get_current_pid_tgid();
	struct bpf_lock_start *start;
	__u64 now;
	char comm[16] = {};
	char type[32] = {};
	__u64 cgroup = 0;

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
		if (c && c->cgroup_labels)
			cgroup = bpf_get_current_cgroup_id();
	}
	lock_type_str(start->flags, type);
	if (now > start->ts) {
		struct bpf_hist_arg ha = {};
		ha.cgroup = cgroup;
		ha.delta = now - start->ts;
		bpf_hist_bump(&lock_hist, &lock_sum, comm, type, &ha);
	}
	bpf_map_delete_elem(&lock_start, &tid);
	return 0;
}
