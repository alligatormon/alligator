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
	__uint(max_entries, 32);
	__type(key, __u32);
	__type(value, struct bpf_probe_cfg);
} probe_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_event_key);
} scratch SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_event_key);
	__type(value, __u64);
} events SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 32);
	__type(key, __u32);
	__type(value, struct bpf_drop_val);
} drops SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, struct bpf_lat_start_key);
	__type(value, struct bpf_lat_start);
} lat_start SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_lat_hist_key);
	__type(value, __u64);
} lat_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_lat_sum_key);
	__type(value, struct bpf_sum_val);
} lat_sum SEC(".maps");

/* Large hist/sum keys do not fit the 512-byte BPF stack. */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_lat_hist_key);
} lat_hk SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_lat_sum_key);
} lat_sk SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_lat_start);
} lat_sv SEC(".maps");

struct sa_in {
	__u16 family;
	__u16 port;
	__u32 addr;
};

/* Decimal and address formatting does not fit the 512-byte BPF stack on clang 23. */
struct fmt_buf {
	char num[32];
	char tmp[24];
	char part[4][8];
	__u8 raw[16];
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct fmt_buf);
} fmtbuf SEC(".maps");

/*
 * Do not pass a 6th C argument into process_arg: the verifier rejects the
 * spill through R11. Stash kprobe(0)/uprobe(1) here for fill_sockaddr.
 */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} sa_user SEC(".maps");

static __always_inline struct fmt_buf *fmt_get(void)
{
	__u32 z = 0;
	return bpf_map_lookup_elem(&fmtbuf, &z);
}

static __always_inline void set_sa_user(__u32 user)
{
	__u32 z = 0;
	__u32 *p = bpf_map_lookup_elem(&sa_user, &z);

	if (p)
		*p = user;
}

static __always_inline int get_sa_user(void)
{
	__u32 z = 0;
	__u32 *p = bpf_map_lookup_elem(&sa_user, &z);

	return p ? (int)*p : 0;
}

static __always_inline void note_drop(__u32 id, int trunc)
{
	struct bpf_drop_val *d = bpf_map_lookup_elem(&drops, &id);
	if (!d)
		return;
	if (trunc)
		__sync_fetch_and_add(&d->trunc, 1);
	else
		__sync_fetch_and_add(&d->lru, 1);
}

static __always_inline void fmt_u32(char *dst, __u32 v)
{
	char tmp[10];
	int n = 0;
	int i;

	if (!v) {
		dst[0] = '0';
		dst[1] = 0;
		return;
	}
	for (i = 0; i < 10; i++) {
		tmp[n] = '0' + (v % 10);
		n++;
		v /= 10;
		if (!v)
			break;
	}
	for (i = 0; i < 10; i++) {
		if (i >= n)
			break;
		dst[i] = tmp[n - 1 - i];
	}
	dst[n] = 0;
}

static __noinline int arg_ok(const struct bpf_arg_cfg *a, const char *text, __u8 tlen)
{
	if (a->filter == 0)
		return 1;
	if (a->filter == 1)
		return a->value[0] && bpf_str_eq(text, a->value);
	if (a->filter == 4) {
		if (a->value[0] && bpf_str_eq(text, a->value))
			return 1;
		if (a->allow0[0] && bpf_str_eq(text, a->allow0))
			return 1;
		if (a->allow1[0] && bpf_str_eq(text, a->allow1))
			return 1;
		if (a->allow2[0] && bpf_str_eq(text, a->allow2))
			return 1;
		if (a->allow3[0] && bpf_str_eq(text, a->allow3))
			return 1;
		return 0;
	}
	if (a->filter == 2)
		return bpf_str_prefix(text, a->value);
	if (a->filter == 3)
		return bpf_str_suffix_len(text, tlen, a->value);
	return 0;
}

static __always_inline char *slot(struct bpf_event_key *ek, int i)
{
	if (i == 0)
		return ek->l0;
	if (i == 1)
		return ek->l1;
	if (i == 2)
		return ek->l2;
	if (i == 3)
		return ek->l3;
	if (i == 4)
		return ek->l4;
	return ek->l5;
}

/*
 * Direct PT_REGS_PARM*(ctx) after a map-derived branch becomes
 * "dereference of modified ctx ptr" on Linux 6.1+. Capture every argument
 * with constant-offset loads in the entry program, then pass a stack array.
 * Avoid PT_REGS_PARM*_CORE here: six CO-RE reads blow the verifier budget.
 */
