#include "system/linux/perf_functions.h"
#include "main.h"
#include "common/logs.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern aconf *ac;

#define PERF_FUNCTIONS_COOLDOWN_SEC 60
#define PERF_FUNCTIONS_KALLSYMS_REFRESH_SEC 60
#define PERF_FUNCTIONS_TOP_MAX 1024
#define PERF_FUNCTIONS_FREQ_MAX 100000
#define PERF_FUNCTIONS_HITS_MAX 8192
#define PERF_FUNCTIONS_LAST_SYM_GAP (1024ULL * 1024ULL)
#define PERF_FUNCTIONS_RING_PAGES_MIN 8

int perf_kallsyms_parse_line(const char *line, uint64_t *addr, char *name, size_t name_cap)
{
	const char *p;
	char *end;
	uint64_t value;
	char type;
	size_t n;

	if (!line || !addr || !name || name_cap < 2)
		return 0;
	p = line;
	while (*p == ' ' || *p == '\t')
		p++;
	if (!isxdigit((unsigned char)*p))
		return 0;
	value = strtoull(p, &end, 16);
	if (end == p || value == 0)
		return 0;
	p = end;
	while (*p == ' ' || *p == '\t')
		p++;
	if (!*p)
		return 0;
	type = *p++;
	if (type != 'T' && type != 't' && type != 'W' && type != 'w')
		return 0;
	while (*p == ' ' || *p == '\t')
		p++;
	n = 0;
	while (p[n] && p[n] != ' ' && p[n] != '\t' && p[n] != '\n' && p[n] != '\r' && p[n] != '[')
		n++;
	if (!n || n >= name_cap)
		return 0;
	memcpy(name, p, n);
	name[n] = 0;
	*addr = value;
	return 1;
}

const char *perf_kallsyms_resolve(const perf_kallsym *syms, size_t n, uint64_t ip)
{
	size_t lo, hi;

	if (!syms || !n || ip < syms[0].addr)
		return NULL;
	lo = 0;
	hi = n;
	while (lo + 1 < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (syms[mid].addr <= ip)
			lo = mid;
		else
			hi = mid;
	}
	if (syms[lo].addr > ip || !syms[lo].name)
		return NULL;
	if (lo + 1 == n && ip - syms[lo].addr > PERF_FUNCTIONS_LAST_SYM_GAP)
		return NULL;
	return syms[lo].name;
}

static int perf_fn_better(const perf_fn_count *items, size_t a, size_t b)
{
	if (items[a].count != items[b].count)
		return items[a].count > items[b].count;
	if (!items[a].name)
		return 0;
	if (!items[b].name)
		return 1;
	return strcmp(items[a].name, items[b].name) < 0;
}

size_t perf_functions_select_top(const perf_fn_count *items, size_t n, size_t top, size_t *idx_out)
{
	size_t i, nsel = 0;

	if (!items || !idx_out || !top)
		return 0;
	for (i = 0; i < n; i++) {
		size_t j;
		if (!items[i].name || !items[i].count)
			continue;
		if (nsel < top) {
			idx_out[nsel++] = i;
			j = nsel - 1;
			while (j > 0 && perf_fn_better(items, idx_out[j], idx_out[j - 1])) {
				size_t tmp = idx_out[j];
				idx_out[j] = idx_out[j - 1];
				idx_out[j - 1] = tmp;
				j--;
			}
		} else if (perf_fn_better(items, i, idx_out[nsel - 1])) {
			idx_out[nsel - 1] = i;
			j = nsel - 1;
			while (j > 0 && perf_fn_better(items, idx_out[j], idx_out[j - 1])) {
				size_t tmp = idx_out[j];
				idx_out[j] = idx_out[j - 1];
				idx_out[j - 1] = tmp;
				j--;
			}
		}
	}
	return nsel;
}

int perf_functions_gate_decide(double sys_percent, int percent_valid, double threshold,
	int currently_open, int64_t *below_since, int64_t now_sec, int64_t cooldown_sec)
{
	int64_t since;

	if (!below_since)
		return 0;
	if (cooldown_sec < 0)
		cooldown_sec = 0;
	if (threshold <= 0.0) {
		*below_since = 0;
		return 1;
	}
	if (percent_valid && sys_percent >= threshold) {
		*below_since = 0;
		return 1;
	}
	if (!currently_open) {
		*below_since = 0;
		return 0;
	}
	since = *below_since;
	if (since <= 0 || now_sec < since)
		since = now_sec;
	if (now_sec - since >= cooldown_sec) {
		*below_since = 0;
		return 0;
	}
	*below_since = since;
	return 1;
}

