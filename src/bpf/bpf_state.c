#include "bpf/bpf.h"
#include "main.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "metric/labels.h"
#include "metric/metric_types.h"
#include "dstructures/ht.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __linux__
#include <dirent.h>
#include "system/linux/pressure.h"
#endif

extern aconf *ac;

static bpf_probe probes[BPF_PROBE_SLOTS];
static int probe_n;
static int cgroup_labels;
static int generic_n;
static context_arg *bpf_carg;
static char *metric_ns;
static char *applied_json;
static int logged_off;

struct hist_row {
	int used;
	char metric[64];
	char comm[16];
	char extra[BPF_STR_MAX];
	char extra_key[16];
	char dev[16];
	uint64_t cgroup;
	uint64_t raw[BPF_HIST_BUCKETS];
	uint64_t sum_ns;
	uint64_t count;
};

struct ctr_row {
	int used;
	char metric[64];
	char comm[16];
	char event[BPF_STR_MAX];
	char path[BPF_STR_MAX];
	char parent[16];
	uint64_t cgroup;
	int has_dport;
	uint32_t dport;
	int has_changed;
	uint32_t changed;
	uint64_t value;
};

struct drop_row {
	int used;
	char probe[64];
	char reason[32];
	uint64_t n;
};

struct skip_row {
	int used;
	char probe[64];
	int on;
};

struct cg_row {
	int used;
	uint64_t id;
	char path[256];
	char pod[64];
	char ns[64];
	char container[128];
};

struct gen_row {
	int used;
	int slot;
	char l[6][BPF_STR_MAX];
	uint64_t count;
};

struct gen_hist_row {
	int used;
	int slot;
	char l[6][BPF_STR_MAX];
	uint64_t raw[BPF_HIST_BUCKETS];
	uint64_t sum_ns;
	uint64_t count;
};

static struct hist_row hists[BPF_SERIES_MAX];
static struct ctr_row ctrs[BPF_SERIES_MAX];
static struct gen_row gens[BPF_SERIES_MAX];
static struct gen_hist_row gen_hists[BPF_SERIES_MAX];
static struct drop_row drops[64];
static struct skip_row skips[BPF_PROBE_SLOTS];
static struct cg_row cgroups[512];
static int cg_n;
static uint32_t hist_lru;
static uint32_t ctr_lru;
static uint32_t gen_lru;
static uint32_t gen_hist_lru;

static void copy_cap(char *dst, size_t cap, const char *src)
{
	size_t n;

	if (!dst || !cap)
		return;
	if (!src)
		src = "";
	n = strlen(src);
	if (n >= cap)
		n = cap - 1;
	memcpy(dst, src, n);
	dst[n] = 0;
}

static int same_str(const char *a, const char *b)
{
	if (!a)
		a = "";
	if (!b)
		b = "";
	return !strcmp(a, b);
}

void bpf_series_reset(void)
{
	memset(hists, 0, sizeof(hists));
	memset(ctrs, 0, sizeof(ctrs));
	memset(gens, 0, sizeof(gens));
	memset(gen_hists, 0, sizeof(gen_hists));
	memset(drops, 0, sizeof(drops));
	hist_lru = 0;
	ctr_lru = 0;
	gen_lru = 0;
	gen_hist_lru = 0;
}

static struct hist_row *hist_find(const char *metric, const char *comm, const char *extra, const char *dev, uint64_t cgroup, int create)
{
	int i;
	struct hist_row *free_row = NULL;

	for (i = 0; i < BPF_SERIES_MAX; i++) {
		if (!hists[i].used) {
			if (!free_row)
				free_row = &hists[i];
			continue;
		}
		if (same_str(hists[i].metric, metric) && same_str(hists[i].comm, comm) &&
		    same_str(hists[i].extra, extra) && same_str(hists[i].dev, dev) &&
		    hists[i].cgroup == cgroup)
			return &hists[i];
	}
	if (!create)
		return NULL;
	if (!free_row) {
		/* Recycle the oldest slot so later probes in the poll order are not starved. */
		bpf_drop_add(metric ? metric : "hist", "lru", 1);
		free_row = &hists[hist_lru % BPF_SERIES_MAX];
		hist_lru = (hist_lru + 1) % BPF_SERIES_MAX;
	}
	memset(free_row, 0, sizeof(*free_row));
	free_row->used = 1;
	copy_cap(free_row->metric, sizeof(free_row->metric), metric);
	copy_cap(free_row->comm, sizeof(free_row->comm), comm);
	copy_cap(free_row->extra, sizeof(free_row->extra), extra);
	copy_cap(free_row->dev, sizeof(free_row->dev), dev);
	free_row->cgroup = cgroup;
	return free_row;
}

void bpf_hist_add_bucket(const char *metric, const char *comm, const char *extra, const char *extra_key,
	const char *dev, uint64_t cgroup, uint32_t bucket, uint64_t count)
{
	struct hist_row *row;

	if (!metric || bucket >= BPF_HIST_BUCKETS)
		return;
	row = hist_find(metric, comm, extra, dev, cgroup, 1);
	if (!row)
		return;
	if (extra_key && extra_key[0])
		copy_cap(row->extra_key, sizeof(row->extra_key), extra_key);
	row->raw[bucket] = count;
}

void bpf_hist_set_sum(const char *metric, const char *comm, const char *extra, const char *dev,
	uint64_t cgroup, uint64_t sum_ns, uint64_t count)
{
	struct hist_row *row = hist_find(metric, comm, extra, dev, cgroup, 1);
	if (!row)
		return;
	row->sum_ns = sum_ns;
	row->count = count;
}

void bpf_hist_observe(const char *metric, const char *comm, const char *extra, const char *extra_key,
	const char *dev, uint64_t cgroup, uint64_t ns)
{
	struct hist_row *row;
	int b;

	row = hist_find(metric, comm, extra, dev, cgroup, 1);
	if (!row)
		return;
	if (extra_key && extra_key[0])
		copy_cap(row->extra_key, sizeof(row->extra_key), extra_key);
	b = bpf_hist_bucket(ns);
	row->raw[b]++;
	row->sum_ns += ns;
	row->count++;
}

