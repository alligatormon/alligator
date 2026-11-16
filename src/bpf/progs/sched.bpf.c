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
	__type(value, struct bpf_sched_cfg);
} sched_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, __u32);
	__type(value, struct bpf_ts_val);
} wake_ts SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, __u32);
	__type(value, struct bpf_ts_val);
} off_ts SEC(".maps");

/* Waker comm for a woken pid — never written through an off_ts value pointer. */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, __u32);
	__type(value, struct bpf_waker_val);
} last_waker SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} runq_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} runq_sum SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} off_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} off_sum SEC(".maps");

static __always_inline struct bpf_sched_cfg *cfg(void)
{
	__u32 key = 0;
	return bpf_map_lookup_elem(&sched_cfg, &key);
}

/* TASK_* low byte: INTERRUPTIBLE=1, UNINTERRUPTIBLE=2, PARKED=0x40. Exit bits
 * 0xb0 are filtered before we store; remaining stopped/traced land in other. */
static __always_inline void off_state_str(long prev_state, char *dst)
{
	__u8 st = (__u8)(prev_state & 0xff);

	if (st & 0x02)
		__builtin_memcpy(dst, "disk_sleep", 11);
	else if (st & 0x40)
		__builtin_memcpy(dst, "parked", 7);
	else if (st & 0x01)
		__builtin_memcpy(dst, "sleep", 6);
	else
		__builtin_memcpy(dst, "other", 6);
}

static __always_inline void note_wake(__u32 pid, const char *comm)
{
	struct bpf_ts_val val = {};
	struct bpf_sched_cfg *c = cfg();

	if (!c || !pid)
		return;
	if (c->runq_on && bpf_comm_ok(c->runq_comm, comm)) {
		val.ts = bpf_ktime_get_ns();
		__builtin_memcpy(val.comm, comm, 16);
		bpf_map_update_elem(&wake_ts, &pid, &val, BPF_ANY);
	}
	/* Opt-in waker: only for allowlisted PIDs that already have an off_ts
	 * start (read-only lookup — never mutate off_ts from wakeup). */
	if (c->off_on && c->waker_on && bpf_comm_ok(c->off_comm, comm) &&
	    bpf_map_lookup_elem(&off_ts, &pid)) {
		struct bpf_waker_val wv = {};

		bpf_get_current_comm(wv.waker, sizeof(wv.waker));
		bpf_map_update_elem(&last_waker, &pid, &wv, BPF_ANY);
	}
}

/* 5.14+ folds both wakeup events onto sched_wakeup_template. Older BTF
 * still has trace_event_raw_sched_wakeup and trace_event_raw_sched_wakeup_new. */
struct trace_event_raw_sched_wakeup___old {
	char comm[16];
	pid_t pid;
} __attribute__((preserve_access_index));

struct trace_event_raw_sched_wakeup_new___old {
	char comm[16];
	pid_t pid;
} __attribute__((preserve_access_index));

static __always_inline void read_wakeup(void *ctx, int is_new, __u32 *pid, char *comm)
{
	if (bpf_core_type_exists(struct trace_event_raw_sched_wakeup_template)) {
		struct trace_event_raw_sched_wakeup_template *ev = ctx;
		*pid = BPF_CORE_READ(ev, pid);
		bpf_core_read_str(comm, 16, &ev->comm);
	} else if (is_new) {
		struct trace_event_raw_sched_wakeup_new___old *ev = ctx;
		*pid = BPF_CORE_READ(ev, pid);
		bpf_core_read_str(comm, 16, &ev->comm);
	} else {
		struct trace_event_raw_sched_wakeup___old *ev = ctx;
		*pid = BPF_CORE_READ(ev, pid);
		bpf_core_read_str(comm, 16, &ev->comm);
	}
}

SEC("tracepoint/sched/sched_wakeup")
int on_wakeup(void *ctx)
{
	char comm[16] = {};
	__u32 pid = 0;

	read_wakeup(ctx, 0, &pid, comm);
	note_wake(pid, comm);
	return 0;
}

SEC("tracepoint/sched/sched_wakeup_new")
int on_wakeup_new(void *ctx)
{
	char comm[16] = {};
	__u32 pid = 0;

	read_wakeup(ctx, 1, &pid, comm);
	note_wake(pid, comm);
	return 0;
}

SEC("tracepoint/sched/sched_switch")
int on_switch(struct trace_event_raw_sched_switch *ctx)
{
	struct bpf_sched_cfg *c = cfg();
	__u32 prev = BPF_CORE_READ(ctx, prev_pid);
	__u32 next = BPF_CORE_READ(ctx, next_pid);
	long prev_state = BPF_CORE_READ(ctx, prev_state);
	char next_comm[16] = {};
	char prev_comm[16] = {};
	__u64 now, cgroup = 0;

	if (!c)
		return 0;
	now = bpf_ktime_get_ns();
	bpf_core_read_str(next_comm, sizeof(next_comm), &ctx->next_comm);
	bpf_core_read_str(prev_comm, sizeof(prev_comm), &ctx->prev_comm);
	if (c->cgroup_labels)
		cgroup = bpf_get_current_cgroup_id();

	if (c->runq_on) {
		struct bpf_ts_val *start = bpf_map_lookup_elem(&wake_ts, &next);
		if (start && now > start->ts) {
			{
				struct bpf_hist_arg ha = {};
				ha.cgroup = start->cgroup;
				ha.delta = now - start->ts;
				bpf_hist_bump(&runq_hist, &runq_sum, next_comm, NULL, &ha);
			}
			bpf_map_delete_elem(&wake_ts, &next);
		}
	}
	if (c->off_on && bpf_comm_ok(c->off_comm, prev_comm) && (prev_state & 0xff) && !(prev_state & 0xb0)) {
		struct bpf_ts_val val = {};
		val.ts = now;
		val.cgroup = cgroup;
		__builtin_memcpy(val.comm, prev_comm, 16);
		off_state_str(prev_state, val.extra);
		bpf_map_update_elem(&off_ts, &prev, &val, BPF_ANY);
	}
	if (c->off_on) {
		struct bpf_ts_val *off = bpf_map_lookup_elem(&off_ts, &next);
		if (off && now > off->ts) {
			{
				struct bpf_hist_arg ha = {};
				char waker[16] = {};
				const char *comm = off->comm[0] ? off->comm : next_comm;
				ha.cgroup = off->cgroup;
				ha.delta = now - off->ts;
				if (c->waker_on) {
					struct bpf_waker_val *wv = bpf_map_lookup_elem(&last_waker, &next);

					if (wv)
						__builtin_memcpy(waker, wv->waker, 16);
					ha.dev = waker;
				}
				bpf_hist_bump(&off_hist, &off_sum, comm, off->extra, &ha);
			}
			bpf_map_delete_elem(&off_ts, &next);
		}
	}
	/* Drop stale last_waker even when off_ts is missing (map is not LRU). */
	if (c->waker_on)
		bpf_map_delete_elem(&last_waker, &next);
	return 0;
}