size_t perf_functions_freq_attempts(uint64_t requested, uint64_t *out, size_t cap)
{
	uint64_t cands[3];
	size_t nc = 0, i, n = 0;

	if (!out || !cap)
		return 0;
	if (!requested)
		requested = 99;
	cands[nc++] = requested;
	if (requested > 49)
		cands[nc++] = 49;
	if (requested > 1)
		cands[nc++] = 1;
	for (i = 0; i < nc && n < cap; i++) {
		size_t j;
		int dup = 0;
		for (j = 0; j < n; j++) {
			if (out[j] == cands[i])
				dup = 1;
		}
		if (!dup)
			out[n++] = cands[i];
	}
	return n;
}

static int perf_fn_ring_copy(void *dst, const char *base, uint64_t size, uint64_t off, size_t len)
{
	uint64_t pos;

	if (!dst || !base || !size || len > size)
		return -1;
	pos = off % size;
	if (pos + len <= size)
		memcpy(dst, base + pos, len);
	else {
		size_t first = (size_t)(size - pos);
		memcpy(dst, base + pos, first);
		memcpy((char *)dst + first, base, len - first);
	}
	return 0;
}

static uint64_t perf_fn_skip_samples(uint64_t gap)
{
	uint64_t n;

	if (!gap)
		return 1;
	n = gap / PERF_FUNCTIONS_SAMPLE_REC;
	return n ? n : 1;
}

static uint64_t perf_fn_interval_samples(uint64_t freq, uint64_t period_ms)
{
	uint64_t prod;

	if (!freq || !period_ms)
		return 1;
	if (freq > UINT64_MAX / period_ms)
		return UINT64_MAX;
	prod = freq * period_ms;
	prod = (prod + 999) / 1000;
	return prod ? prod : 1;
}

int perf_functions_ring_step(const void *data, uint64_t data_size,
	uint64_t head, uint64_t tail, perf_fn_ring_step *step)
{
	const char *base = data;
	uint64_t gap;
	uint32_t type = 0;
	uint16_t misc = 0;
	uint16_t size = 0;

	if (!step)
		return 0;
	step->tail = tail;
	step->dropped = 0;
	step->ip = 0;
	step->take_ip = 0;
	step->skip_window = 0;
	if (tail >= head)
		return 0;
	gap = head - tail;
	/* Bytes past the data region are no longer a record boundary. */
	if (!data_size || gap > data_size
		|| perf_fn_ring_copy(&type, base, data_size, tail, sizeof(type)) < 0
		|| perf_fn_ring_copy(&misc, base, data_size, tail + 4, sizeof(misc)) < 0
		|| perf_fn_ring_copy(&size, base, data_size, tail + 6, sizeof(size)) < 0
		|| size < PERF_FN_HDR_SIZE
		|| (uint64_t)size > data_size
		|| tail + (uint64_t)size > head) {
		step->tail = head;
		step->dropped = perf_fn_skip_samples(gap);
		step->skip_window = 1;
		return 1;
	}
	step->tail = tail + size;
	if (type == PERF_FN_RECORD_LOST && size >= PERF_FN_HDR_SIZE + 16) {
		uint64_t lost = 0;
		if (perf_fn_ring_copy(&lost, base, data_size, tail + PERF_FN_HDR_SIZE + 8, sizeof(lost)) < 0) {
			step->tail = head;
			step->dropped = perf_fn_skip_samples(gap);
			step->skip_window = 1;
			return 1;
		}
		step->dropped = lost;
		return 1;
	}
	if (type == PERF_FN_RECORD_SAMPLE && size >= PERF_FN_HDR_SIZE + sizeof(uint64_t)) {
		if ((misc & PERF_FN_MISC_CPUMODE_MASK) == PERF_FN_MISC_KERNEL) {
			uint64_t ip = 0;
			if (perf_fn_ring_copy(&ip, base, data_size, tail + PERF_FN_HDR_SIZE, sizeof(ip)) == 0) {
				step->take_ip = 1;
				step->ip = ip;
			}
		}
	}
	return 1;
}

