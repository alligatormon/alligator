#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_types.h"
#include "map_help.h"

char LICENSE[] SEC("license") = "GPL";

/* VM_FAULT_* from linux/mm_types.h — stable bits across recent kernels. */
#define BPF_VM_FAULT_MAJOR 0x000004
/* OOM|SIGBUS|SIGSEGV|HWPOISON|HWPOISON_LARGE — do not count as minor/major. */
#define BPF_VM_FAULT_ERROR 0x000073

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_mem_cfg);
} mem_cfg SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} fault_ctr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4096);
	__type(key, struct bpf_ctr_key);
	__type(value, __u64);
} oom_ctr SEC(".maps");

static __always_inline struct bpf_mem_cfg *cfg(void)
{
	__u32 key = 0;
	return bpf_map_lookup_elem(&mem_cfg, &key);
}

static __noinline void bump_fault(const char *comm, const char *type, __u64 cgroup)
{
	struct bpf_ctr_key key = {};
	__u64 *v;
	__u64 one = 1;

	__builtin_memcpy(key.comm, comm, 16);
	bpf_copy_str(key.extra, type, 64);
	key.cgroup = cgroup;
	v = bpf_map_lookup_elem(&fault_ctr, &key);
	if (v)
		__sync_fetch_and_add(v, 1);
	else
		bpf_map_update_elem(&fault_ctr, &key, &one, BPF_ANY);
}

static __noinline void bump_oom(const char *comm, __u64 cgroup)
{
	struct bpf_ctr_key key = {};
	__u64 *v;
	__u64 one = 1;

	__builtin_memcpy(key.comm, comm, 16);
	key.cgroup = cgroup;
	v = bpf_map_lookup_elem(&oom_ctr, &key);
	if (v)
		__sync_fetch_and_add(v, 1);
	else
		bpf_map_update_elem(&oom_ctr, &key, &one, BPF_ANY);
}

/* Manual attach in userspace (like udp). Return value carries VM_FAULT_*. */
SEC("kretprobe/handle_mm_fault")
int BPF_KRETPROBE(on_mm_fault, long ret)
{
	struct bpf_mem_cfg *c = cfg();
	char comm[16] = {};
	__u64 cg = 0;
	const char *type;

	if (!c || !c->fault_on)
		return 0;
	if (ret < 0 || (ret & BPF_VM_FAULT_ERROR))
		return 0;
	bpf_get_current_comm(comm, sizeof(comm));
	if (!bpf_comm_ok(c->fault_comm, comm))
		return 0;
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	type = (ret & BPF_VM_FAULT_MAJOR) ? "major" : "minor";
	bump_fault(comm, type, cg);
	return 0;
}

/* mark_victim runs in the victim's context; current comm is the killed task. */
SEC("tracepoint/oom/mark_victim")
int on_oom_mark(void *ctx)
{
	struct bpf_mem_cfg *c = cfg();
	char comm[16] = {};
	__u64 cg = 0;

	(void)ctx;
	if (!c || !c->oom_on)
		return 0;
	bpf_get_current_comm(comm, sizeof(comm));
	if (!bpf_comm_ok(c->oom_comm, comm))
		return 0;
	if (c->cgroup_labels)
		cg = bpf_get_current_cgroup_id();
	bump_oom(comm, cg);
	return 0;
}
