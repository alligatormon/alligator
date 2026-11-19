#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include "bpf_types.h"
#include "map_help.h"

char LICENSE[] SEC("license") = "GPL";

#define ETH_P_IP 0x0800
#define ETH_P_IPV6 0x86DD
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define UDP_EPHEMERAL_MIN 32768

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_netdrop_cfg);
} netdrop_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_kfree_key);
	__type(value, __u64);
} kfree_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} udp_drop_ctr SEC(".maps");

static __always_inline struct bpf_netdrop_cfg *cfg(void)
{
	__u32 key = 0;
	return bpf_map_lookup_elem(&netdrop_cfg, &key);
}

static __always_inline __u16 eth_code(__u16 proto)
{
	if (proto == ETH_P_IP)
		return 1;
	if (proto == ETH_P_IPV6)
		return 2;
	return 0;
}

static __always_inline __u16 ip_code(__u16 proto)
{
	if (proto == IPPROTO_TCP)
		return 1;
	if (proto == IPPROTO_UDP)
		return 2;
	if (proto == IPPROTO_ICMP || proto == 58) /* ICMPv6 */
		return 3;
	return 0;
}

static __always_inline int header_was_set(__u16 off)
{
	return off != (__u16)~0U;
}

static __always_inline void bump_kfree(__u16 eth, __u16 ip, __u16 reason)
{
	struct bpf_kfree_key key = {};
	__u64 *v;
	__u64 one = 1;

	key.eth = eth;
	key.ip = ip;
	key.reason = reason;
	v = bpf_map_lookup_elem(&kfree_ctr, &key);
	if (v)
		__sync_fetch_and_add(v, 1);
	else
		bpf_map_update_elem(&kfree_ctr, &key, &one, BPF_ANY);
}

SEC("tp_btf/kfree_skb")
int BPF_PROG(on_kfree_skb, struct sk_buff *skb, void *location, enum skb_drop_reason reason)
{
	struct bpf_netdrop_cfg *c = cfg();
	struct ethhdr eth_hdr = {};
	struct iphdr ip_hdr = {};
	struct ipv6hdr ipv6_hdr = {};
	__u16 eth_proto = 0;
	__u16 ip_proto = 0;
	__u16 mac_off;
	__u16 net_off;
	void *head;

	(void)location;
	if (!c || !c->kfree_on || !skb)
		return 0;

	/*
	 * Always count: many stack-local drops (e.g. udp_fail_queue_rcv_skb)
	 * never set a MAC header. eth/ip are best-effort; reason is required.
	 */
	mac_off = BPF_CORE_READ(skb, mac_header);
	net_off = BPF_CORE_READ(skb, network_header);
	head = (void *)(long)BPF_CORE_READ(skb, head);

	if (header_was_set(mac_off)) {
		if (!bpf_probe_read_kernel(&eth_hdr, sizeof(eth_hdr), head + mac_off))
			eth_proto = bpf_ntohs(eth_hdr.h_proto);
	}
	if (!eth_proto)
		eth_proto = bpf_ntohs(BPF_CORE_READ(skb, protocol));

	if (header_was_set(net_off)) {
		switch (eth_proto) {
		case ETH_P_IP:
			if (!bpf_probe_read_kernel(&ip_hdr, sizeof(ip_hdr), head + net_off))
				ip_proto = ip_hdr.protocol;
			break;
		case ETH_P_IPV6:
			if (!bpf_probe_read_kernel(&ipv6_hdr, sizeof(ipv6_hdr), head + net_off))
				ip_proto = ipv6_hdr.nexthdr;
			break;
		default:
			break;
		}
	}

	bump_kfree(eth_code(eth_proto), ip_code(ip_proto), (__u16)reason);
	return 0;
}

SEC("tp_btf/udp_fail_queue_rcv_skb")
int BPF_PROG(on_udp_fail_queue, int rc, struct sock *sk)
{
	struct bpf_netdrop_cfg *c = cfg();
	struct bpf_ctr_key key = {};
	__u16 lport;
	__u64 *v;
	__u64 one = 1;

	(void)rc;
	if (!c || !c->udp_drop_on || !sk)
		return 0;
	lport = BPF_CORE_READ(sk, __sk_common.skc_num);
	if (lport >= UDP_EPHEMERAL_MIN)
		lport = 0;
	key.dport = lport;
	key.changed = 1;
	v = bpf_map_lookup_elem(&udp_drop_ctr, &key);
	if (v)
		__sync_fetch_and_add(v, 1);
	else
		bpf_map_update_elem(&udp_drop_ctr, &key, &one, BPF_ANY);
	return 0;
}