uint64_t perf_functions_ring_fit(uint64_t freq, uint64_t period_ms, uint64_t page_size,
	uint64_t *data_pages)
{
	uint64_t samples, bytes, pages, eff, cap_slots, cap_bytes;

	if (!freq)
		freq = 99;
	if (freq > PERF_FUNCTIONS_FREQ_MAX)
		freq = PERF_FUNCTIONS_FREQ_MAX;
	if (!period_ms)
		period_ms = 10000;
	if (!page_size)
		page_size = 4096;

	if (page_size > UINT64_MAX / PERF_FUNCTIONS_RING_PAGES_MAX)
		page_size = 4096;
	cap_bytes = PERF_FUNCTIONS_RING_PAGES_MAX * page_size;
	cap_slots = cap_bytes / PERF_FUNCTIONS_SAMPLE_REC;
	if (!cap_slots)
		cap_slots = 1;

	samples = perf_fn_interval_samples(freq, period_ms);
	eff = freq;
	if (samples > cap_slots) {
		if (cap_slots > UINT64_MAX / 1000 || period_ms > cap_slots * 1000)
			eff = 1;
		else {
			eff = (cap_slots * 1000) / period_ms;
			if (!eff)
				eff = 1;
		}
		if (eff > freq)
			eff = freq;
		samples = perf_fn_interval_samples(eff, period_ms);
		if (samples > cap_slots)
			samples = cap_slots;
	}

	bytes = samples * PERF_FUNCTIONS_SAMPLE_REC;
	if (bytes > cap_bytes)
		bytes = cap_bytes;
	pages = PERF_FUNCTIONS_RING_PAGES_MIN;
	while (pages < PERF_FUNCTIONS_RING_PAGES_MAX) {
		if (pages > UINT64_MAX / page_size)
			break;
		if (pages * page_size >= bytes)
			break;
		pages <<= 1;
	}
	if (data_pages)
		*data_pages = pages;
	return eff;
}

static int perf_functions_name_copy(const char *in, char *out, size_t cap)
{
	size_t n;

	if (!in || !out || cap < 2)
		return 0;
	n = strlen(in);
	if (n >= 2 && in[0] == '[' && in[n - 1] == ']') {
		in++;
		n -= 2;
	}
	if (!n || n >= cap)
		return 0;
	memcpy(out, in, n);
	out[n] = 0;
	return 1;
}

static void perf_functions_allow_free(void)
{
	size_t i;

	if (!ac || !ac->system_perf_functions_allow)
		return;
	for (i = 0; i < ac->system_perf_functions_allow_n; i++)
		free(ac->system_perf_functions_allow[i]);
	free(ac->system_perf_functions_allow);
	ac->system_perf_functions_allow = NULL;
	ac->system_perf_functions_allow_n = 0;
}

static int perf_functions_allow_same(const char *const *names, size_t nnames)
{
	size_t i, n = 0;
	char buf[256];

	if (!ac)
		return 0;
	if (!names)
		nnames = 0;
	for (i = 0; i < nnames; i++) {
		if (perf_functions_name_copy(names[i], buf, sizeof(buf)))
			n++;
	}
	if (n != ac->system_perf_functions_allow_n)
		return 0;
	n = 0;
	for (i = 0; i < nnames; i++) {
		if (!perf_functions_name_copy(names[i], buf, sizeof(buf)))
			continue;
		if (!ac->system_perf_functions_allow[n] || strcmp(buf, ac->system_perf_functions_allow[n]))
			return 0;
		n++;
	}
	return 1;
}

static void perf_functions_allow_replace(const char *const *names, size_t nnames)
{
	char **next;
	size_t i, n = 0;

	perf_functions_allow_free();
	if (!ac || !names || !nnames)
		return;
	next = calloc(nnames, sizeof(*next));
	if (!next)
		return;
	for (i = 0; i < nnames; i++) {
		char buf[256];
		if (!perf_functions_name_copy(names[i], buf, sizeof(buf)))
			continue;
		next[n] = strdup(buf);
		if (!next[n])
			continue;
		n++;
	}
	ac->system_perf_functions_allow = next;
	ac->system_perf_functions_allow_n = n;
}

static void perf_functions_runtime_close(void);
static void perf_functions_kallsyms_free(void);

void perf_functions_config_set(int enable, uint64_t freq, uint64_t top, double sys_percent,
	const char *const *names, size_t nnames)
{
	int same;

	if (!ac)
		return;
	if (!freq)
		freq = 99;
	if (freq > PERF_FUNCTIONS_FREQ_MAX)
		freq = PERF_FUNCTIONS_FREQ_MAX;
	if (!top)
		top = 20;
	if (top > PERF_FUNCTIONS_TOP_MAX)
		top = PERF_FUNCTIONS_TOP_MAX;
	if (sys_percent < 0)
		sys_percent = 0;

	if (!enable) {
		perf_functions_cleanup();
		perf_functions_allow_free();
		ac->system_perf_functions = 0;
		ac->system_perf_functions_freq = freq;
		ac->system_perf_functions_top = top;
		ac->system_perf_functions_sys_percent = sys_percent;
		return;
	}

	same = ac->system_perf_functions
		&& ac->system_perf_functions_freq == freq
		&& ac->system_perf_functions_top == top
		&& ac->system_perf_functions_sys_percent == sys_percent
		&& perf_functions_allow_same(names, nnames);
	if (same)
		return;

	perf_functions_runtime_close();
	perf_functions_allow_replace(names, nnames);
	ac->system_perf_functions = 1;
	ac->system_perf_functions_freq = freq;
	ac->system_perf_functions_top = top;
	ac->system_perf_functions_sys_percent = sys_percent;
}

