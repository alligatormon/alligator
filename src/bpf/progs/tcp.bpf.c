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

/* Accept-queue wait: key is child sock* from inet_csk_reqsk_queue_add. */
struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 16384);
	__type(key, __u64);
	__type(value, struct bpf_ts_val);
} accept_ts SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} accept_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} accept_sum SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} syn_backlog_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} syn_backlog_sum SEC(".maps");

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

/*
 * Build-host vmlinux.h may describe tcp_send_reset with sockaddr-like daddr and
 * no dport; reading that daddr breaks CO-RE on 6.1 elrepo and drops the whole
 * tcp skeleton. A ___ flavor only names dport; libbpf matches the essential
 * type at load time against the running kernel.
 */
struct trace_event_raw_tcp_send_reset___dport {
	__u16 dport;
} __attribute__((preserve_access_index));

SEC("tracepoint/tcp/tcp_send_reset")
int tcp_send_reset(void *ctx)
{
	struct bpf_tcp_cfg *c;
	__u32 z = 0;
	__u64 cg = 0;
	__u16 dport = 0;
	struct trace_event_raw_tcp_send_reset___dport *e = ctx;

	c = bpf_map_lookup_elem(&tcp_cfg, &z);
	if (!c || !c->reset_on)
		return 0;
	if (bpf_core_field_exists(e->dport))
		dport = BPF_CORE_READ(e, dport);
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

static __always_inline void port_to_str(__u16 port, char *dst)
{
	__u16 n = port;
	int i = 0, j = 0;
	char tmp[6];

	if (!n) {
		dst[0] = '0';
		dst[1] = 0;
		return;
	}
	while (n && i < 5) {
		tmp[i++] = '0' + (n % 10);
		n /= 10;
	}
	while (i > 0)
		dst[j++] = tmp[--i];
	dst[j] = 0;
}

/*
 * Accept-queue latency: from placing the completed child on the listen accept
 * queue until a successful inet_csk_accept return. Manual attach in userspace.
 *
 * inet_csk_reqsk_queue_add(struct sock *sk, struct request_sock *req,
 *                          struct sock *child)
 */
SEC("kprobe")
int tcp_accept_queue_add(struct pt_regs *ctx)
{
	struct bpf_tcp_cfg *c;
	__u32 z = 0;
	struct sock *sk;
	struct sock *child;
	struct bpf_ts_val val = {};
	__u64 key;
	__u16 port;

	c = bpf_map_lookup_elem(&tcp_cfg, &z);
	if (!c || !c->accept_on)
		return 0;
	sk = (struct sock *)PT_REGS_PARM1(ctx);
	child = (struct sock *)PT_REGS_PARM3(ctx);
	if (!sk || !child)
		return 0;
	key = (__u64)child;
	val.ts = bpf_ktime_get_ns();
	port = BPF_CORE_READ(sk, __sk_common.skc_num);
	port_to_str(port, val.extra);
	bpf_map_update_elem(&accept_ts, &key, &val, BPF_ANY);
	return 0;
}

/* kretprobe inet_csk_accept — retval is the accepted child sock* (or NULL). */
SEC("kretprobe")
int tcp_accept_ret(struct pt_regs *ctx)
{
	struct bpf_tcp_cfg *c;
	__u32 z = 0;
	struct sock *newsk;
	struct bpf_ts_val *v;
	__u64 key;
	__u64 now;
	__u64 delta;
	char empty[16] = {};

	c = bpf_map_lookup_elem(&tcp_cfg, &z);
	if (!c || !c->accept_on)
		return 0;
	newsk = (struct sock *)PT_REGS_RC(ctx);
	if (!newsk)
		return 0;
	key = (__u64)newsk;
	/*
	 * Keyed by unique child sock*. Concurrent accepts cannot share a key,
	 * so lookup+delete is enough (avoids bpf_map_lookup_and_delete_elem,
	 * which needs kernel 5.13+ and would block the whole tcp skeleton on
	 * 5.8–5.12).
	 *
	 * Pass v->extra (map value) into bpf_hist_bump — not a stack copy —
	 * so bpf_probe_read_kernel_str sees a kernel pointer (same pattern as
	 * block_io). Avoid copying the whole bpf_ts_val onto the BPF stack.
	 */
	v = bpf_map_lookup_elem(&accept_ts, &key);
	if (!v)
		return 0;
	now = bpf_ktime_get_ns();
	if (now <= v->ts) {
		bpf_map_delete_elem(&accept_ts, &key);
		return 0;
	}
	delta = now - v->ts;
	{
		struct bpf_hist_arg ha = {};

		ha.delta = delta;
		bpf_hist_bump(&accept_hist, &accept_sum, empty, v->extra, &ha);
	}
	bpf_map_delete_elem(&accept_ts, &key);
	return 0;
}

static __always_inline void syn_backlog_sample(struct sock *sk)
{
	struct bpf_tcp_cfg *c;
	__u32 z = 0;
	__u32 backlog;
	char empty[16] = {};

	c = bpf_map_lookup_elem(&tcp_cfg, &z);
	if (!c || !c->syn_backlog_on || !sk)
		return;
	backlog = BPF_CORE_READ(sk, sk_ack_backlog);
	{
		struct bpf_hist_arg ha = {};
		ha.delta = backlog;
		bpf_size_hist_bump(&syn_backlog_hist, &syn_backlog_sum, empty, NULL, &ha);
	}
}

SEC("kprobe")
int tcp_syn_recv_v4(struct pt_regs *ctx)
{
	syn_backlog_sample((struct sock *)PT_REGS_PARM1(ctx));
	return 0;
}

SEC("kprobe")
int tcp_syn_recv_v6(struct pt_regs *ctx)
{
	syn_backlog_sample((struct sock *)PT_REGS_PARM1(ctx));
	return 0;
}
