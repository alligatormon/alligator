#pragma once

/*
 * Shared by userspace and BPF programs. Include vmlinux.h (or linux/types.h)
 * before this header from a .bpf.c file. Pass -D__BPF_PROG__ when compiling BPF.
 */

#ifdef __BPF_PROG__
#elif defined(__linux__)
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int64_t __s64;
#endif

#define BPF_STR_MAX 64
#define BPF_ARG_MAX 6
#define BPF_PROBE_SLOTS 32
#define BPF_ALLOW_MAX 4
#define BPF_PREFIX_MAX 4
#define BPF_COMM_MAX 8
#define BPF_SYSCALL_SLOTS 8
#define BPF_HIST_BUCKETS 22
#define BPF_HIST_LAST 21
#define BPF_LRU_CAP 4096
/* Shared userspace staging table for all named hist/counter/generic series per scrape. */
#define BPF_SERIES_MAX BPF_LRU_CAP

#define BPF_ARG_NONE 0
#define BPF_ARG_U64 1
#define BPF_ARG_USER_STR 2
#define BPF_ARG_KERNEL_STR 3
#define BPF_ARG_SOCKADDR 4
#define BPF_ARG_USER_BUFFER 5

/* mode on bpf_probe_cfg. Count is the historical kprobe/uprobe path. */
#define BPF_MODE_COUNT 0
#define BPF_MODE_CAPTURE 1

/* Where a user_buffer length comes from. */
#define BPF_SIZE_NONE 0
#define BPF_SIZE_ARG 1
#define BPF_SIZE_RETVAL 2
#define BPF_SIZE_DEREF_ARG 3
#define BPF_SIZE_DEREF_SAVED 4

/* Hard cap copied into one ringbuf slot. Config max= is clamped to this. */
#define BPF_CAPTURE_MAX 512
#define BPF_CAPTURE_BUFS 2
#define BPF_REGEX_MAX 512

#define BPF_FILTER_NONE 0
#define BPF_FILTER_EQ 1
#define BPF_FILTER_PREFIX 2
#define BPF_FILTER_SUFFIX 3
#define BPF_FILTER_ALLOW 4

#define BPF_FIELD_ADDR 0
#define BPF_FIELD_PORT 1

#define BPF_KIND_KPROBE 1
#define BPF_KIND_UPROBE 2
#define BPF_KIND_NAMED 3
#define BPF_KIND_URETPROBE 4

#define BPF_NAMED_RUNQUEUE 1
#define BPF_NAMED_OFFCPU 2
#define BPF_NAMED_SCHED 3
#define BPF_NAMED_BLOCK 4
#define BPF_NAMED_LOCK 5
#define BPF_NAMED_TCP_RETRANS 6
#define BPF_NAMED_TCP_CONNECT 7
#define BPF_NAMED_TCP_RTT 8
#define BPF_NAMED_TCP_RESET 9
#define BPF_NAMED_DNS 10
#define BPF_NAMED_SYSCALL 11
#define BPF_NAMED_LIFE 12
#define BPF_NAMED_FD 13
#define BPF_NAMED_IO 14
#define BPF_NAMED_EXEC 15
#define BPF_NAMED_FILE 16
#define BPF_NAMED_NET 17
#define BPF_NAMED_PRIV 18
#define BPF_NAMED_CGROUP_MEM 19

struct bpf_arg_cfg {
	__u8 kind;
	__u8 filter;
	__u8 reg;
	__u8 field;
	__u8 size_src;
	__u8 size_reg;
	__u8 from_saved;
	__u8 pad_arg;
	__u16 max;
	__u16 pad_max;
	char value[BPF_STR_MAX];
	char allow0[BPF_STR_MAX];
	char allow1[BPF_STR_MAX];
	char allow2[BPF_STR_MAX];
	char allow3[BPF_STR_MAX];
};

struct bpf_probe_cfg {
	__u32 nargs;
	__u32 latency;
	struct bpf_arg_cfg args[BPF_ARG_MAX];
	char comm[BPF_COMM_MAX][16];
	__u32 mode;
	__u32 pair_id;
	__u32 has_when;
	__u32 save_mask;
	__s64 retval_gt;
};

/* Entry/return latency start: key is (tid, probe_id) so nested probes do not collide. */
struct bpf_lat_start_key {
	__u64 tid;
	__u32 probe_id;
	__u32 pad;
};

struct bpf_lat_start {
	__u64 ts;
	char l0[BPF_STR_MAX];
	char l1[BPF_STR_MAX];
	char l2[BPF_STR_MAX];
	char l3[BPF_STR_MAX];
	char l4[BPF_STR_MAX];
	char l5[BPF_STR_MAX];
};

/* Histogram key mirrors generic counter labels (probe_id + arg values). */
struct bpf_lat_hist_key {
	__u32 probe_id;
	__u32 bucket;
	char l0[BPF_STR_MAX];
	char l1[BPF_STR_MAX];
	char l2[BPF_STR_MAX];
	char l3[BPF_STR_MAX];
	char l4[BPF_STR_MAX];
	char l5[BPF_STR_MAX];
};

