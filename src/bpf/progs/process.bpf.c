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
	__type(value, struct bpf_proc_cfg);
} proc_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, __u64);
	__type(value, struct bpf_ts_val);
} sys_start SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_hist_key);
	__type(value, __u64);
} sys_hist SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_sum_key);
	__type(value, struct bpf_sum_val);
} sys_sum SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} life_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} fd_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} io_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} exec_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} file_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} net_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} priv_ctr SEC(".maps");

static __always_inline struct bpf_proc_cfg *pcfg(void)
{
	__u32 key = 0;
	return bpf_map_lookup_elem(&proc_cfg, &key);
}

static __always_inline int sys_allowed(struct bpf_proc_cfg *c, __u32 id)
{
	if (c->sys_nr[0] == id || c->sys_nr[1] == id || c->sys_nr[2] == id || c->sys_nr[3] == id ||
	    c->sys_nr[4] == id || c->sys_nr[5] == id || c->sys_nr[6] == id || c->sys_nr[7] == id)
		return 1;
	return 0;
}

static __always_inline int sys_feature(struct bpf_proc_cfg *c, __u32 id)
{
	if (c->file_on && id == c->openat_nr)
		return 1;
	if (c->io_on && (id == c->read_nr || id == c->write_nr))
		return 1;
	if (c->fd_on && (id == c->openat_nr || id == c->close_nr))
		return 1;
	if (c->net_on && (id == c->connect_nr || id == c->accept_nr || id == c->accept4_nr))
		return 1;
	return 0;
}

static __always_inline int comm_allowed(struct bpf_proc_cfg *c, const char *comm)
{
	int i;

#pragma unroll
	for (i = 0; i < 8; i++) {
		if (bpf_str_eq16(comm, c->comm_allow[i]))
			return 1;
	}
	return 0;
}

static __always_inline int prefix_ok(struct bpf_proc_cfg *c, const char *text)
{
	if (!c->nprefix)
		return 0;
	if (bpf_str_prefix(text, c->prefix[0]))
		return 1;
	if (c->nprefix > 1 && bpf_str_prefix(text, c->prefix[1]))
		return 1;
	if (c->nprefix > 2 && bpf_str_prefix(text, c->prefix[2]))
		return 1;
	if (c->nprefix > 3 && bpf_str_prefix(text, c->prefix[3]))
		return 1;
	return 0;
}

struct bpf_ctr_arg {
	const char *parent;
	__u64 cgroup;
	__u32 changed;
};

static __noinline void bump(void *map, const char *comm, const char *extra, struct bpf_ctr_arg *a)
{
	struct bpf_ctr_key key = {};
	__u64 *v;
	__u64 one = 1;

	__builtin_memcpy(key.comm, comm, 16);
	bpf_copy_str(key.extra, extra, 64);
	if (a->parent)
		__builtin_memcpy(key.parent, a->parent, 16);
	key.changed = a->changed;
	key.cgroup = a->cgroup;
	v = bpf_map_lookup_elem(map, &key);
	if (v)
		__sync_fetch_and_add(v, 1);
	else
		bpf_map_update_elem(map, &key, &one, BPF_ANY);
}

static __noinline void add_gauge(void *map, const char *comm, const char *extra, __s64 delta, __u64 cgroup)
{
	struct bpf_ctr_key key = {};
	__u64 *v;

	__builtin_memcpy(key.comm, comm, 16);
	bpf_copy_str(key.extra, extra, 64);
	key.cgroup = cgroup;
	v = bpf_map_lookup_elem(map, &key);
	if (!v) {
		__u64 init;
		if (delta <= 0)
			return;
		init = (__u64)delta;
		bpf_map_update_elem(map, &key, &init, BPF_ANY);
		return;
	}
	if (delta < 0 && *v == 0)
		return;
	__sync_fetch_and_add(v, (__u64)delta);
}