static __always_inline void read_parms(struct pt_regs *ctx, unsigned long parms[6])
{
	parms[0] = PT_REGS_PARM1(ctx);
	parms[1] = PT_REGS_PARM2(ctx);
	parms[2] = PT_REGS_PARM3(ctx);
	parms[3] = PT_REGS_PARM4(ctx);
	parms[4] = PT_REGS_PARM5(ctx);
	parms[5] = PT_REGS_PARM6(ctx);
}

/* Returns string length in *tlen (0 on failure). */
static __noinline int fill_u64(char *dst, unsigned long ptr, __u8 *tlen)
{
	struct fmt_buf *fb = fmt_get();
	int n = 0;
	int j;
	__u64 v = ptr;

	if (!fb)
		return 0;
	if (!v) {
		dst[0] = '0';
		dst[1] = 0;
		*tlen = 1;
		return 1;
	}
	for (j = 0; j < 20; j++) {
		fb->tmp[n] = '0' + (v % 10);
		n++;
		v /= 10;
		if (!v)
			break;
	}
	for (j = 0; j < 20; j++) {
		if (j >= n)
			break;
		fb->num[j] = fb->tmp[n - 1 - j];
	}
	fb->num[n] = 0;
	bpf_probe_read_kernel_str(dst, 64, fb->num);
	*tlen = (__u8)n;
	return 1;
}

static __noinline int fill_str(__u32 id, char *dst, unsigned long ptr, __u8 kind, __u8 *tlen)
{
	long n;

	if (kind == BPF_ARG_USER_STR)
		n = bpf_probe_read_user_str(dst, 64, (void *)ptr);
	else
		n = bpf_probe_read_kernel_str(dst, 64, (void *)ptr);
	if (n <= 0)
		return 0;
	if (n >= 64)
		note_drop(id, 1);
	/* probe_read_*_str includes the NUL in the return count. */
	*tlen = (__u8)(n > 1 ? n - 1 : 0);
	return 1;
}

/*
 * Uprobe: userspace sockaddr. Kprobe (e.g. tcp_v4_connect): address already
 * move_addr_to_kernel()'d — read kernel memory. Mode comes from sa_user.
 */
static __noinline int fill_sockaddr(char *dst, unsigned long ptr, __u8 field, __u8 *tlen)
{
	struct sa_in sa = {};
	struct fmt_buf *fb = fmt_get();
	int k = 0;
	int j;
	int user = get_sa_user();
	long rc;

	if (!fb)
		return 0;
	if (user)
		rc = bpf_probe_read_user(&sa, sizeof(sa), (void *)ptr);
	else
		rc = bpf_probe_read_kernel(&sa, sizeof(sa), (void *)ptr);
	if (rc < 0)
		return 0;
	if (sa.family == 10) {
		if (user)
			rc = bpf_probe_read_user(fb->raw, sizeof(fb->raw), (void *)(ptr + 8));
		else
			rc = bpf_probe_read_kernel(fb->raw, sizeof(fb->raw), (void *)(ptr + 8));
		if (rc < 0)
			return 0;
		if (field == BPF_FIELD_PORT) {
			fmt_u32(dst, bpf_ntohs(sa.port));
			for (j = 0; j < 8 && dst[j]; j++)
				;
			*tlen = (__u8)j;
			return 1;
		}
		{
			static const char hex[] = "0123456789abcdef";
			for (j = 0; j < 16 && k < 60; j++) {
				if (j && (j % 2) == 0)
					dst[k++] = ':';
				dst[k++] = hex[(fb->raw[j] >> 4) & 0xf];
				dst[k++] = hex[fb->raw[j] & 0xf];
			}
			dst[k] = 0;
		}
		*tlen = (__u8)k;
		return 1;
	}
	if (sa.family != 2)
		return 0;
	if (field == BPF_FIELD_PORT) {
		fmt_u32(dst, bpf_ntohs(sa.port));
		for (j = 0; j < 8 && dst[j]; j++)
			;
		*tlen = (__u8)j;
		return 1;
	}
	{
		__u32 addr = bpf_ntohl(sa.addr);
		fmt_u32(fb->part[0], (addr >> 24) & 0xff);
		fmt_u32(fb->part[1], (addr >> 16) & 0xff);
		fmt_u32(fb->part[2], (addr >> 8) & 0xff);
		fmt_u32(fb->part[3], addr & 0xff);
		for (j = 0; j < 4 && fb->part[0][j] && k < 62; j++)
			dst[k++] = fb->part[0][j];
		dst[k++] = '.';
		for (j = 0; j < 4 && fb->part[1][j] && k < 62; j++)
			dst[k++] = fb->part[1][j];
		dst[k++] = '.';
		for (j = 0; j < 4 && fb->part[2][j] && k < 62; j++)
			dst[k++] = fb->part[2][j];
		dst[k++] = '.';
		for (j = 0; j < 4 && fb->part[3][j] && k < 63; j++)
			dst[k++] = fb->part[3][j];
		dst[k] = 0;
	}
	*tlen = (__u8)k;
	return 1;
}