static int ctr_same(const struct ctr_row *r, const char *metric, const char *comm, const char *event,
	const char *path, const char *parent, uint64_t cgroup, int has_dport, uint32_t dport,
	int has_changed, uint32_t changed)
{
	if (!same_str(r->metric, metric) || !same_str(r->comm, comm) || !same_str(r->event, event))
		return 0;
	if (!same_str(r->path, path) || !same_str(r->parent, parent) || r->cgroup != cgroup)
		return 0;
	if (r->has_dport != has_dport || (has_dport && r->dport != dport))
		return 0;
	if (r->has_changed != has_changed || (has_changed && r->changed != changed))
		return 0;
	return 1;
}

void bpf_counter_set(const char *metric, const char *comm, const char *event, const char *path,
	const char *parent, uint64_t cgroup, int has_dport, uint32_t dport, int has_changed,
	uint32_t changed, uint64_t value)
{
	int i;
	struct ctr_row *free_row = NULL;

	if (!metric)
		return;
	for (i = 0; i < BPF_SERIES_MAX; i++) {
		if (!ctrs[i].used) {
			if (!free_row)
				free_row = &ctrs[i];
			continue;
		}
		if (ctr_same(&ctrs[i], metric, comm, event, path, parent, cgroup, has_dport, dport, has_changed, changed)) {
			ctrs[i].value = value;
			return;
		}
	}
	if (!free_row) {
		bpf_drop_add(metric, "lru", 1);
		free_row = &ctrs[ctr_lru % BPF_SERIES_MAX];
		ctr_lru = (ctr_lru + 1) % BPF_SERIES_MAX;
	}
	memset(free_row, 0, sizeof(*free_row));
	free_row->used = 1;
	copy_cap(free_row->metric, sizeof(free_row->metric), metric);
	copy_cap(free_row->comm, sizeof(free_row->comm), comm);
	copy_cap(free_row->event, sizeof(free_row->event), event);
	copy_cap(free_row->path, sizeof(free_row->path), path);
	copy_cap(free_row->parent, sizeof(free_row->parent), parent);
	free_row->cgroup = cgroup;
	free_row->has_dport = has_dport;
	free_row->dport = dport;
	free_row->has_changed = has_changed;
	free_row->changed = changed;
	free_row->value = value;
}

void bpf_generic_set(int slot, const char *l0, const char *l1, const char *l2, const char *l3, const char *l4, const char *l5, uint64_t count)
{
	int i;
	int n;
	const char *ls[6];
	struct gen_row *row = NULL;

	ls[0] = l0; ls[1] = l1; ls[2] = l2; ls[3] = l3; ls[4] = l4; ls[5] = l5;
	for (i = 0; i < BPF_SERIES_MAX; i++) {
		if (!gens[i].used)
			continue;
		if (gens[i].slot == slot && !strcmp(gens[i].l[0], l0 ? l0 : "") &&
		    !strcmp(gens[i].l[1], l1 ? l1 : "") && !strcmp(gens[i].l[2], l2 ? l2 : "") &&
		    !strcmp(gens[i].l[3], l3 ? l3 : "") && !strcmp(gens[i].l[4], l4 ? l4 : "") &&
		    !strcmp(gens[i].l[5], l5 ? l5 : "")) {
			gens[i].count = count;
			return;
		}
	}
	for (i = 0; i < BPF_SERIES_MAX; i++) {
		if (gens[i].used)
			continue;
		row = &gens[i];
		break;
	}
	if (!row) {
		bpf_drop_add("generic", "lru", 1);
		row = &gens[gen_lru % BPF_SERIES_MAX];
		gen_lru = (gen_lru + 1) % BPF_SERIES_MAX;
	}
	memset(row, 0, sizeof(*row));
	row->used = 1;
	row->slot = slot;
	for (n = 0; n < 6; n++)
		copy_cap(row->l[n], BPF_STR_MAX, ls[n]);
	row->count = count;
}

static int gen_hist_same(const struct gen_hist_row *r, int slot, const char *const ls[6])
{
	int n;

	if (r->slot != slot)
		return 0;
	for (n = 0; n < 6; n++) {
		if (!same_str(r->l[n], ls[n] ? ls[n] : ""))
			return 0;
	}
	return 1;
}

static struct gen_hist_row *gen_hist_find(int slot, const char *const ls[6], int create)
{
	int i;
	int n;
	struct gen_hist_row *free_row = NULL;

	for (i = 0; i < BPF_SERIES_MAX; i++) {
		if (!gen_hists[i].used) {
			if (!free_row)
				free_row = &gen_hists[i];
			continue;
		}
		if (gen_hist_same(&gen_hists[i], slot, ls))
			return &gen_hists[i];
	}
	if (!create)
		return NULL;
	if (!free_row) {
		bpf_drop_add("bpf_probe_latency_seconds", "lru", 1);
		free_row = &gen_hists[gen_hist_lru % BPF_SERIES_MAX];
		gen_hist_lru = (gen_hist_lru + 1) % BPF_SERIES_MAX;
	}
	memset(free_row, 0, sizeof(*free_row));
	free_row->used = 1;
	free_row->slot = slot;
	for (n = 0; n < 6; n++)
		copy_cap(free_row->l[n], BPF_STR_MAX, ls[n]);
	return free_row;
}

void bpf_generic_hist_bucket(int slot, const char *l0, const char *l1, const char *l2, const char *l3, const char *l4, const char *l5,
	uint32_t bucket, uint64_t count)
{
	const char *ls[6];
	struct gen_hist_row *row;

	if (bucket >= BPF_HIST_BUCKETS)
		return;
	ls[0] = l0; ls[1] = l1; ls[2] = l2; ls[3] = l3; ls[4] = l4; ls[5] = l5;
	row = gen_hist_find(slot, ls, 1);
	if (!row)
		return;
	row->raw[bucket] = count;
}

void bpf_generic_hist_sum(int slot, const char *l0, const char *l1, const char *l2, const char *l3, const char *l4, const char *l5,
	uint64_t sum_ns, uint64_t count)
{
	const char *ls[6];
	struct gen_hist_row *row;

	ls[0] = l0; ls[1] = l1; ls[2] = l2; ls[3] = l3; ls[4] = l4; ls[5] = l5;
	row = gen_hist_find(slot, ls, 1);
	if (!row)
		return;
	row->sum_ns = sum_ns;
	row->count = count;
}