typedef struct {
	uint64_t addr;
	uint32_t off;
} perf_ksym_off;

typedef struct {
	perf_ksym_off *tab;
	size_t n;
	size_t cap;
	char *blob;
	size_t blen;
	size_t bcap;
} perf_ksym_acc;

static size_t kallsyms_bytes_live;

static size_t perf_kallsyms_owned_bytes(size_t n, size_t blob_len)
{
	return n * sizeof(perf_kallsym) + blob_len;
}

size_t perf_kallsyms_bytes_live(void)
{
	return kallsyms_bytes_live;
}

void perf_kallsyms_table_free(perf_kallsym *syms, size_t nsyms, char *blob, size_t blob_len)
{
	size_t bytes;

	if (!syms && !blob)
		return;
	bytes = perf_kallsyms_owned_bytes(nsyms, blob_len);
	if (kallsyms_bytes_live >= bytes)
		kallsyms_bytes_live -= bytes;
	else
		kallsyms_bytes_live = 0;
	free(syms);
	free(blob);
}

static void perf_ksym_acc_free(perf_ksym_acc *a)
{
	if (!a)
		return;
	free(a->tab);
	free(a->blob);
	memset(a, 0, sizeof(*a));
}

static int perf_ksym_off_cmp(const void *a, const void *b)
{
	const perf_ksym_off *ka = a;
	const perf_ksym_off *kb = b;

	if (ka->addr < kb->addr)
		return -1;
	if (ka->addr > kb->addr)
		return 1;
	return 0;
}

static int perf_ksym_acc_add(perf_ksym_acc *a, uint64_t addr, const char *name)
{
	size_t len, ncap, bcap;
	char *nb;
	perf_ksym_off *nt;

	if (!a || !name || !name[0])
		return -1;
	len = strlen(name) + 1;
	if (len > UINT32_MAX || a->blen > UINT32_MAX - len)
		return -1;
	if (a->blen + len > a->bcap) {
		bcap = a->bcap ? a->bcap * 2 : 4096;
		while (bcap < a->blen + len) {
			if (bcap > SIZE_MAX / 2)
				return -1;
			bcap *= 2;
		}
		nb = realloc(a->blob, bcap);
		if (!nb)
			return -1;
		a->blob = nb;
		a->bcap = bcap;
	}
	if (a->n == a->cap) {
		ncap = a->cap ? a->cap * 2 : 1024;
		nt = realloc(a->tab, ncap * sizeof(*nt));
		if (!nt)
			return -1;
		a->tab = nt;
		a->cap = ncap;
	}
	memcpy(a->blob + a->blen, name, len);
	a->tab[a->n].addr = addr;
	a->tab[a->n].off = (uint32_t)a->blen;
	a->blen += len;
	a->n++;
	return 0;
}

static int perf_ksym_acc_line(perf_ksym_acc *a, const char *line)
{
	uint64_t addr;
	char name[256];

	if (!perf_kallsyms_parse_line(line, &addr, name, sizeof(name)))
		return 0;
	if (perf_ksym_acc_add(a, addr, name) < 0)
		return -1;
	return 1;
}

/* Consumes *a. On failure the outputs are left untouched. */
static int perf_ksym_acc_finish(perf_ksym_acc *a, perf_kallsym **syms, size_t *nsyms, char **blob, size_t *blob_len)
{
	perf_kallsym *out;
	char *mem;
	size_t i, n, blen;

	if (!a || !syms || !nsyms || !blob || !blob_len)
		return -1;
	n = a->n;
	if (!n) {
		perf_ksym_acc_free(a);
		*syms = NULL;
		*nsyms = 0;
		*blob = NULL;
		*blob_len = 0;
		return 0;
	}
	qsort(a->tab, n, sizeof(*a->tab), perf_ksym_off_cmp);
	blen = a->blen;
	mem = a->blob;
	if (blen != a->bcap) {
		char *shrunk = realloc(mem, blen);
		if (shrunk)
			mem = shrunk;
	}
	out = malloc(n * sizeof(*out));
	if (!out) {
		a->blob = mem;
		perf_ksym_acc_free(a);
		return -1;
	}
	for (i = 0; i < n; i++) {
		out[i].addr = a->tab[i].addr;
		out[i].name = mem + a->tab[i].off;
	}
	free(a->tab);
	memset(a, 0, sizeof(*a));
	kallsyms_bytes_live += perf_kallsyms_owned_bytes(n, blen);
	*syms = out;
	*nsyms = n;
	*blob = mem;
	*blob_len = blen;
	return 0;
}

