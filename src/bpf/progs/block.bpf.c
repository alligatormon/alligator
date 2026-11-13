#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_types.h"
#include "map_help.h"

char LICENSE[] SEC("license") = "GPL";

struct bio_key {
	__u32 dev;
	__u32 pad;
	__u64 sector;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_block_cfg);
} block_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, struct bio_key);
	__type(value, struct bpf_ts_val);
} issue_ts SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} block_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} block_sum SEC(".maps");

/*
 * Constant indexes only. A variable stack index becomes `r1 |= index` in
 * clang 23, and the 5.14 verifier rejects bitwise ops on a pointer.
 * dev_t is 12-bit major + 20-bit minor, so 4 and 7 digits are enough.
 */
static __always_inline int u32_digits(char *dst, __u32 v)
{
	if (v >= 1000000) {
		dst[0] = '0' + v / 1000000; v %= 1000000;
		dst[1] = '0' + v / 100000; v %= 100000;
		dst[2] = '0' + v / 10000; v %= 10000;
		dst[3] = '0' + v / 1000; v %= 1000;
		dst[4] = '0' + v / 100; v %= 100;
		dst[5] = '0' + v / 10;
		dst[6] = '0' + v % 10;
		return 7;
	}
	if (v >= 100000) {
		dst[0] = '0' + v / 100000; v %= 100000;
		dst[1] = '0' + v / 10000; v %= 10000;
		dst[2] = '0' + v / 1000; v %= 1000;
		dst[3] = '0' + v / 100; v %= 100;
		dst[4] = '0' + v / 10;
		dst[5] = '0' + v % 10;
		return 6;
	}
	if (v >= 10000) {
		dst[0] = '0' + v / 10000; v %= 10000;
		dst[1] = '0' + v / 1000; v %= 1000;
		dst[2] = '0' + v / 100; v %= 100;
		dst[3] = '0' + v / 10;
		dst[4] = '0' + v % 10;
		return 5;
	}
	if (v >= 1000) {
		dst[0] = '0' + v / 1000; v %= 1000;
		dst[1] = '0' + v / 100; v %= 100;
		dst[2] = '0' + v / 10;
		dst[3] = '0' + v % 10;
		return 4;
	}
	if (v >= 100) {
		dst[0] = '0' + v / 100; v %= 100;
		dst[1] = '0' + v / 10;
		dst[2] = '0' + v % 10;
		return 3;
	}
	if (v >= 10) {
		dst[0] = '0' + v / 10;
		dst[1] = '0' + v % 10;
		return 2;
	}
	dst[0] = '0' + v;
	return 1;
}

#define WRITE_MIN(off) do { \
		dst[off] = min[0]; \
		if (nn > 1) dst[(off) + 1] = min[1]; \
		if (nn > 2) dst[(off) + 2] = min[2]; \
		if (nn > 3) dst[(off) + 3] = min[3]; \
		if (nn > 4) dst[(off) + 4] = min[4]; \
		if (nn > 5) dst[(off) + 5] = min[5]; \
		if (nn > 6) dst[(off) + 6] = min[6]; \
	} while (0)

static __always_inline void dev_str(char dst[16], __u32 dev)
{
	char maj[8] = {};
	char min[8] = {};
	int nm, nn;

	dst[0] = dst[1] = dst[2] = dst[3] = dst[4] = dst[5] = dst[6] = dst[7] = 0;
	dst[8] = dst[9] = dst[10] = dst[11] = dst[12] = dst[13] = dst[14] = dst[15] = 0;
	nm = u32_digits(maj, dev >> 20);
	nn = u32_digits(min, dev & 0xfffff);
	dst[0] = maj[0];
	if (nm > 1)
		dst[1] = maj[1];
	if (nm > 2)
		dst[2] = maj[2];
	if (nm > 3)
		dst[3] = maj[3];
	if (nm <= 1) {
		dst[1] = ':';
		WRITE_MIN(2);
	} else if (nm == 2) {
		dst[2] = ':';
		WRITE_MIN(3);
	} else if (nm == 3) {
		dst[3] = ':';
		WRITE_MIN(4);
	} else {
		dst[4] = ':';
		WRITE_MIN(5);
	}
}
#undef WRITE_MIN

