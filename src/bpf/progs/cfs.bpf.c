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
	__type(value, struct bpf_cfs_cfg);
} cfs_cfg SEC(".maps");

/*
 * Start time for an active throttle interval, keyed by cfs_rq*.
 * Uses bpf_ktime_get_ns() on both throttle and unthrottle so the delta does
 * not depend on rq->clock (which can lag rq_clock() at fentry of unthrottle).
 */
struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, __u64);
	__type(value, struct bpf_ts_val);
} cfs_throttle_ts SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} cfs_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} cfs_sum SEC(".maps");

static __always_inline struct bpf_cfs_cfg *cfg(void)
{
	__u32 key = 0;
	return bpf_map_lookup_elem(&cfs_cfg, &key);
}

SEC("fentry/throttle_cfs_rq")
int BPF_PROG(throttle_cfs_rq, struct cfs_rq *cfs_rq)
{
	struct bpf_cfs_cfg *c = cfg();
	struct bpf_ts_val val = {};
	__u64 key;

	if (!c || !c->on || !cfs_rq)
		return 0;
	key = (__u64)cfs_rq;
	val.ts = bpf_ktime_get_ns();
	if (c->cgroup_labels)
		val.cgroup = BPF_CORE_READ(cfs_rq, tg->css.cgroup->kn->id);
	bpf_map_update_elem(&cfs_throttle_ts, &key, &val, BPF_ANY);
	return 0;
}

SEC("fentry/unthrottle_cfs_rq")
int BPF_PROG(unthrottle_cfs_rq, struct cfs_rq *cfs_rq)
{
	struct bpf_cfs_cfg *c = cfg();
	struct bpf_ts_val *start;
	__u64 key;
	__u64 now;
	char empty[16] = {};
	struct bpf_hist_arg ha = {};

	if (!c || !c->on || !cfs_rq)
		return 0;
	key = (__u64)cfs_rq;
	start = bpf_map_lookup_elem(&cfs_throttle_ts, &key);
	if (!start)
		return 0;
	now = bpf_ktime_get_ns();
	if (now > start->ts) {
		ha.cgroup = start->cgroup;
		ha.delta = now - start->ts;
		bpf_hist_bump(&cfs_hist, &cfs_sum, empty, NULL, &ha);
	}
	bpf_map_delete_elem(&cfs_throttle_ts, &key);
	return 0;
}
