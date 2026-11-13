#pragma once

/*
 * At most five helper arguments. The verifier rejects a 6th C argument spilled
 * through R11 (see process_arg / fill_sockaddr: use a percpu flag, not a 6th
 * parameter, for kprobe vs uprobe sockaddr address space).
 */
struct bpf_hist_arg {
	__u64 cgroup;
	__u64 delta;
	const char *dev;
};

static __noinline void bpf_hist_bump(void *hist, void *sum, const char *comm,
	const char *extra, struct bpf_hist_arg *a)
{
	struct bpf_hist_key hk = {};
	struct bpf_sum_key sk = {};
	__u64 *slot;
	struct bpf_sum_val *sv;
	__u64 one = 1;
	__u32 bucket = 0;
	__u64 ns = a->delta;
	int i;

	__builtin_memcpy(hk.comm, comm, 16);
	if (extra)
		bpf_probe_read_kernel_str(hk.extra, sizeof(hk.extra), extra);
	if (a->dev)
		__builtin_memcpy(hk.dev, a->dev, 16);
	hk.cgroup = a->cgroup;
	for (i = 0; i < 21; i++) {
		__u64 lim = (1ULL << i) * 1000ULL;
		if (ns < lim) {
			bucket = (__u32)i;
			break;
		}
		bucket = 21;
	}
	hk.bucket = bucket;
	slot = bpf_map_lookup_elem(hist, &hk);
	if (slot)
		__sync_fetch_and_add(slot, 1);
	else
		bpf_map_update_elem(hist, &hk, &one, BPF_ANY);

	sk.cgroup = a->cgroup;
	__builtin_memcpy(sk.comm, comm, 16);
	if (extra)
		bpf_probe_read_kernel_str(sk.extra, sizeof(sk.extra), extra);
	if (a->dev)
		__builtin_memcpy(sk.dev, a->dev, 16);
	sv = bpf_map_lookup_elem(sum, &sk);
	if (sv) {
		__sync_fetch_and_add(&sv->sum_ns, a->delta);
		__sync_fetch_and_add(&sv->count, 1);
	} else {
		struct bpf_sum_val init = {};
		init.sum_ns = a->delta;
		init.count = 1;
		bpf_map_update_elem(sum, &sk, &init, BPF_ANY);
	}
}

/*
 * Keep rule compares <= 16: longer walks in generic kprobe blow the 1M
 * verifier budget. Filter values like ".example.com" fit. Suffix uses a
 * caller-provided text length so we never scan all 64 bytes here.
 */
#define BPF_STR_CMP_MAX 16

static __noinline int bpf_str_prefix(const char *text, const char *rule)
{
	int i;

	for (i = 0; i < BPF_STR_CMP_MAX; i++) {
		if (!rule[i])
			return i > 0;
		if (text[i] != rule[i])
			return 0;
	}
	return 1;
}

static __noinline int bpf_str_eq(const char *text, const char *rule);

static __noinline int bpf_str_suffix_len(const char *text, __u8 tlen, const char *rule)
{
	__u8 rlen = 0;
	__u8 i;
	__u8 off;

	for (i = 0; i < BPF_STR_CMP_MAX; i++) {
		if (!rule[i])
			break;
		rlen++;
	}
	if (!rlen || tlen < rlen)
		return 0;
	off = tlen - rlen;
	for (i = 0; i < BPF_STR_CMP_MAX; i++) {
		if (i >= rlen)
			return 1;
		if (text[(off + i) & 63] != rule[i])
			return 0;
	}
	return 1;
}

/* For smaller programs (dns/process). Generic kprobe passes tlen explicitly. */
static __noinline int bpf_str_suffix(const char *text, const char *rule)
{
	__u8 tlen = 0;
	__u8 i;

	for (i = 0; i < 64; i++) {
		if (!text[i])
			break;
		tlen++;
	}
	return bpf_str_suffix_len(text, tlen, rule);
}

static __always_inline int bpf_str_eq16(const char *text, const char *rule)
{
	int i;

	if (!rule || !rule[0])
		return 0;
	for (i = 0; i < 16; i++) {
		if (text[i] != rule[i])
			return 0;
		if (!text[i])
			return 1;
	}
	return 1;
}

/* Empty allow[0] means every process. Otherwise the task name must match one entry. */
static __always_inline int bpf_comm_ok(const char allow[BPF_COMM_MAX][16], const char *comm)
{
	int i;

	if (!allow[0][0])
		return 1;
	for (i = 0; i < BPF_COMM_MAX; i++) {
		if (!allow[i][0])
			return 0;
		if (bpf_str_eq16(comm, allow[i]))
			return 1;
	}
	return 0;
}

static __noinline void bpf_copy_str(char *dst, const char *src, int cap)
{
	int i;

	if (!src)
		return;
	for (i = 0; i < 64; i++) {
		if (i >= cap)
			break;
		dst[i] = src[i];
		if (!src[i])
			break;
	}
}

static __noinline int bpf_str_eq(const char *text, const char *rule)
{
	int i;

	for (i = 0; i < BPF_STR_CMP_MAX; i++) {
		if (text[i] != rule[i])
			return 0;
		if (!text[i])
			return 1;
	}
	return 1;
}
