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
	__type(value, struct bpf_dns_cfg);
} dns_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, __u64);
	__type(value, struct bpf_ts_val);
} dns_start SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} dns_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} dns_sum SEC(".maps");

SEC("uprobe")
int dns_entry(struct pt_regs *ctx)
{
	struct bpf_dns_cfg *c;
	__u32 z = 0;
	__u64 tid;
	struct bpf_ts_val val = {};
	const char *node;
	char name[64] = {};

	c = bpf_map_lookup_elem(&dns_cfg, &z);
	if (!c || !c->on)
		return 0;
	{
		char comm[16] = {};
		bpf_get_current_comm(comm, sizeof(comm));
		if (!bpf_comm_ok(c->comm, comm))
			return 0;
	}
	tid = bpf_get_current_pid_tgid();
	val.ts = bpf_ktime_get_ns();
	if (c->cgroup_labels)
		val.cgroup = bpf_get_current_cgroup_id();
	node = (const char *)PT_REGS_PARM1(ctx);
	if (c->filter_op && node && bpf_probe_read_user_str(name, sizeof(name), node) > 1) {
		int ok = 0;
		if (c->filter_op == 1 || c->filter_op == 4)
			ok = c->value[0] && bpf_str_eq(name, c->value);
		else if (c->filter_op == 2)
			ok = bpf_str_prefix(name, c->value);
		else if (c->filter_op == 3)
			ok = bpf_str_suffix(name, c->value);
		if (ok) {
			__builtin_memcpy(val.extra, name, 31);
			val.extra[31] = 0;
		}
	}
	bpf_map_update_elem(&dns_start, &tid, &val, BPF_ANY);
	return 0;
}

SEC("uretprobe")
int dns_ret(struct pt_regs *ctx)
{
	struct bpf_dns_cfg *c;
	__u32 z = 0;
	__u64 tid;
	struct bpf_ts_val *start;
	__u64 now;
	char extra[32] = {};
	char comm[16] = {};

	(void)ctx;
	c = bpf_map_lookup_elem(&dns_cfg, &z);
	if (!c || !c->on)
		return 0;
	tid = bpf_get_current_pid_tgid();
	start = bpf_map_lookup_elem(&dns_start, &tid);
	if (!start)
		return 0;
	now = bpf_ktime_get_ns();
	if (c->filter_op && start->extra[0])
		__builtin_memcpy(extra, start->extra, 32);
	bpf_get_current_comm(comm, sizeof(comm));
	if (!bpf_comm_ok(c->comm, comm)) {
		bpf_map_delete_elem(&dns_start, &tid);
		return 0;
	}
	if (now > start->ts)
		{
			struct bpf_hist_arg ha = {};
			ha.cgroup = start->cgroup;
			ha.delta = now - start->ts;
			bpf_hist_bump(&dns_hist, &dns_sum, comm, extra[0] ? extra : NULL, &ha);
		}
	bpf_map_delete_elem(&dns_start, &tid);
	return 0;
}