void bpf_drop_add(const char *probe, const char *reason, uint64_t n)
{
	int i;

	if (!n)
		return;
	for (i = 0; i < 64; i++) {
		if (!drops[i].used)
			continue;
		if (same_str(drops[i].probe, probe) && same_str(drops[i].reason, reason)) {
			drops[i].n += n;
			return;
		}
	}
	for (i = 0; i < 64; i++) {
		if (drops[i].used)
			continue;
		drops[i].used = 1;
		copy_cap(drops[i].probe, sizeof(drops[i].probe), probe);
		copy_cap(drops[i].reason, sizeof(drops[i].reason), reason);
		drops[i].n = n;
		return;
	}
}

void bpf_skip_set(const char *probe, int on)
{
	int i;

	for (i = 0; i < BPF_PROBE_SLOTS; i++) {
		if (skips[i].used && same_str(skips[i].probe, probe)) {
			skips[i].on = on;
			return;
		}
	}
	if (!on)
		return;
	for (i = 0; i < BPF_PROBE_SLOTS; i++) {
		if (skips[i].used)
			continue;
		skips[i].used = 1;
		copy_cap(skips[i].probe, sizeof(skips[i].probe), probe);
		skips[i].on = 1;
		return;
	}
}

static void metric_ns_set(const char *name)
{
	free(metric_ns);
	metric_ns = NULL;
	if (name && name[0])
		metric_ns = strdup(name);
}

static void carg_sync_namespace(void)
{
	if (!bpf_carg)
		return;
	if (metric_ns && bpf_carg->namespace && !strcmp(metric_ns, bpf_carg->namespace)) {
		insert_namespace(metric_ns, 0, UINT64_MAX);
		return;
	}
	if (!metric_ns && !bpf_carg->namespace)
		return;
	free(bpf_carg->namespace);
	bpf_carg->namespace = NULL;
	if (!metric_ns)
		return;
	bpf_carg->namespace = strdup(metric_ns);
	insert_namespace(metric_ns, 0, UINT64_MAX);
}

static void ensure_carg(void)
{
	if (!ac)
		return;
	if (!bpf_carg) {
		bpf_carg = calloc(1, sizeof(*bpf_carg));
		if (!bpf_carg)
			return;
		bpf_carg->ttl = 300;
	}
	carg_sync_namespace();
}

static const struct cg_row *cg_lookup(uint64_t id)
{
	int i;

	if (!id)
		return NULL;
	for (i = 0; i < cg_n; i++) {
		if (cgroups[i].used && cgroups[i].id == id)
			return &cgroups[i];
	}
	return NULL;
}

void bpf_cgroup_remember(const char *path, const char *pod, const char *ns, const char *container)
{
	struct stat st;
	char pbuf[64], nbuf[64], cbuf[128];
	int i;
	uint64_t id = 0;

	if (!path || !path[0])
		return;
	bpf_cgroup_parse(path, pbuf, sizeof(pbuf), nbuf, sizeof(nbuf), cbuf, sizeof(cbuf));
	if (pod && pod[0])
		copy_cap(pbuf, sizeof(pbuf), pod);
	if (ns && ns[0])
		copy_cap(nbuf, sizeof(nbuf), ns);
	if (container && container[0])
		copy_cap(cbuf, sizeof(cbuf), container);
	if (!stat(path, &st) && st.st_ino)
		id = (uint64_t)st.st_ino;
	else if (ac && ac->system_sysfs) {
		char full[512];
		snprintf(full, sizeof(full), "%sfs/cgroup%s%s", ac->system_sysfs, path[0] == '/' ? "" : "/", path);
		if (!stat(full, &st) && st.st_ino)
			id = (uint64_t)st.st_ino;
	}
	for (i = 0; i < cg_n; i++) {
		if (cgroups[i].used && ((id && cgroups[i].id == id) || !strcmp(cgroups[i].path, path))) {
			cgroups[i].id = id ? id : cgroups[i].id;
			copy_cap(cgroups[i].path, sizeof(cgroups[i].path), path);
			if (pbuf[0])
				copy_cap(cgroups[i].pod, sizeof(cgroups[i].pod), pbuf);
			if (nbuf[0])
				copy_cap(cgroups[i].ns, sizeof(cgroups[i].ns), nbuf);
			if (cbuf[0])
				copy_cap(cgroups[i].container, sizeof(cgroups[i].container), cbuf);
			return;
		}
	}
	if (cg_n >= (int)(sizeof(cgroups) / sizeof(cgroups[0])))
		return;
	memset(&cgroups[cg_n], 0, sizeof(cgroups[cg_n]));
	cgroups[cg_n].used = 1;
	cgroups[cg_n].id = id;
	copy_cap(cgroups[cg_n].path, sizeof(cgroups[cg_n].path), path);
	copy_cap(cgroups[cg_n].pod, sizeof(cgroups[cg_n].pod), pbuf);
	copy_cap(cgroups[cg_n].ns, sizeof(cgroups[cg_n].ns), nbuf);
	copy_cap(cgroups[cg_n].container, sizeof(cgroups[cg_n].container), cbuf);
	cg_n++;
}

static void add_label(alligator_ht *lbl, char *name, const char *value)
{
	if (!lbl || !name || !value || !value[0])
		return;
	labels_hash_insert_nocache(lbl, name, (char *)value);
}

static void add_cgroup_labels(alligator_ht *lbl, uint64_t id)
{
	const struct cg_row *cg;
	char idbuf[32];

	if (!cgroup_labels || !id)
		return;
	cg = cg_lookup(id);
	if (!cg) {
		snprintf(idbuf, sizeof(idbuf), "%" PRIu64, id);
		add_label(lbl, "cgroup", idbuf);
		return;
	}
	add_label(lbl, "cgroup", cg->path[0] ? cg->path : NULL);
	add_label(lbl, "pod", cg->pod);
	add_label(lbl, "namespace", cg->ns);
	add_label(lbl, "container", cg->container);
}