static __noinline int process_arg(__u32 id, struct bpf_event_key *ek, const struct bpf_arg_cfg *a,
	int idx, const unsigned long parms[6])
{
	char *dst;
	unsigned long ptr;
	__u8 reg;
	__u8 tlen = 0;
	int ok;

	dst = slot(ek, idx);
	reg = a->reg;
	if (reg > 5)
		reg = 5;
	ptr = parms[reg];
	if (a->kind == BPF_ARG_U64)
		ok = fill_u64(dst, ptr, &tlen);
	else if (a->kind == BPF_ARG_USER_STR || a->kind == BPF_ARG_KERNEL_STR)
		ok = fill_str(id, dst, ptr, a->kind, &tlen);
	else if (a->kind == BPF_ARG_SOCKADDR)
		ok = fill_sockaddr(dst, ptr, a->field, &tlen);
	else
		return 1;
	if (!ok)
		return 0;
	return arg_ok(a, dst, tlen);
}

static __noinline int handle(struct pt_regs *ctx, const unsigned long parms[6])
{
	__u64 cookie = bpf_get_attach_cookie(ctx);
	__u32 id = (__u32)cookie;
	struct bpf_probe_cfg *cfg;
	struct bpf_event_key *ek;
	__u32 z = 0;
	__u32 want_lat;
	__u64 one = 1;
	__u64 *cur;

	if (id >= 32)
		return 0;
	cfg = bpf_map_lookup_elem(&probe_cfg, &id);
	if (!cfg)
		return 0;
	want_lat = cfg->latency;
	{
		char comm[16] = {};
		bpf_get_current_comm(comm, sizeof(comm));
		if (!bpf_comm_ok(cfg->comm, comm))
			return 0;
	}
	ek = bpf_map_lookup_elem(&scratch, &z);
	if (!ek)
		return 0;
	__builtin_memset(ek, 0, sizeof(*ek));
	ek->probe_id = id;

	/* Unrolled: a loop over map-derived nargs multiplies verifier states. */
	if (cfg->nargs > 0 && !process_arg(id, ek, &cfg->args[0], 0, parms))
		return 0;
	if (cfg->nargs > 1 && !process_arg(id, ek, &cfg->args[1], 1, parms))
		return 0;
	if (cfg->nargs > 2 && !process_arg(id, ek, &cfg->args[2], 2, parms))
		return 0;
	if (cfg->nargs > 3 && !process_arg(id, ek, &cfg->args[3], 3, parms))
		return 0;
	if (cfg->nargs > 4 && !process_arg(id, ek, &cfg->args[4], 4, parms))
		return 0;
	if (cfg->nargs > 5 && !process_arg(id, ek, &cfg->args[5], 5, parms))
		return 0;

	cur = bpf_map_lookup_elem(&events, ek);
	if (cur)
		__sync_fetch_and_add(cur, 1);
	else {
		struct bpf_drop_val *d = bpf_map_lookup_elem(&drops, &id);
		if (d && d->live >= 4096)
			__sync_fetch_and_add(&d->lru, 1);
		if (bpf_map_update_elem(&events, ek, &one, BPF_ANY))
			note_drop(id, 0);
		else if (d && d->live < 4096)
			__sync_fetch_and_add(&d->live, 1);
	}
	if (want_lat) {
		struct bpf_lat_start_key sk = {};
		struct bpf_lat_start *sv;
		__u32 z2 = 0;

		sv = bpf_map_lookup_elem(&lat_sv, &z2);
		if (sv) {
			__builtin_memset(sv, 0, sizeof(*sv));
			sv->ts = bpf_ktime_get_ns();
			__builtin_memcpy(sv->l0, ek->l0, sizeof(sv->l0));
			__builtin_memcpy(sv->l1, ek->l1, sizeof(sv->l1));
			__builtin_memcpy(sv->l2, ek->l2, sizeof(sv->l2));
			__builtin_memcpy(sv->l3, ek->l3, sizeof(sv->l3));
			__builtin_memcpy(sv->l4, ek->l4, sizeof(sv->l4));
			__builtin_memcpy(sv->l5, ek->l5, sizeof(sv->l5));
			sk.tid = bpf_get_current_pid_tgid();
			sk.probe_id = id;
			bpf_map_update_elem(&lat_start, &sk, sv, BPF_ANY);
		}
	}
	return 0;
}