int perf_kallsyms_table_set(perf_kallsym **syms, size_t *nsyms, char **blob, size_t *blob_len, const char *text)
{
	perf_ksym_acc acc;
	perf_kallsym *next = NULL;
	char *nblob = NULL;
	size_t nn = 0, nbl = 0;
	const char *p;

	if (!syms || !nsyms || !blob || !blob_len)
		return -1;
	memset(&acc, 0, sizeof(acc));
	p = text ? text : "";
	while (*p) {
		char line[1024];
		size_t len = 0;
		int rc;

		while (p[len] && p[len] != '\n' && len + 1 < sizeof(line))
			len++;
		memcpy(line, p, len);
		line[len] = 0;
		if (p[len] == '\n')
			p += len + 1;
		else if (p[len]) {
			p += len;
			while (*p && *p != '\n')
				p++;
			if (*p == '\n')
				p++;
		} else
			p += len;
		rc = perf_ksym_acc_line(&acc, line);
		if (rc < 0) {
			perf_ksym_acc_free(&acc);
			return -1;
		}
	}
	if (perf_ksym_acc_finish(&acc, &next, &nn, &nblob, &nbl) < 0)
		return -1;
	perf_kallsyms_table_free(*syms, *nsyms, *blob, *blob_len);
	*syms = next;
	*nsyms = nn;
	*blob = nblob;
	*blob_len = nbl;
	return 0;
}

#ifdef __linux__

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/perf_event.h>

#ifndef PERF_FLAG_FD_CLOEXEC
#define PERF_FLAG_FD_CLOEXEC (1UL << 3)
#endif

_Static_assert(PERF_RECORD_LOST == PERF_FN_RECORD_LOST, "PERF_RECORD_LOST");
_Static_assert(PERF_RECORD_SAMPLE == PERF_FN_RECORD_SAMPLE, "PERF_RECORD_SAMPLE");
_Static_assert(PERF_RECORD_MISC_KERNEL == PERF_FN_MISC_KERNEL, "PERF_RECORD_MISC_KERNEL");
_Static_assert(sizeof(struct perf_event_header) == PERF_FN_HDR_SIZE, "perf_event_header");

typedef struct {
	char *name;
	uint64_t count;
} perf_hit;

typedef struct {
	int fd;
	int cpu;
	void *mem;
	size_t mem_len;
} perf_fn_fd;

static perf_hit *hits;
static size_t hits_n;
static size_t hits_cap;
static uint64_t dropped;
static perf_fn_fd *fn_fds;
static size_t fn_fds_n;
static size_t fn_fds_cap;
static int64_t below_since;
static perf_kallsym *ksyms;
static size_t ksyms_n;
static char *ksyms_blob;
static size_t ksyms_blob_len;
static int ksyms_tried;
static int64_t ksyms_loaded_at;
static int kallsyms_warned;
static int open_fail_logged;
static int ring_overrun_logged;
static uint64_t clamp_logged_req;
static uint64_t clamp_logged_eff;

static int hit_cmp(const void *key, const void *elem)
{
	return strcmp((const char *)key, ((const perf_hit *)elem)->name);
}

static perf_hit *hit_find(const char *name)
{
	if (!hits_n || !name)
		return NULL;
	return bsearch(name, hits, hits_n, sizeof(*hits), hit_cmp);
}

static uint64_t hit_count_of(const char *name)
{
	perf_hit *h = hit_find(name);
	return h ? h->count : 0;
}

static void hits_reset(void)
{
	size_t i;
	for (i = 0; i < hits_n; i++)
		free(hits[i].name);
	free(hits);
	hits = NULL;
	hits_n = 0;
	hits_cap = 0;
}

static void hit_add(const char *name)
{
	perf_hit *found;
	char *copy;
	size_t i;

	if (!name || !name[0])
		return;
	found = hit_find(name);
	if (found) {
		found->count++;
		return;
	}
	if (hits_n >= PERF_FUNCTIONS_HITS_MAX) {
		dropped++;
		return;
	}
	if (hits_n == hits_cap) {
		size_t ncap = hits_cap ? hits_cap * 2 : 64;
		perf_hit *n = realloc(hits, ncap * sizeof(*n));
		if (!n) {
			dropped++;
			return;
		}
		hits = n;
		hits_cap = ncap;
	}
	copy = strdup(name);
	if (!copy) {
		dropped++;
		return;
	}
	i = hits_n;
	while (i > 0 && strcmp(copy, hits[i - 1].name) < 0) {
		hits[i] = hits[i - 1];
		i--;
	}
	hits[i].name = copy;
	hits[i].count = 1;
	hits_n++;
}