static void emit_hist_row(struct hist_row *row)
{
	uint64_t acc = 0;
	uint64_t total = 0;
	int i;
	char le[32];
	char metric[80];
	double sum;

	if (!row->used || !bpf_carg)
		return;
	namespace_metric_family_set_prom_type(bpf_carg, row->metric, METRIC_TYPE_HISTOGRAM);
	for (i = 0; i < BPF_HIST_BUCKETS; i++)
		total += row->raw[i];
	if (!row->count)
		row->count = total;
	for (i = 0; i < BPF_HIST_LAST; i++) {
		alligator_ht *lbl = alligator_ht_init(NULL);
		acc += row->raw[i];
		snprintf(le, sizeof(le), "%g", bpf_hist_le(i));
		add_label(lbl, "le", le);
		add_label(lbl, "comm", row->comm);
		if (row->extra_key[0])
			add_label(lbl, row->extra_key, row->extra);
		add_label(lbl, "dev", row->dev);
		add_cgroup_labels(lbl, row->cgroup);
		snprintf(metric, sizeof(metric), "%s_bucket", row->metric);
		metric_add(metric, lbl, &acc, DATATYPE_UINT, bpf_carg);
	}
	{
		alligator_ht *lbl = alligator_ht_init(NULL);
		acc += row->raw[BPF_HIST_LAST];
		add_label(lbl, "le", "+Inf");
		add_label(lbl, "comm", row->comm);
		if (row->extra_key[0])
			add_label(lbl, row->extra_key, row->extra);
		add_label(lbl, "dev", row->dev);
		add_cgroup_labels(lbl, row->cgroup);
		snprintf(metric, sizeof(metric), "%s_bucket", row->metric);
		metric_add(metric, lbl, &acc, DATATYPE_UINT, bpf_carg);
	}
	sum = (double)row->sum_ns / 1e9;
	{
		alligator_ht *lbl = alligator_ht_init(NULL);
		add_label(lbl, "comm", row->comm);
		if (row->extra_key[0])
			add_label(lbl, row->extra_key, row->extra);
		add_label(lbl, "dev", row->dev);
		add_cgroup_labels(lbl, row->cgroup);
		snprintf(metric, sizeof(metric), "%s_sum", row->metric);
		metric_add(metric, lbl, &sum, DATATYPE_DOUBLE, bpf_carg);
	}
	{
		alligator_ht *lbl = alligator_ht_init(NULL);
		uint64_t count = row->count ? row->count : acc;
		add_label(lbl, "comm", row->comm);
		if (row->extra_key[0])
			add_label(lbl, row->extra_key, row->extra);
		add_label(lbl, "dev", row->dev);
		add_cgroup_labels(lbl, row->cgroup);
		snprintf(metric, sizeof(metric), "%s_count", row->metric);
		metric_add(metric, lbl, &count, DATATYPE_UINT, bpf_carg);
	}
}

static void emit_gen_hist_row(struct gen_hist_row *row)
{
	const bpf_probe *p;
	uint64_t acc = 0;
	uint64_t total = 0;
	int i;
	int a;
	char le[32];
	char metric[80];
	double sum;
	const char *name = "generic";

	if (!row->used || !bpf_carg)
		return;
	p = NULL;
	for (a = 0; a < probe_n; a++) {
		if (probes[a].generic_slot == row->slot) {
			p = &probes[a];
			break;
		}
	}
	if (p)
		name = p->name;
	namespace_metric_family_set_prom_type(bpf_carg, "bpf_probe_latency_seconds", METRIC_TYPE_HISTOGRAM);
	for (i = 0; i < BPF_HIST_BUCKETS; i++)
		total += row->raw[i];
	if (!row->count)
		row->count = total;
	for (i = 0; i < BPF_HIST_LAST; i++) {
		alligator_ht *lbl = alligator_ht_init(NULL);
		acc += row->raw[i];
		snprintf(le, sizeof(le), "%g", bpf_hist_le(i));
		add_label(lbl, "le", le);
		add_label(lbl, "probe", name);
		if (p) {
			for (a = 0; a < p->nargs && a < 6; a++)
				add_label(lbl, (char *)p->args[a].label, row->l[a]);
		}
		snprintf(metric, sizeof(metric), "bpf_probe_latency_seconds_bucket");
		metric_add(metric, lbl, &acc, DATATYPE_UINT, bpf_carg);
	}
	{
		alligator_ht *lbl = alligator_ht_init(NULL);
		acc += row->raw[BPF_HIST_LAST];
		add_label(lbl, "le", "+Inf");
		add_label(lbl, "probe", name);
		if (p) {
			for (a = 0; a < p->nargs && a < 6; a++)
				add_label(lbl, (char *)p->args[a].label, row->l[a]);
		}
		snprintf(metric, sizeof(metric), "bpf_probe_latency_seconds_bucket");
		metric_add(metric, lbl, &acc, DATATYPE_UINT, bpf_carg);
	}
	sum = (double)row->sum_ns / 1e9;
	{
		alligator_ht *lbl = alligator_ht_init(NULL);
		add_label(lbl, "probe", name);
		if (p) {
			for (a = 0; a < p->nargs && a < 6; a++)
				add_label(lbl, (char *)p->args[a].label, row->l[a]);
		}
		metric_add("bpf_probe_latency_seconds_sum", lbl, &sum, DATATYPE_DOUBLE, bpf_carg);
	}
	{
		alligator_ht *lbl = alligator_ht_init(NULL);
		uint64_t count = row->count ? row->count : acc;
		add_label(lbl, "probe", name);
		if (p) {
			for (a = 0; a < p->nargs && a < 6; a++)
				add_label(lbl, (char *)p->args[a].label, row->l[a]);
		}
		metric_add("bpf_probe_latency_seconds_count", lbl, &count, DATATYPE_UINT, bpf_carg);
	}
}

static void emit_ctr_row(struct ctr_row *row)
{
	alligator_ht *lbl;
	char port[16];
	char changed[8];

	uint8_t mtype = METRIC_TYPE_COUNTER;

	if (!row->used || !bpf_carg)
		return;
	if (!strcmp(row->metric, "bpf_process_fds"))
		mtype = METRIC_TYPE_GAUGE;
	namespace_metric_family_set(NULL, bpf_carg, row->metric, mtype, row->metric);
	lbl = alligator_ht_init(NULL);
	add_label(lbl, "comm", row->comm);
	add_label(lbl, "event", row->event);
	add_label(lbl, "path", row->path);
	add_label(lbl, "parent", row->parent);
	if (row->has_dport) {
		snprintf(port, sizeof(port), "%u", row->dport);
		add_label(lbl, "dport", port);
	}
	if (row->has_changed) {
		snprintf(changed, sizeof(changed), "%u", row->changed);
		add_label(lbl, "changed", changed);
	}
	add_cgroup_labels(lbl, row->cgroup);
	metric_add(row->metric, lbl, &row->value, DATATYPE_UINT, bpf_carg);
}