SEC("tracepoint/raw_syscalls/sys_enter")
int sys_enter(struct trace_event_raw_sys_enter *ctx)
{
	struct bpf_proc_cfg *c = pcfg();
	__u32 id;
	__u64 tid;
	struct bpf_ts_val val = {};
	char path[64] = {};
	char comm[16] = {};
	__u64 cg = 0;

	if (!c)
		return 0;
	id = (__u32)BPF_CORE_READ(ctx, id);
	if (!sys_allowed(c, id) && !sys_feature(c, id))
		return 0;
	tid = bpf_get_current_pid_tgid();
	bpf_get_current_comm(comm, sizeof(comm));
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	if (c->syscall_on && sys_allowed(c, id) && bpf_comm_ok(c->sys_comm, comm)) {
		val.ts = bpf_ktime_get_ns();
		val.cgroup = cg;
		__builtin_memcpy(val.comm, comm, 16);
		bpf_map_update_elem(&sys_start, &tid, &val, BPF_ANY);
	}
	if (c->file_on && id == c->openat_nr && bpf_comm_ok(c->file_comm, comm)) {
		const char *user_path = (const char *)BPF_CORE_READ(ctx, args[1]);
		if (bpf_probe_read_user_str(path, sizeof(path), user_path) > 0 && prefix_ok(c, path))
			{
				struct bpf_ctr_arg a = {};
				a.cgroup = cg;
				bump(&file_ctr, comm, path, &a);
			}
	}
	if (c->net_on && id == c->connect_nr && bpf_comm_ok(c->net_comm, comm)) {
		char ev[64] = "connect";
		struct bpf_ctr_arg a = {};
		a.cgroup = cg;
		bump(&net_ctr, comm, ev, &a);
	}
	if (c->net_on && (id == c->accept_nr || id == c->accept4_nr) && bpf_comm_ok(c->net_comm, comm)) {
		char ev[64] = "accept";
		struct bpf_ctr_arg a = {};
		a.cgroup = cg;
		bump(&net_ctr, comm, ev, &a);
	}
	return 0;
}

SEC("tracepoint/raw_syscalls/sys_exit")
int sys_exit(struct trace_event_raw_sys_exit *ctx)
{
	struct bpf_proc_cfg *c = pcfg();
	__u64 tid;
	__u32 id;
	long ret;
	struct bpf_ts_val *start;
	char comm[16] = {};
	__u64 now;

	if (!c)
		return 0;
	id = (__u32)BPF_CORE_READ(ctx, id);
	if (!sys_allowed(c, id) && !sys_feature(c, id))
		return 0;
	tid = bpf_get_current_pid_tgid();
	ret = BPF_CORE_READ(ctx, ret);
	bpf_get_current_comm(comm, sizeof(comm));
	now = bpf_ktime_get_ns();
	start = bpf_map_lookup_elem(&sys_start, &tid);
	if (c->syscall_on && sys_allowed(c, id) && start && now > start->ts) {
		char num[32] = {};
		int n = 0;
		int j;
		__u32 v = id;
		char tmp[10];
		__u64 saved_cg = start->cgroup;
		if (!v) {
			num[0] = '0';
		} else {
			for (j = 0; j < 10; j++) {
				tmp[n] = '0' + (v % 10);
				n++;
				v /= 10;
				if (!v)
					break;
			}
			for (j = 0; j < 10; j++) {
				if (j >= n)
					break;
				num[j] = tmp[n - 1 - j];
			}
		}
		if (bpf_comm_ok(c->sys_comm, comm)) {
			struct bpf_hist_arg ha = {};
			ha.cgroup = saved_cg;
			ha.delta = now - start->ts;
			bpf_hist_bump(&sys_hist, &sys_sum, comm, num, &ha);
		}
		bpf_map_delete_elem(&sys_start, &tid);
		start = NULL;
	}
	if (c->io_on && bpf_comm_ok(c->io_comm, comm) && ret > 0 && (id == c->read_nr || id == c->write_nr)) {
		char op[64] = {};
		__u64 cg = 0;
		if (id == c->write_nr)
			__builtin_memcpy(op, "write", 6);
		else
			__builtin_memcpy(op, "read", 5);
		if (c->cgroup_labels)
			cg = bpf_get_current_cgroup_id();
		add_gauge(&io_ctr, comm, op, ret, cg);
	}
	if (c->fd_on && comm_allowed(c, comm)) {
		if (id == c->openat_nr && ret >= 0) {
			char ev[64] = "fd";
			add_gauge(&fd_ctr, comm, ev, 1, 0);
		}
		if (id == c->close_nr && ret == 0) {
			char ev[64] = "fd";
			add_gauge(&fd_ctr, comm, ev, -1, 0);
		}
	}
	return 0;
}