static int allow_match(const char *name)
{
	size_t i;

	if (!ac || !ac->system_perf_functions_allow_n)
		return 1;
	for (i = 0; i < ac->system_perf_functions_allow_n; i++) {
		if (ac->system_perf_functions_allow[i] && !strcmp(ac->system_perf_functions_allow[i], name))
			return 1;
	}
	return 0;
}

static void account_ip(uint64_t ip)
{
	const char *name = perf_kallsyms_resolve(ksyms, ksyms_n, ip);
	if (!name) {
		dropped++;
		return;
	}
	if (!allow_match(name))
		return;
	hit_add(name);
}

static void perf_functions_kallsyms_free(void)
{
	perf_kallsyms_table_free(ksyms, ksyms_n, ksyms_blob, ksyms_blob_len);
	ksyms = NULL;
	ksyms_n = 0;
	ksyms_blob = NULL;
	ksyms_blob_len = 0;
	ksyms_tried = 0;
	ksyms_loaded_at = 0;
}

static void kallsyms_load(void)
{
	FILE *fd;
	char path[512];
	char line[1024];
	perf_ksym_acc acc;
	perf_kallsym *next = NULL;
	char *nblob = NULL;
	size_t nn = 0, nbl = 0;

	ksyms_loaded_at = (int64_t)time(NULL);
	ksyms_tried = 1;
	if (!ac || !ac->system_procfs)
		return;
	snprintf(path, sizeof(path), "%s/kallsyms", ac->system_procfs);
	fd = fopen(path, "r");
	if (!fd) {
		if (!kallsyms_warned && ac->system_carg) {
			carglog(ac->system_carg, L_WARN, "perf_functions: open %s: %s\n", path, strerror(errno));
			kallsyms_warned = 1;
		}
		return;
	}
	memset(&acc, 0, sizeof(acc));
	while (fgets(line, sizeof(line), fd)) {
		if (perf_ksym_acc_line(&acc, line) < 0) {
			fclose(fd);
			perf_ksym_acc_free(&acc);
			return;
		}
	}
	fclose(fd);
	if (perf_ksym_acc_finish(&acc, &next, &nn, &nblob, &nbl) < 0)
		return;
	perf_kallsyms_table_free(ksyms, ksyms_n, ksyms_blob, ksyms_blob_len);
	ksyms = next;
	ksyms_n = nn;
	ksyms_blob = nblob;
	ksyms_blob_len = nbl;
	ksyms_tried = 1;
	ksyms_loaded_at = (int64_t)time(NULL);
}

static void drain_one(perf_fn_fd *e)
{
	struct perf_event_mmap_page *meta;
	char *data;
	uint64_t data_size, head, tail;
	long page;

	if (!e || e->fd < 0 || !e->mem)
		return;
	page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
	if (e->mem_len <= (size_t)page)
		return;
	meta = e->mem;
	data = (char *)e->mem + page;
	data_size = e->mem_len - (size_t)page;
	head = meta->data_head;
	__sync_synchronize();
	tail = meta->data_tail;
	while (tail < head) {
		perf_fn_ring_step step;
		uint64_t before = tail;

		if (!perf_functions_ring_step(data, data_size, head, tail, &step))
			break;
		/* A bad header is not a record boundary. Publishing the same tail
		 * wedges every later scrape, so skip the window to data_head. */
		if (step.skip_window) {
			dropped += step.dropped;
			if (!ring_overrun_logged && ac && ac->system_carg) {
				carglog(ac->system_carg, L_INFO,
					"perf_functions: ring overrun, skipped %" PRIu64 " bytes to data_head; sampling continues\n",
					head - before);
				ring_overrun_logged = 1;
			}
			tail = step.tail;
			break;
		}
		if (step.take_ip)
			account_ip(step.ip);
		dropped += step.dropped;
		tail = step.tail;
		if (tail <= before)
			break;
	}
	__sync_synchronize();
	meta->data_tail = tail;
	__sync_synchronize();
}

static int perf_cpu_online(int cpu)
{
	char path[128];
	char buf[16];
	FILE *fd;

	if (!ac || !ac->system_sysfs)
		return 1;
	snprintf(path, sizeof(path), "%s/devices/system/cpu/cpu%d/online", ac->system_sysfs, cpu);
	fd = fopen(path, "r");
	if (!fd)
		return 1;
	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return 0;
	}
	fclose(fd);
	return buf[0] != '0';
}