void bpf_pressure_sample(const char *path, const char *kind, double avg10, double avg60, double avg300, double total_sec)
{
	alligator_ht *lbl;
	const char *metric;
	char pod[64], ns[64], container[128];
	double windows[3];
	const char *wn[3] = { "10", "60", "300" };
	int i;

	if (!bpf_carg || !kind)
		return;
	bpf_cgroup_parse(path, pod, sizeof(pod), ns, sizeof(ns), container, sizeof(container));
	if (!strcmp(kind, "some"))
		metric = "bpf_cgroup_memory_pressure_waiting_seconds_total";
	else if (!strcmp(kind, "full"))
		metric = "bpf_cgroup_memory_pressure_stalled_seconds_total";
	else
		return;
	namespace_metric_family_set(NULL, bpf_carg, metric, METRIC_TYPE_COUNTER, "Cgroup memory PSI total.");
	lbl = alligator_ht_init(NULL);
	add_label(lbl, "cgroup", path);
	add_label(lbl, "pod", pod);
	add_label(lbl, "namespace", ns);
	add_label(lbl, "container", container);
	metric_add((char *)metric, lbl, &total_sec, DATATYPE_DOUBLE, bpf_carg);
	windows[0] = avg10;
	windows[1] = avg60;
	windows[2] = avg300;
	for (i = 0; i < 3; i++) {
		const char *avg_metric = !strcmp(kind, "some")
			? "bpf_cgroup_memory_pressure_waiting_avg_percent"
			: "bpf_cgroup_memory_pressure_stalled_avg_percent";
		lbl = alligator_ht_init(NULL);
		add_label(lbl, "cgroup", path);
		add_label(lbl, "pod", pod);
		add_label(lbl, "namespace", ns);
		add_label(lbl, "container", container);
		add_label(lbl, "window", wn[i]);
		namespace_metric_family_set(NULL, bpf_carg, avg_metric, METRIC_TYPE_GAUGE, "Cgroup memory PSI average.");
		metric_add((char *)avg_metric, lbl, &windows[i], DATATYPE_DOUBLE, bpf_carg);
	}
}

#ifdef __linux__
static int walk_left;

static void walk_cgroup(const char *dir, int depth, int want_pressure)
{
	DIR *d;
	struct dirent *de;

	if (depth > 8 || walk_left <= 0 || !dir)
		return;
	bpf_cgroup_remember(dir, NULL, NULL, NULL);
	walk_left--;
	if (want_pressure) {
		char file[512];
		FILE *fp;
		snprintf(file, sizeof(file), "%s/memory.pressure", dir);
		fp = fopen(file, "r");
		if (fp) {
			char line[256];
			while (fgets(line, sizeof(line), fp)) {
				char kind[16];
				double avg10, avg60, avg300;
				uint64_t total = 0;
				if (!pressure_parse_line(line, kind, sizeof(kind), &avg10, &avg60, &avg300, &total))
					continue;
				bpf_pressure_sample(dir, kind, avg10, avg60, avg300, (double)total / 1000000.0);
			}
			fclose(fp);
		}
	}
	d = opendir(dir);
	if (!d)
		return;
	while ((de = readdir(d))) {
		char child[512];
		struct stat st;
		if (de->d_name[0] == '.')
			continue;
		snprintf(child, sizeof(child), "%s/%s", dir, de->d_name);
		if (stat(child, &st))
			continue;
		if (S_ISDIR(st.st_mode))
			walk_cgroup(child, depth + 1, want_pressure);
	}
	closedir(d);
}

static void refresh_cgroups(int want_pressure)
{
	char root[256];
	const char *sys = (ac && ac->system_sysfs) ? ac->system_sysfs : "/sys/";

	walk_left = 1024;
	snprintf(root, sizeof(root), "%sfs/cgroup", sys);
	walk_cgroup(root, 0, want_pressure);
}
#endif

static int json_bool(json_t *j)
{
	if (!j)
		return 0;
	if (json_is_true(j))
		return 1;
	if (json_is_integer(j))
		return json_integer_value(j) != 0;
	if (json_is_string(j)) {
		const char *s = json_string_value(j);
		return s && strcmp(s, "0") && strcmp(s, "off") && strcmp(s, "false");
	}
	return 0;
}

static const char *json_str(json_t *j)
{
	if (!j || !json_is_string(j))
		return NULL;
	return json_string_value(j);
}

static int load_arg(bpf_arg *dst, json_t *j)
{
	const char *type, *filter, *label, *value;
	json_t *allow;
	size_t i;

	memset(dst, 0, sizeof(*dst));
	dst->index = (int)json_integer_value(json_object_get(j, "index"));
	type = json_str(json_object_get(j, "type"));
	filter = json_str(json_object_get(j, "filter"));
	label = json_str(json_object_get(j, "label"));
	value = json_str(json_object_get(j, "value"));
	if (!strcmp(type ? type : "", "u64"))
		dst->kind = BPF_ARG_U64;
	else if (!strcmp(type ? type : "", "user_str"))
		dst->kind = BPF_ARG_USER_STR;
	else if (!strcmp(type ? type : "", "kernel_str"))
		dst->kind = BPF_ARG_KERNEL_STR;
	else if (!strcmp(type ? type : "", "sockaddr"))
		dst->kind = BPF_ARG_SOCKADDR;
	else
		return -1;
	dst->filter = BPF_FILTER_NONE;
	if (filter) {
		if (!strcmp(filter, "eq"))
			dst->filter = BPF_FILTER_EQ;
		else if (!strcmp(filter, "prefix"))
			dst->filter = BPF_FILTER_PREFIX;
		else if (!strcmp(filter, "suffix"))
			dst->filter = BPF_FILTER_SUFFIX;
		else if (!strcmp(filter, "allow"))
			dst->filter = BPF_FILTER_ALLOW;
	}
	copy_cap(dst->label, sizeof(dst->label), label);
	copy_cap(dst->value, sizeof(dst->value), value);
	if (label && (!strcmp(label, "dport") || !strcmp(label, "port")))
		dst->field = BPF_FIELD_PORT;
	allow = json_object_get(j, "allow");
	if (json_is_array(allow)) {
		for (i = 0; i < json_array_size(allow) && dst->nallow < BPF_ALLOW_MAX; i++) {
			const char *s = json_str(json_array_get(allow, i));
			if (!s)
				continue;
			copy_cap(dst->allow[dst->nallow], BPF_STR_MAX, s);
			dst->nallow++;
		}
	}
	/* Plain `allow=foo` fills the array without filter=; treat as ALLOW. */
	if (dst->nallow > 0 && dst->filter == BPF_FILTER_NONE)
		dst->filter = BPF_FILTER_ALLOW;
	if (bpf_path_label(dst->label) && dst->filter == BPF_FILTER_NONE)
		return -1;
	return 0;
}