static __noinline void lat_bump(__u32 id, const struct bpf_lat_start *start, __u64 delta)
{
	struct bpf_lat_hist_key *hk;
	struct bpf_lat_sum_key *sk;
	__u32 z = 0;
	__u32 bucket = 0;
	__u64 *slot;
	__u64 one = 1;
	struct bpf_sum_val *sv;
	__u64 ns = delta;
	int i;

	hk = bpf_map_lookup_elem(&lat_hk, &z);
	sk = bpf_map_lookup_elem(&lat_sk, &z);
	if (!hk || !sk)
		return;
	__builtin_memset(hk, 0, sizeof(*hk));
	__builtin_memset(sk, 0, sizeof(*sk));
	hk->probe_id = id;
	sk->probe_id = id;
	__builtin_memcpy(hk->l0, start->l0, sizeof(hk->l0));
	__builtin_memcpy(hk->l1, start->l1, sizeof(hk->l1));
	__builtin_memcpy(hk->l2, start->l2, sizeof(hk->l2));
	__builtin_memcpy(hk->l3, start->l3, sizeof(hk->l3));
	__builtin_memcpy(hk->l4, start->l4, sizeof(hk->l4));
	__builtin_memcpy(hk->l5, start->l5, sizeof(hk->l5));
	__builtin_memcpy(sk->l0, start->l0, sizeof(sk->l0));
	__builtin_memcpy(sk->l1, start->l1, sizeof(sk->l1));
	__builtin_memcpy(sk->l2, start->l2, sizeof(sk->l2));
	__builtin_memcpy(sk->l3, start->l3, sizeof(sk->l3));
	__builtin_memcpy(sk->l4, start->l4, sizeof(sk->l4));
	__builtin_memcpy(sk->l5, start->l5, sizeof(sk->l5));
	for (i = 0; i < 21; i++) {
		__u64 lim = (1ULL << i) * 1000ULL;
		if (ns < lim) {
			bucket = (__u32)i;
			break;
		}
		bucket = 21;
	}
	hk->bucket = bucket;
	slot = bpf_map_lookup_elem(&lat_hist, hk);
	if (slot)
		__sync_fetch_and_add(slot, 1);
	else
		bpf_map_update_elem(&lat_hist, hk, &one, BPF_ANY);
	sv = bpf_map_lookup_elem(&lat_sum, sk);
	if (sv) {
		__sync_fetch_and_add(&sv->sum_ns, delta);
		__sync_fetch_and_add(&sv->count, 1);
	} else {
		struct bpf_sum_val init = {};
		init.sum_ns = delta;
		init.count = 1;
		bpf_map_update_elem(&lat_sum, sk, &init, BPF_ANY);
	}
}

static __noinline int handle_ret(struct pt_regs *ctx)
{
	__u64 cookie = bpf_get_attach_cookie(ctx);
	__u32 id = (__u32)cookie;
	struct bpf_lat_start_key sk = {};
	struct bpf_lat_start *start;
	__u64 now;

	if (id >= 32)
		return 0;
	sk.tid = bpf_get_current_pid_tgid();
	sk.probe_id = id;
	start = bpf_map_lookup_elem(&lat_start, &sk);
	if (!start)
		return 0;
	now = bpf_ktime_get_ns();
	if (now > start->ts)
		lat_bump(id, start, now - start->ts);
	bpf_map_delete_elem(&lat_start, &sk);
	return 0;
}

SEC("kprobe")
int generic_kprobe(struct pt_regs *ctx)
{
	unsigned long parms[6];

	set_sa_user(0);
	read_parms(ctx, parms);
	return handle(ctx, parms);
}

SEC("kretprobe")
int generic_kretprobe(struct pt_regs *ctx)
{
	return handle_ret(ctx);
}

SEC("uprobe")
int generic_uprobe(struct pt_regs *ctx)
{
	unsigned long parms[6];

	set_sa_user(1);
	read_parms(ctx, parms);
	return handle(ctx, parms);
}

SEC("uretprobe")
int generic_uretprobe(struct pt_regs *ctx)
{
	return handle_ret(ctx);
}