static int try_open(int cpu, uint64_t freq)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_SOFTWARE;
	attr.size = sizeof(attr);
	attr.config = PERF_COUNT_SW_CPU_CLOCK;
	attr.sample_type = PERF_SAMPLE_IP;
	attr.freq = 1;
	attr.sample_freq = freq;
	attr.exclude_user = 1;
	attr.exclude_kernel = 0;
	attr.exclude_hv = 1;
	attr.exclude_idle = 1;
	attr.disabled = 0;
	return (int)syscall(__NR_perf_event_open, &attr, -1, cpu, -1, PERF_FLAG_FD_CLOEXEC);
}

static int fn_push(int fd, int cpu, void *mem, size_t len)
{
	if (fn_fds_n == fn_fds_cap) {
		size_t ncap = fn_fds_cap ? fn_fds_cap * 2 : 8;
		perf_fn_fd *n = realloc(fn_fds, ncap * sizeof(*n));
		if (!n)
			return -1;
		fn_fds = n;
		fn_fds_cap = ncap;
	}
	fn_fds[fn_fds_n].fd = fd;
	fn_fds[fn_fds_n].cpu = cpu;
	fn_fds[fn_fds_n].mem = mem;
	fn_fds[fn_fds_n].mem_len = len;
	fn_fds_n++;
	return 0;
}

static int map_and_keep(int fd, int cpu, uint64_t freq)
{
	long page;
	uint64_t period_ms, pages = 0;
	size_t map_len;
	void *mem;

	page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
	period_ms = 10000;
	if (ac && ac->system_aggregator_repeat > 0)
		period_ms = (uint64_t)ac->system_aggregator_repeat;
	(void)perf_functions_ring_fit(freq, period_ms, (uint64_t)page, &pages);
	map_len = (size_t)(1 + pages) * (size_t)page;
	mem = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED)
		return -1;
	(void)ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
	if (fn_push(fd, cpu, mem, map_len) < 0) {
		munmap(mem, map_len);
		return -1;
	}
	return 0;
}

static void perf_functions_open(void)
{
	long ncpu;
	int cpu, probe = -1, fd;
	uint64_t attempts[3];
	size_t nattempts, ai;
	uint64_t use_freq = 0;
	uint64_t requested, open_freq, period_ms;
	long page;

	if (!ac)
		return;
	requested = ac->system_perf_functions_freq ? ac->system_perf_functions_freq : 99;
	ncpu = sysconf(_SC_NPROCESSORS_CONF);
	if (ncpu <= 0)
		return;
	for (cpu = 0; cpu < (int)ncpu; cpu++) {
		if (perf_cpu_online(cpu)) {
			probe = cpu;
			break;
		}
	}
	if (probe < 0)
		return;

	page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
	period_ms = 10000;
	if (ac->system_aggregator_repeat > 0)
		period_ms = (uint64_t)ac->system_aggregator_repeat;
	open_freq = perf_functions_ring_fit(requested, period_ms, (uint64_t)page, NULL);
	if (open_freq != requested && (clamp_logged_req != requested || clamp_logged_eff != open_freq) && ac->system_carg) {
		carglog(ac->system_carg, L_INFO,
			"perf_functions: sample freq %" PRIu64 " does not fit one %" PRIu64 " ms scrape in %u data pages, using %" PRIu64 "\n",
			requested, period_ms, PERF_FUNCTIONS_RING_PAGES_MAX, open_freq);
		clamp_logged_req = requested;
		clamp_logged_eff = open_freq;
	}

	nattempts = perf_functions_freq_attempts(open_freq, attempts, 3);
	for (ai = 0; ai < nattempts && !use_freq; ai++) {
		fd = try_open(probe, attempts[ai]);
		if (fd >= 0) {
			if (map_and_keep(fd, probe, attempts[ai]) < 0) {
				close(fd);
				if (!open_fail_logged && ac->system_carg) {
					carglog(ac->system_carg, L_WARN, "perf_functions: mmap cpu%d: %s\n", probe, strerror(errno));
					open_fail_logged = 1;
				}
				return;
			}
			use_freq = attempts[ai];
			break;
		}
		if (errno == EACCES || errno == EPERM) {
			if (!open_fail_logged && ac->system_carg) {
				carglog(ac->system_carg, L_WARN,
					"perf_functions: perf_event_open cpu-clock: %s (cpu-wide pid -1 needs perf_event_paranoid <= 0, or root / CAP_PERFMON / CAP_SYS_ADMIN)\n",
					strerror(errno));
				open_fail_logged = 1;
			}
			return;
		}
	}
	if (!use_freq) {
		if (!open_fail_logged && ac->system_carg) {
			carglog(ac->system_carg, L_WARN, "perf_functions: sample freq %" PRIu64 " rejected\n", open_freq);
			open_fail_logged = 1;
		}
		return;
	}
	if (use_freq != open_freq && ac->system_carg)
		carglog(ac->system_carg, L_INFO, "perf_functions: sample freq %" PRIu64 " rejected, using %" PRIu64 "\n",
			open_freq, use_freq);

	for (cpu = 0; cpu < (int)ncpu; cpu++) {
		if (cpu == probe || !perf_cpu_online(cpu))
			continue;
		fd = try_open(cpu, use_freq);
		if (fd < 0)
			continue;
		if (map_and_keep(fd, cpu, use_freq) < 0)
			close(fd);
	}
	open_fail_logged = 0;
}