static int load_probe(bpf_probe *dst, json_t *j)
{
	const char *kind, *symbol, *name, *binary;
	json_t *args, *arr;
	size_t i;

	memset(dst, 0, sizeof(*dst));
	dst->generic_slot = -1;
	kind = json_str(json_object_get(j, "kind"));
	symbol = json_str(json_object_get(j, "symbol"));
	name = json_str(json_object_get(j, "name"));
	binary = json_str(json_object_get(j, "binary"));
	if (!kind)
		return -1;
	if (!strcmp(kind, "kprobe"))
		dst->kind = BPF_KIND_KPROBE;
	else if (!strcmp(kind, "uprobe"))
		dst->kind = BPF_KIND_UPROBE;
	else if (!strcmp(kind, "probe"))
		dst->kind = BPF_KIND_NAMED;
	else
		return -1;
	copy_cap(dst->symbol, sizeof(dst->symbol), symbol);
	copy_cap(dst->binary, sizeof(dst->binary), binary);
	copy_cap(dst->name, sizeof(dst->name), name ? name : symbol);
	if (dst->kind == BPF_KIND_NAMED) {
		dst->named = bpf_named_id(dst->name);
		if (!dst->named) {
			glog(L_WARN, "bpf: unknown probe '%s'\n", dst->name);
			return -1;
		}
	}
	if (dst->kind == BPF_KIND_UPROBE && strchr(dst->symbol, ':')) {
		char *colon = strchr(dst->symbol, ':');
		copy_cap(dst->binary, sizeof(dst->binary), dst->symbol);
		dst->binary[(size_t)(colon - dst->symbol)] = 0;
		copy_cap(dst->symbol, sizeof(dst->symbol), colon + 1);
	}
	{
		const char *fop = json_str(json_object_get(j, "filter"));
		const char *fval = json_str(json_object_get(j, "filter_value"));
		if (fop) {
			if (!strcmp(fop, "eq"))
				dst->filter_op = BPF_FILTER_EQ;
			else if (!strcmp(fop, "prefix"))
				dst->filter_op = BPF_FILTER_PREFIX;
			else if (!strcmp(fop, "suffix"))
				dst->filter_op = BPF_FILTER_SUFFIX;
			else if (!strcmp(fop, "allow"))
				dst->filter_op = BPF_FILTER_ALLOW;
		}
		copy_cap(dst->filter_value, sizeof(dst->filter_value), fval);
	}
	dst->want_dport = json_bool(json_object_get(j, "label_dport"));
	dst->latency = json_bool(json_object_get(j, "latency"));
	args = json_object_get(j, "args");
	if (json_is_array(args)) {
		for (i = 0; i < json_array_size(args) && dst->nargs < BPF_ARG_MAX; i++) {
			if (load_arg(&dst->args[dst->nargs], json_array_get(args, i))) {
				glog(L_WARN, "bpf: probe '%s' rejected an argument (path and uid labels need a filter)\n", dst->name);
				return -1;
			}
			dst->nargs++;
		}
	}
	arr = json_object_get(j, "prefixes");
	if (json_is_array(arr)) {
		for (i = 0; i < json_array_size(arr) && dst->nprefix < BPF_PREFIX_MAX; i++) {
			const char *s = json_str(json_array_get(arr, i));
			if (!s)
				continue;
			copy_cap(dst->prefix[dst->nprefix], BPF_STR_MAX, s);
			dst->nprefix++;
		}
	}
	arr = json_object_get(j, "comms");
	if (json_is_array(arr)) {
		for (i = 0; i < json_array_size(arr) && dst->ncomm < BPF_COMM_MAX; i++) {
			const char *s = json_str(json_array_get(arr, i));
			if (!s)
				continue;
			copy_cap(dst->comm[dst->ncomm], 16, s);
			dst->ncomm++;
		}
	}
	arr = json_object_get(j, "syscalls");
	if (json_is_array(arr)) {
		for (i = 0; i < json_array_size(arr) && dst->nsys < BPF_SYSCALL_SLOTS; i++) {
			const char *s = json_str(json_array_get(arr, i));
			if (!s)
				continue;
			copy_cap(dst->syscalls[dst->nsys], 16, s);
			dst->nsys++;
		}
	}
	if (dst->named == BPF_NAMED_SYSCALL && !dst->nsys) {
		const char *def[] = { "read", "write", "openat", "futex", "poll", "connect" };
		for (i = 0; i < 6; i++)
			copy_cap(dst->syscalls[dst->nsys++], 16, def[i]);
	}
	if (dst->named == BPF_NAMED_FILE && dst->filter_op && dst->filter_op != BPF_FILTER_PREFIX) {
		glog(L_WARN, "bpf: file_access only supports filter=prefix; probe not enabled\n");
		return -1;
	}
	if (dst->named == BPF_NAMED_FILE && dst->filter_op == BPF_FILTER_PREFIX && dst->filter_value[0] && dst->nprefix < BPF_PREFIX_MAX) {
		copy_cap(dst->prefix[dst->nprefix], BPF_STR_MAX, dst->filter_value);
		dst->nprefix++;
	}
	if (dst->named == BPF_NAMED_FILE && !dst->nprefix) {
		glog(L_WARN, "bpf: file_access requires a prefix allowlist; probe not enabled\n");
		return -1;
	}
	if (dst->named == BPF_NAMED_FD && !dst->ncomm) {
		glog(L_WARN, "bpf: fd_track requires a comm allowlist; probe not enabled\n");
		return -1;
	}
	if (dst->named == BPF_NAMED_DNS && !dst->binary[0])
		copy_cap(dst->binary, sizeof(dst->binary), "/lib64/libc.so.6");
	if (dst->named == BPF_NAMED_DNS && !dst->symbol[0])
		copy_cap(dst->symbol, sizeof(dst->symbol), "getaddrinfo");
	dst->enabled = 1;
	return 0;
}