struct bpf_lat_sum_key {
	__u32 probe_id;
	__u32 pad;
	char l0[BPF_STR_MAX];
	char l1[BPF_STR_MAX];
	char l2[BPF_STR_MAX];
	char l3[BPF_STR_MAX];
	char l4[BPF_STR_MAX];
	char l5[BPF_STR_MAX];
};

struct bpf_hist_key {
	__u64 cgroup;
	__u32 bucket;
	__u32 pad;
	char comm[16];
	char extra[32];
	char dev[16];
};

struct bpf_sum_key {
	__u64 cgroup;
	char comm[16];
	char extra[32];
	char dev[16];
};

struct bpf_ctr_key {
	__u64 cgroup;
	__u32 dport;
	__u32 changed;
	char comm[16];
	char extra[BPF_STR_MAX];
	char parent[16];
};

struct bpf_event_key {
	__u32 probe_id;
	__u32 pad;
	char l0[BPF_STR_MAX];
	char l1[BPF_STR_MAX];
	char l2[BPF_STR_MAX];
	char l3[BPF_STR_MAX];
	char l4[BPF_STR_MAX];
	char l5[BPF_STR_MAX];
};

struct bpf_drop_val {
	__u64 trunc;
	__u64 lru;
	__u64 live;
	__u64 ringbuf_full;
	__u64 user_read_fail;
	__u64 stash_miss;
};

/* Entry registers kept until the paired uretprobe. Key is (tid, pair_id). */
struct bpf_stash_key {
	__u64 tid;
	__u32 pair_id;
	__u32 pad;
};

struct bpf_stash_val {
	__u64 saved[BPF_ARG_MAX];
};

/*
 * One captured call. Buffers are length-delimited (not C strings). Userspace
 * must honor len[] and must not strlen() data[].
 */
struct bpf_capture_event {
	__u64 ts;
	__u64 pid_tgid;
	__s64 retval;
	__u32 probe_id;
	__u32 dir;
	__u32 len[BPF_CAPTURE_BUFS];
	__u32 arg_index[BPF_CAPTURE_BUFS];
	__u32 trunc;
	__u32 nbuf;
	char comm[16];
	char data[BPF_CAPTURE_BUFS][BPF_CAPTURE_MAX];
};

struct bpf_sched_cfg {
	__u32 runq_on;
	__u32 off_on;
	__u32 cgroup_labels;
	__u32 pad;
	char runq_comm[BPF_COMM_MAX][16];
	char off_comm[BPF_COMM_MAX][16];
};

struct bpf_block_cfg {
	__u32 on;
	__u32 cgroup_labels;
	char comm[BPF_COMM_MAX][16];
};

struct bpf_tcp_cfg {
	__u32 retrans_on;
	__u32 connect_on;
	__u32 rtt_on;
	__u32 reset_on;
	__u32 dport_on;
	__u32 cgroup_labels;
	char retrans_comm[BPF_COMM_MAX][16];
	char connect_comm[BPF_COMM_MAX][16];
	char rtt_comm[BPF_COMM_MAX][16];
	char reset_comm[BPF_COMM_MAX][16];
};

struct bpf_proc_cfg {
	__u32 syscall_on;
	__u32 life_on;
	__u32 fd_on;
	__u32 io_on;
	__u32 exec_on;
	__u32 file_on;
	__u32 net_on;
	__u32 priv_on;
	__u32 cgroup_labels;
	__u32 nprefix;
	__u32 sys_nr[BPF_SYSCALL_SLOTS];
	char prefix[BPF_PREFIX_MAX][BPF_STR_MAX];
	char comm_allow[BPF_COMM_MAX][16];
	char sys_comm[BPF_COMM_MAX][16];
	char life_comm[BPF_COMM_MAX][16];
	char io_comm[BPF_COMM_MAX][16];
	char exec_comm[BPF_COMM_MAX][16];
	char file_comm[BPF_COMM_MAX][16];
	char net_comm[BPF_COMM_MAX][16];
	char priv_comm[BPF_COMM_MAX][16];
	__u8 exec_filter_op;
	__u8 file_filter_op;
	__u8 dns_filter_op;
	__u8 pad_op;
	char exec_filter[BPF_STR_MAX];
	char file_filter[BPF_STR_MAX];
	__u32 openat_nr;
	__u32 read_nr;
	__u32 write_nr;
	__u32 close_nr;
	__u32 connect_nr;
	__u32 accept_nr;
	__u32 accept4_nr;
};

struct bpf_dns_cfg {
	__u32 on;
	__u32 cgroup_labels;
	__u8 filter_op;
	__u8 pad[3];
	char value[BPF_STR_MAX];
	char comm[BPF_COMM_MAX][16];
};

struct bpf_lock_cfg {
	char comm[BPF_COMM_MAX][16];
};

struct bpf_ts_val {
	__u64 ts;
	__u64 cgroup;
	char comm[16];
	char extra[32];
};

struct bpf_sum_val {
	__u64 sum_ns;
	__u64 count;
};