static void perf_functions_runtime_close(void)
{
	size_t i;

	for (i = 0; i < fn_fds_n; i++) {
		if (fn_fds[i].mem && fn_fds[i].mem != MAP_FAILED)
			munmap(fn_fds[i].mem, fn_fds[i].mem_len);
		if (fn_fds[i].fd >= 0)
			close(fn_fds[i].fd);
	}
	free(fn_fds);
	fn_fds = NULL;
	fn_fds_n = 0;
	fn_fds_cap = 0;
	hits_reset();
	dropped = 0;
	below_since = 0;
	ring_overrun_logged = 0;
}

void perf_functions_cleanup(void)
{
	perf_functions_runtime_close();
	perf_functions_kallsyms_free();
	kallsyms_warned = 0;
	open_fail_logged = 0;
	clamp_logged_req = 0;
	clamp_logged_eff = 0;
}

static void publish_gauge(int sampling)
{
	uint64_t v = sampling ? 1 : 0;
	if (!ac || !ac->system_carg)
		return;
	metric_add_auto("perf_kernel_sampling", &v, DATATYPE_UINT, ac->system_carg);
}

static void publish_counts(void)
{
	size_t i;

	if (!ac || !ac->system_carg)
		return;
	if (ac->system_perf_functions_allow_n) {
		for (i = 0; i < ac->system_perf_functions_allow_n; i++) {
			uint64_t c;
			char *name = ac->system_perf_functions_allow[i];
			if (!name)
				continue;
			c = hit_count_of(name);
			metric_add_labels("perf_kernel_samples_total", &c, DATATYPE_UINT, ac->system_carg, "function", name);
		}
	} else if (hits_n) {
		perf_fn_count *items;
		size_t *idx;
		size_t top, nsel;
		top = ac->system_perf_functions_top ? ac->system_perf_functions_top : 20;
		items = calloc(hits_n, sizeof(*items));
		idx = calloc(top, sizeof(*idx));
		if (items && idx) {
			for (i = 0; i < hits_n; i++) {
				items[i].name = hits[i].name;
				items[i].count = hits[i].count;
			}
			nsel = perf_functions_select_top(items, hits_n, top, idx);
			for (i = 0; i < nsel; i++) {
				uint64_t c = items[idx[i]].count;
				metric_add_labels("perf_kernel_samples_total", &c, DATATYPE_UINT, ac->system_carg,
					"function", (char *)items[idx[i]].name);
			}
		}
		free(items);
		free(idx);
	}
	metric_add_auto("perf_kernel_samples_dropped_total", &dropped, DATATYPE_UINT, ac->system_carg);
}

void get_perf_functions_stats(void)
{
	int64_t now;
	int open_now, want;
	size_t i;

	if (!ac || !ac->system_carg || !ac->system_perf_functions)
		return;
	now = (int64_t)time(NULL);
	open_now = fn_fds_n > 0;
	want = perf_functions_gate_decide(ac->system_cpu_system_percent, ac->system_cpu_system_percent_valid,
		ac->system_perf_functions_sys_percent, open_now, &below_since, now, PERF_FUNCTIONS_COOLDOWN_SEC);
	if (!want) {
		if (open_now)
			perf_functions_runtime_close();
		publish_gauge(0);
		return;
	}
	if (!fn_fds_n)
		perf_functions_open();
	if (!fn_fds_n) {
		publish_gauge(0);
		return;
	}
	if (!ksyms_tried || now - ksyms_loaded_at >= PERF_FUNCTIONS_KALLSYMS_REFRESH_SEC)
		kallsyms_load();
	for (i = 0; i < fn_fds_n; i++)
		drain_one(&fn_fds[i]);
	publish_counts();
	publish_gauge(1);
}

#else

static void perf_functions_runtime_close(void) {}
static void perf_functions_kallsyms_free(void) {}

void perf_functions_cleanup(void)
{
	perf_functions_runtime_close();
	perf_functions_kallsyms_free();
}
void get_perf_functions_stats(void) {}

#endif