static json_t *probe_to_json(const bpf_probe *p)
{
	json_t *o = json_object();
	json_t *args = json_array();
	json_t *prefs = json_array();
	int i;

	if (p->kind == BPF_KIND_KPROBE)
		json_object_set_new(o, "kind", json_string("kprobe"));
	else if (p->kind == BPF_KIND_UPROBE)
		json_object_set_new(o, "kind", json_string("uprobe"));
	else
		json_object_set_new(o, "kind", json_string("probe"));
	if (p->kind == BPF_KIND_NAMED)
		json_object_set_new(o, "name", json_string(p->name));
	else if (p->kind == BPF_KIND_UPROBE && p->binary[0]) {
		char sym[512];
		snprintf(sym, sizeof(sym), "%s:%s", p->binary, p->symbol);
		json_object_set_new(o, "symbol", json_string(sym));
	} else if (p->symbol[0])
		json_object_set_new(o, "symbol", json_string(p->symbol));
	for (i = 0; i < p->nargs; i++) {
		json_t *a = json_object();
		const char *type = "u64";
		const char *filter = NULL;
		json_object_set_new(a, "index", json_integer(p->args[i].index));
		if (p->args[i].kind == BPF_ARG_USER_STR)
			type = "user_str";
		else if (p->args[i].kind == BPF_ARG_KERNEL_STR)
			type = "kernel_str";
		else if (p->args[i].kind == BPF_ARG_SOCKADDR)
			type = "sockaddr";
		json_object_set_new(a, "type", json_string(type));
		if (p->args[i].label[0])
			json_object_set_new(a, "label", json_string(p->args[i].label));
		if (p->args[i].filter == BPF_FILTER_EQ)
			filter = "eq";
		else if (p->args[i].filter == BPF_FILTER_PREFIX)
			filter = "prefix";
		else if (p->args[i].filter == BPF_FILTER_SUFFIX)
			filter = "suffix";
		else if (p->args[i].filter == BPF_FILTER_ALLOW)
			filter = "allow";
		if (filter)
			json_object_set_new(a, "filter", json_string(filter));
		if (p->args[i].value[0])
			json_object_set_new(a, "value", json_string(p->args[i].value));
		if (p->args[i].nallow) {
			json_t *allow = json_array();
			int k;
			for (k = 0; k < p->args[i].nallow; k++)
				json_array_append_new(allow, json_string(p->args[i].allow[k]));
			json_object_set_new(a, "allow", allow);
		}
		json_array_append_new(args, a);
	}
	if (p->nargs)
		json_object_set_new(o, "args", args);
	else
		json_decref(args);
	for (i = 0; i < p->nprefix; i++)
		json_array_append_new(prefs, json_string(p->prefix[i]));
	if (p->nprefix)
		json_object_set_new(o, "prefixes", prefs);
	else
		json_decref(prefs);
	if (p->ncomm) {
		json_t *comms = json_array();
		for (i = 0; i < p->ncomm; i++)
			json_array_append_new(comms, json_string(p->comm[i]));
		json_object_set_new(o, "comms", comms);
	}
	if (p->nsys) {
		json_t *sys = json_array();
		for (i = 0; i < p->nsys; i++)
			json_array_append_new(sys, json_string(p->syscalls[i]));
		json_object_set_new(o, "syscalls", sys);
	}
	if (p->want_dport)
		json_object_set_new(o, "label_dport", json_true());
	if (p->latency)
		json_object_set_new(o, "latency", json_true());
	if (p->filter_op) {
		const char *filter = "eq";
		if (p->filter_op == BPF_FILTER_PREFIX)
			filter = "prefix";
		else if (p->filter_op == BPF_FILTER_SUFFIX)
			filter = "suffix";
		else if (p->filter_op == BPF_FILTER_ALLOW)
			filter = "allow";
		json_object_set_new(o, "filter", json_string(filter));
		if (p->filter_value[0])
			json_object_set_new(o, "filter_value", json_string(p->filter_value));
	}
	if (p->binary[0] && p->kind != BPF_KIND_UPROBE)
		json_object_set_new(o, "binary", json_string(p->binary));
	return o;
}

int bpf_config_apply(json_t *obj, int clear)
{
	char *dump = NULL;
	json_t *probes_j;
	size_t i;

	if (clear || !obj) {
		bpf_loader_shutdown();
		memset(probes, 0, sizeof(probes));
		probe_n = 0;
		generic_n = 0;
		cgroup_labels = 0;
		free(applied_json);
		applied_json = NULL;
		metric_ns_set(NULL);
		carg_sync_namespace();
		bpf_series_reset();
		memset(skips, 0, sizeof(skips));
		return 0;
	}
	dump = json_dumps(obj, JSON_COMPACT);
	if (dump && applied_json && !strcmp(dump, applied_json)) {
		free(dump);
		return 0;
	}
	bpf_loader_shutdown();
	memset(skips, 0, sizeof(skips));
	memset(probes, 0, sizeof(probes));
	probe_n = 0;
	generic_n = 0;
	cgroup_labels = json_bool(json_object_get(obj, "cgroup_labels"));
	metric_ns_set(json_str(json_object_get(obj, "namespace")));
	probes_j = json_object_get(obj, "probes");
	if (json_is_array(probes_j)) {
		for (i = 0; i < json_array_size(probes_j) && probe_n < BPF_PROBE_SLOTS; i++) {
			bpf_probe tmp;
			if (load_probe(&tmp, json_array_get(probes_j, i)))
				continue;
			if (tmp.kind != BPF_KIND_NAMED) {
				if (generic_n >= BPF_PROBE_SLOTS)
					continue;
				tmp.generic_slot = generic_n++;
			}
			probes[probe_n++] = tmp;
		}
	}
	free(applied_json);
	applied_json = dump;
	bpf_series_reset();
	ensure_carg();
	if (probe_n)
		bpf_loader_sync();
	return 0;
}