static __noinline void life(const char *event)
{
	struct bpf_proc_cfg *c = pcfg();
	char comm[16] = {};
	char ev[64] = {};
	__u64 cg = 0;

	if (!c || !c->life_on || !event)
		return;
	bpf_copy_str(ev, event, 64);
	bpf_get_current_comm(comm, sizeof(comm));
	if (!bpf_comm_ok(c->life_comm, comm))
		return;
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	{
		struct bpf_ctr_arg a = {};
		a.cgroup = cg;
		bump(&life_ctr, comm, ev, &a);
	}
}

SEC("tracepoint/sched/sched_process_fork")
int on_fork(struct trace_event_raw_sched_process_fork *ctx)
{
	char ev[64] = "fork";
	(void)ctx;
	life(ev);
	return 0;
}

SEC("tracepoint/sched/sched_process_exit")
int on_exit(void *ctx)
{
	char ev[64] = "exit";
	(void)ctx;
	life(ev);
	return 0;
}

SEC("tracepoint/sched/sched_process_exec")
int on_exec(struct trace_event_raw_sched_process_exec *ctx)
{
	struct bpf_proc_cfg *c = pcfg();
	char comm[16] = {};
	char parent[16] = {};
	char file[64] = {};
	struct task_struct *task;
	struct task_struct *par;
	unsigned long off;
	__u64 cg = 0;

	char ev[64] = "exec";
	life(ev);
	if (!c || !c->exec_on)
		return 0;
	bpf_get_current_comm(comm, sizeof(comm));
	if (!bpf_comm_ok(c->exec_comm, comm))
		return 0;
	task = (struct task_struct *)bpf_get_current_task();
	par = BPF_CORE_READ(task, real_parent);
	bpf_core_read_str(parent, sizeof(parent), &par->comm);
	off = BPF_CORE_READ(ctx, __data_loc_filename) & 0xffff;
	bpf_probe_read_kernel_str(file, sizeof(file), (char *)ctx + off);
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	{
		int match = 0;
		if (c->exec_filter_op == 1 || c->exec_filter_op == 4)
			match = bpf_str_eq(file, c->exec_filter);
		else if (c->exec_filter_op == 2)
			match = bpf_str_prefix(file, c->exec_filter);
		else if (c->exec_filter_op == 3)
			match = bpf_str_suffix(file, c->exec_filter);
		{
			struct bpf_ctr_arg a = {};
			a.parent = parent;
			a.cgroup = cg;
			bump(&exec_ctr, comm, match ? file : NULL, &a);
		}
	}
	return 0;
}

SEC("kprobe/commit_creds")
int BPF_KPROBE(on_commit_creds, struct cred *new)
{
	struct bpf_proc_cfg *c = pcfg();
	struct task_struct *task;
	const struct cred *oldc;
	__u32 ou, oe, nu, ne;
	char comm[16] = {};
	__u64 cg = 0;

	if (!c || !c->priv_on || !new)
		return 0;
	task = (struct task_struct *)bpf_get_current_task();
	oldc = BPF_CORE_READ(task, real_cred);
	ou = BPF_CORE_READ(oldc, uid.val);
	oe = BPF_CORE_READ(oldc, euid.val);
	nu = BPF_CORE_READ(new, uid.val);
	ne = BPF_CORE_READ(new, euid.val);
	bpf_get_current_comm(comm, sizeof(comm));
	if (!bpf_comm_ok(c->priv_comm, comm))
		return 0;
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	{
		struct bpf_ctr_arg a = {};
		a.changed = ou != nu || oe != ne;
		a.cgroup = cg;
		bump(&priv_ctr, comm, NULL, &a);
	}
	return 0;
}