static __always_inline void on_issue(__u32 dev, __u64 sector, const char *rwbs)
{
	struct bpf_block_cfg *c;
	struct bio_key key = {};
	struct bpf_ts_val val = {};
	__u32 z = 0;

	c = bpf_map_lookup_elem(&block_cfg, &z);
	if (!c || !c->on)
		return;
	if (c->comm[0][0]) {
		bpf_get_current_comm(val.comm, sizeof(val.comm));
		if (!bpf_comm_ok(c->comm, val.comm))
			return;
	}
	key.dev = dev;
	key.sector = sector;
	val.ts = bpf_ktime_get_ns();
	if (c->cgroup_labels)
		val.cgroup = bpf_get_current_cgroup_id();
	if (rwbs && rwbs[0] == 'W')
		__builtin_memcpy(val.extra, "write", 6);
	else if (rwbs && rwbs[0] == 'R')
		__builtin_memcpy(val.extra, "read", 5);
	else
		__builtin_memcpy(val.extra, "other", 6);
	bpf_map_update_elem(&issue_ts, &key, &val, BPF_ANY);
}

static __always_inline void on_done(__u32 dev, __u64 sector)
{
	struct bio_key key = {};
	struct bpf_ts_val *start;
	char devs[16] = {};
	__u64 now;

	key.dev = dev;
	key.sector = sector;
	start = bpf_map_lookup_elem(&issue_ts, &key);
	if (!start)
		return;
	now = bpf_ktime_get_ns();
	dev_str(devs, dev);
	if (now > start->ts)
		{
			struct bpf_hist_arg ha = {};
			ha.cgroup = start->cgroup;
			ha.delta = now - start->ts;
			ha.dev = devs;
			bpf_hist_bump(&block_hist, &block_sum, start->comm, start->extra, &ha);
		}
	bpf_map_delete_elem(&issue_ts, &key);
}

/* Older BTF names the issue event trace_event_raw_block_rq_issue.
 * 5.14 names the shared class trace_event_raw_block_rq. */
struct trace_event_raw_block_rq_issue___old {
	dev_t dev;
	sector_t sector;
	char rwbs[8];
} __attribute__((preserve_access_index));

SEC("tracepoint/block/block_rq_issue")
int block_issue(void *ctx)
{
	char rwbs[8] = {};
	dev_t dev = 0;
	sector_t sector = 0;

	if (bpf_core_type_exists(struct trace_event_raw_block_rq_issue___old)) {
		struct trace_event_raw_block_rq_issue___old *ev = ctx;
		dev = BPF_CORE_READ(ev, dev);
		sector = BPF_CORE_READ(ev, sector);
		bpf_core_read_str(rwbs, sizeof(rwbs), &ev->rwbs);
	} else {
		struct trace_event_raw_block_rq *ev = ctx;
		dev = BPF_CORE_READ(ev, dev);
		sector = BPF_CORE_READ(ev, sector);
		bpf_core_read_str(rwbs, sizeof(rwbs), &ev->rwbs);
	}
	on_issue(dev, sector, rwbs);
	return 0;
}

/*
 * d5869fdc189f moved block_rq_complete onto event class block_rq_completion.
 * The event name stays block_rq_complete. BTF then exposes
 * trace_event_raw_block_rq_completion and drops trace_event_raw_block_rq_complete.
 */
struct trace_event_raw_block_rq_complete___x {
	dev_t dev;
	sector_t sector;
} __attribute__((preserve_access_index));

struct trace_event_raw_block_rq_completion___x {
	dev_t dev;
	sector_t sector;
} __attribute__((preserve_access_index));

SEC("tracepoint/block/block_rq_complete")
int block_complete(void *ctx)
{
	dev_t dev;
	sector_t sector;

	if (bpf_core_type_exists(struct trace_event_raw_block_rq_completion___x)) {
		struct trace_event_raw_block_rq_completion___x *ev = ctx;
		dev = BPF_CORE_READ(ev, dev);
		sector = BPF_CORE_READ(ev, sector);
	} else {
		struct trace_event_raw_block_rq_complete___x *ev = ctx;
		dev = BPF_CORE_READ(ev, dev);
		sector = BPF_CORE_READ(ev, sector);
	}
	on_done(dev, sector);
	return 0;
}