void bpf_config_get(json_t *dst)
{
	json_t *root, *arr;
	int i;

	if (!dst || !probe_n)
		return;
	root = json_object();
	arr = json_array();
	if (cgroup_labels)
		json_object_set_new(root, "cgroup_labels", json_true());
	if (metric_ns)
		json_object_set_new(root, "namespace", json_string(metric_ns));
	for (i = 0; i < probe_n; i++)
		json_array_append_new(arr, probe_to_json(&probes[i]));
	json_object_set_new(root, "probes", arr);
	json_object_set_new(dst, "bpf", root);
}

int bpf_probe_count(void)
{
	return probe_n;
}

int bpf_probe_on_name(const char *name)
{
	int i;
	int id;

	if (!name)
		return 0;
	id = bpf_named_id(name);
	for (i = 0; i < probe_n; i++) {
		if (!probes[i].enabled)
			continue;
		if (id && probes[i].named == id)
			return 1;
		if (!strcmp(probes[i].name, name) || !strcmp(probes[i].symbol, name))
			return 1;
	}
	return 0;
}

const bpf_probe *bpf_probe_at(int i)
{
	if (i < 0 || i >= probe_n)
		return NULL;
	return &probes[i];
}

int bpf_cgroup_labels_on(void)
{
	return cgroup_labels;
}

int bpf_any_enabled(void)
{
	return probe_n > 0;
}

static int want_named(int id)
{
	int i;

	for (i = 0; i < probe_n; i++) {
		if (probes[i].enabled && probes[i].named == id)
			return 1;
	}
	return 0;
}

void bpf_scrape(void)
{
	int i;
	uint64_t up;
	int want_pressure;
	int want_cg;

	if (!probe_n)
		return;
	ensure_carg();
	if (!bpf_carg)
		return;
	want_pressure = want_named(BPF_NAMED_CGROUP_MEM);
	want_cg = cgroup_labels || want_pressure;
#ifdef __linux__
	if (want_cg)
		refresh_cgroups(want_pressure);
#else
	(void)want_cg;
	(void)want_pressure;
#endif
	bpf_loader_poll();
	for (i = 0; i < BPF_SERIES_MAX; i++)
		emit_hist_row(&hists[i]);
	for (i = 0; i < BPF_SERIES_MAX; i++)
		emit_ctr_row(&ctrs[i]);
	for (i = 0; i < BPF_SERIES_MAX; i++)
		emit_gen_hist_row(&gen_hists[i]);
	for (i = 0; i < BPF_SERIES_MAX; i++) {
		const bpf_probe *p;
		alligator_ht *lbl;
		int a;
		if (!gens[i].used)
			continue;
		p = NULL;
		for (a = 0; a < probe_n; a++) {
			if (probes[a].generic_slot == gens[i].slot) {
				p = &probes[a];
				break;
			}
		}
		lbl = alligator_ht_init(NULL);
		namespace_metric_family_set(NULL, bpf_carg, "bpf_probe_events_total", METRIC_TYPE_COUNTER,
			"Filtered kprobe and uprobe events.");
		add_label(lbl, "probe", p ? p->name : "generic");
		if (p) {
			for (a = 0; a < p->nargs && a < 6; a++)
				add_label(lbl, (char *)p->args[a].label, gens[i].l[a]);
		}
		metric_add("bpf_probe_events_total", lbl, &gens[i].count, DATATYPE_UINT, bpf_carg);
	}
	for (i = 0; i < 64; i++) {
		if (!drops[i].used)
			continue;
		{
			alligator_ht *lbl = alligator_ht_init(NULL);
			namespace_metric_family_set(NULL, bpf_carg, "bpf_probe_dropped_total", METRIC_TYPE_COUNTER,
				"BPF samples not stored.");
			add_label(lbl, "probe", drops[i].probe);
			add_label(lbl, "reason", drops[i].reason);
			metric_add("bpf_probe_dropped_total", lbl, &drops[i].n, DATATYPE_UINT, bpf_carg);
		}
	}
	for (i = 0; i < BPF_PROBE_SLOTS; i++) {
		uint64_t v;
		alligator_ht *lbl;
		if (!skips[i].used)
			continue;
		v = skips[i].on ? 1 : 0;
		lbl = alligator_ht_init(NULL);
		namespace_metric_family_set(NULL, bpf_carg, "bpf_probe_skipped", METRIC_TYPE_GAUGE,
			"Probe not attached on this kernel.");
		add_label(lbl, "probe", skips[i].probe);
		metric_add("bpf_probe_skipped", lbl, &v, DATATYPE_UINT, bpf_carg);
	}
	up = bpf_loader_attached() ? 1 : 0;
	if (!up && !logged_off) {
		glog(L_INFO, "bpf: collector configured but not attached (ENABLE_BPF build, kernel >= 5.8, BTF, CAP_BPF+CAP_PERFMON or CAP_SYS_ADMIN)\n");
		logged_off = 1;
	}
	if (up)
		logged_off = 0;
	namespace_metric_family_set(NULL, bpf_carg, "bpf_up", METRIC_TYPE_GAUGE, "1 when BPF programs are attached.");
	metric_add_auto("bpf_up", &up, DATATYPE_UINT, bpf_carg);
}

void bpf_cleanup(void)
{
	bpf_loader_shutdown();
	if (bpf_carg) {
		free(bpf_carg->namespace);
		free(bpf_carg);
		bpf_carg = NULL;
	}
	metric_ns_set(NULL);
	free(applied_json);
	applied_json = NULL;
	memset(probes, 0, sizeof(probes));
	probe_n = 0;
	generic_n = 0;
	cgroup_labels = 0;
	cg_n = 0;
	logged_off = 0;
	memset(skips, 0, sizeof(skips));
	bpf_series_reset();
}
