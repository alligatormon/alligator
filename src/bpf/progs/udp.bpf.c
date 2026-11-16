#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_types.h"
#include "map_help.h"

char LICENSE[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_udp_cfg);
} udp_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} udp_ctr SEC(".maps");

static __noinline void bump_udp(const char *event)
{
	struct bpf_udp_cfg *c;
	__u32 z = 0;
	struct bpf_ctr_key key = {};
	__u64 *v;
	__u64 one = 1;
	char comm[16] = {};

	c = bpf_map_lookup_elem(&udp_cfg, &z);
	if (!c || !c->on || !event)
		return;
	bpf_get_current_comm(comm, sizeof(comm));
	if (!bpf_comm_ok(c->comm, comm))
		return;
	__builtin_memcpy(key.comm, comm, 16);
	bpf_copy_str(key.extra, event, 64);
	if (c->cgroup_labels)
		key.cgroup = bpf_get_current_cgroup_id();
	v = bpf_map_lookup_elem(&udp_ctr, &key);
	if (v)
		__sync_fetch_and_add(v, 1);
	else
		bpf_map_update_elem(&udp_ctr, &key, &one, BPF_ANY);
}

SEC("kprobe")
int udp_send(struct pt_regs *ctx)
{
	(void)ctx;
	bump_udp("send");
	return 0;
}

/* Count only successful deliveries; entry would include -EAGAIN polls. */
SEC("kretprobe")
int udp_recv(struct pt_regs *ctx)
{
	long ret = PT_REGS_RC(ctx);

	if (ret <= 0)
		return 0;
	bump_udp("recv");
	return 0;
}
