#pragma once

#include <jansson.h>
#include <stddef.h>
#include <stdint.h>
#include "bpf/bpf_types.h"

typedef struct bpf_plain_tok {
	const char *s;
	int start;
	int end;
	int semicolon;
} bpf_plain_tok;

typedef struct bpf_arg {
	int index;
	int kind;
	int filter;
	int field;
	char label[32];
	char value[BPF_STR_MAX];
	char allow[BPF_ALLOW_MAX][BPF_STR_MAX];
	int nallow;
} bpf_arg;

typedef struct bpf_probe {
	int kind;
	int named;
	int enabled;
	int generic_slot;
	char symbol[256];
	char binary[256];
	char name[64];
	bpf_arg args[BPF_ARG_MAX];
	int nargs;
	char prefix[BPF_PREFIX_MAX][BPF_STR_MAX];
	int nprefix;
	char comm[BPF_COMM_MAX][16];
	int ncomm;
	char syscalls[BPF_SYSCALL_SLOTS][16];
	int nsys;
	int want_dport;
	int latency;
	int filter_op;
	char filter_value[BPF_STR_MAX];
} bpf_probe;

int bpf_filter_match(int op, const char *rule, const char *text);
int bpf_filter_allow(const char *const *list, size_t n, const char *text);
size_t bpf_str_copy(char *dst, size_t cap, const char *src, int *truncated);
int bpf_hist_bucket(uint64_t ns);
double bpf_hist_le(int bucket);
int bpf_lru_note(uint32_t *len, uint32_t cap, uint64_t *dropped, int is_new);
int bpf_kernel_at_least(const char *release, int maj, int min);
int bpf_syscall_nr(const char *name, const char *machine);
const char *bpf_syscall_name(int nr, const char *machine);
int bpf_path_label(const char *label);
int bpf_event_label_ok(const char *label, int op, const char *rule, const char *text,
	const char *const *allow, size_t nallow);
int bpf_cgroup_parse(const char *path, char *pod, size_t pod_n, char *ns, size_t ns_n,
	char *container, size_t container_n);
int bpf_named_id(const char *name);
const char *bpf_named_metric(int named);
int bpf_named_is_hist(int named);

json_t *bpf_plain_parse(const bpf_plain_tok *toks, uint64_t n, uint64_t *idx);

int bpf_config_apply(json_t *obj, int clear);
void bpf_config_get(json_t *dst);
int bpf_probe_count(void);
int bpf_probe_on_name(const char *name);
const bpf_probe *bpf_probe_at(int i);
int bpf_cgroup_labels_on(void);
int bpf_any_enabled(void);

void bpf_hist_add_bucket(const char *metric, const char *comm, const char *extra, const char *extra_key,
	const char *dev, uint64_t cgroup, uint32_t bucket, uint64_t count);
void bpf_hist_set_sum(const char *metric, const char *comm, const char *extra, const char *dev,
	uint64_t cgroup, uint64_t sum_ns, uint64_t count);
void bpf_hist_observe(const char *metric, const char *comm, const char *extra, const char *extra_key,
	const char *dev, uint64_t cgroup, uint64_t ns);
void bpf_counter_set(const char *metric, const char *comm, const char *event, const char *path,
	const char *parent, uint64_t cgroup, int has_dport, uint32_t dport, int has_changed,
	uint32_t changed, uint64_t value);
void bpf_generic_set(int slot, const char *l0, const char *l1, const char *l2, const char *l3, const char *l4, const char *l5, uint64_t count);
void bpf_generic_hist_bucket(int slot, const char *l0, const char *l1, const char *l2, const char *l3, const char *l4, const char *l5,
	uint32_t bucket, uint64_t count);
void bpf_generic_hist_sum(int slot, const char *l0, const char *l1, const char *l2, const char *l3, const char *l4, const char *l5,
	uint64_t sum_ns, uint64_t count);
void bpf_drop_add(const char *probe, const char *reason, uint64_t n);
void bpf_skip_set(const char *probe, int on);
void bpf_series_reset(void);

void bpf_cgroup_remember(const char *path, const char *pod, const char *ns, const char *container);
void bpf_pressure_sample(const char *path, const char *kind, double avg10, double avg60, double avg300, double total_sec);

void bpf_scrape(void);
void bpf_cleanup(void);

int bpf_loader_sync(void);
void bpf_loader_shutdown(void);
void bpf_loader_poll(void);
int bpf_loader_attached(void);
int bpf_loader_selftest(void);
