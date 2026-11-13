#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include "bpf_types.h"
#include "map_help.h"

char LICENSE[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_tcp_cfg);
} tcp_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, __u64);
	__type(value, struct bpf_ts_val);
} syn_ts SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} retrans_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} reset_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} connect_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} connect_sum SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} rtt_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} rtt_sum SEC(".maps");

static __always_inline void bump_ctr(void *map, __u16 dport, __u32 dport_on, __u64 cgroup,
	const char allow[BPF_COMM_MAX][16])
{
	struct bpf_ctr_key key = {};
	__u64 *v;
	__u64 one = 1;
	char comm[16] = {};

	bpf_get_current_comm(comm, sizeof(comm));
	if (!bpf_comm_ok(allow, comm))
		return;
	__builtin_memcpy(key.comm, comm, 16);
	key.cgroup = cgroup;
	if (dport_on) {
		key.dport = dport;
		key.changed = 1;
	}
	v = bpf_map_lookup_elem(map, &key);
	if (v)
		__sync_fetch_and_add(v, 1);
	else
		bpf_map_update_elem(map, &key, &one, BPF_ANY);
}

SEC("tracepoint/tcp/tcp_retransmit_skb")
int tcp_retrans(struct trace_event_raw_tcp_event_sk_skb *ctx)
{
	struct bpf_tcp_cfg *c;
	__u32 z = 0;
	__u64 cg = 0;

	c = bpf_map_lookup_elem(&tcp_cfg, &z);
	if (!c || !c->retrans_on)
		return 0;
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	bump_ctr(&retrans_ctr, BPF_CORE_READ(ctx, dport), c->dport_on, cg, c->retrans_comm);
	return 0;
}

SEC("tracepoint/sock/inet_sock_set_state")
int inet_state(struct trace_event_raw_inet_sock_set_state *ctx)
{
	struct bpf_tcp_cfg *c;
	__u32 z = 0;
	int oldstate, newstate;
	__u64 sk;
	__u64 now, cg = 0;

	c = bpf_map_lookup_elem(&tcp_cfg, &z);
	if (!c)
		return 0;
	oldstate = BPF_CORE_READ(ctx, oldstate);
	newstate = BPF_CORE_READ(ctx, newstate);
	sk = (__u64)BPF_CORE_READ(ctx, skaddr);
	now = bpf_ktime_get_ns();
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	if (c->connect_on && newstate == 2) {
		struct bpf_ts_val val = {};
		char comm[16] = {};
		bpf_get_current_comm(comm, sizeof(comm));
		if (bpf_comm_ok(c->connect_comm, comm)) {
			val.ts = now;
			val.cgroup = cg;
			__builtin_memcpy(val.comm, comm, 16);
			bpf_map_update_elem(&syn_ts, &sk, &val, BPF_ANY);
		}
	}
	if (c->connect_on && (newstate == 1 || newstate == 7)) {
		struct bpf_ts_val *start = bpf_map_lookup_elem(&syn_ts, &sk);
		if (start && now > start->ts && (newstate == 1 || oldstate == 2)) {
			char comm[16] = {};
			__builtin_memcpy(comm, start->comm, 16);
			{
				struct bpf_hist_arg ha = {};
				ha.cgroup = start->cgroup;
				ha.delta = now - start->ts;
				bpf_hist_bump(&connect_hist, &connect_sum, comm, NULL, &ha);
			}
		}
		if (start)
			bpf_map_delete_elem(&syn_ts, &sk);
	}
	if (newstate == 1 && c->rtt_on && sk) {
		struct sock *skp = (struct sock *)sk;
		__u8 proto = BPF_CORE_READ(skp, sk_protocol);
		if (proto == 6) {
			struct tcp_sock *tp = (struct tcp_sock *)sk;
			__u32 srtt = BPF_CORE_READ(tp, srtt_us);
			__u64 ns;
			char comm[16] = {};
			if (srtt) {
				ns = (__u64)(srtt >> 3) * 1000ULL;
				bpf_get_current_comm(comm, sizeof(comm));
				if (bpf_comm_ok(c->rtt_comm, comm)) {
					struct bpf_hist_arg ha = {};
					ha.cgroup = cg;
					ha.delta = ns;
					bpf_hist_bump(&rtt_hist, &rtt_sum, comm, NULL, &ha);
				}
			}
		}
	}
	(void)oldstate;
	return 0;
}

SEC("tracepoint/tcp/tcp_send_reset")
int tcp_send_reset(struct trace_event_raw_tcp_send_reset *ctx)
{
	struct bpf_tcp_cfg *c;
	__u32 z = 0;
	__u64 cg = 0;
	__u16 dport = 0;
	__u8 daddr[28] = {};

	c = bpf_map_lookup_elem(&tcp_cfg, &z);
	if (!c || !c->reset_on)
		return 0;
	/* daddr is a sockaddr_in or sockaddr_in6; the port sits at offset 2. */
	if (!bpf_core_read(daddr, sizeof(daddr), &ctx->daddr))
		dport = bpf_ntohs(*(__u16 *)(daddr + 2));
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	bump_ctr(&reset_ctr, dport, c->dport_on, cg, c->reset_comm);
	return 0;
}

SEC("tracepoint/tcp/tcp_receive_reset")
int tcp_recv_reset(struct trace_event_raw_tcp_event_sk *ctx)
{
	struct bpf_tcp_cfg *c;
	__u32 z = 0;
	__u64 cg = 0;

	c = bpf_map_lookup_elem(&tcp_cfg, &z);
	if (!c || !c->reset_on)
		return 0;
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	bump_ctr(&reset_ctr, BPF_CORE_READ(ctx, dport), c->dport_on, cg, c->reset_comm);
	return 0;
}
