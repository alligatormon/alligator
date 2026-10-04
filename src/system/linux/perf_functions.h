#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct perf_kallsym {
	uint64_t addr;
	char *name;
} perf_kallsym;

typedef struct perf_fn_count {
	const char *name;
	uint64_t count;
} perf_fn_count;

int perf_kallsyms_parse_line(const char *line, uint64_t *addr, char *name, size_t name_cap);
const char *perf_kallsyms_resolve(const perf_kallsym *syms, size_t n, uint64_t ip);

/* Names in *syms point into *blob. A later set frees the previous table.
 * *blob_len is the byte size of *blob (0 when *blob is NULL). */
int perf_kallsyms_table_set(perf_kallsym **syms, size_t *nsyms, char **blob, size_t *blob_len, const char *text);
void perf_kallsyms_table_free(perf_kallsym *syms, size_t nsyms, char *blob, size_t blob_len);
size_t perf_kallsyms_bytes_live(void);
size_t perf_functions_select_top(const perf_fn_count *items, size_t n, size_t top, size_t *idx_out);
int perf_functions_gate_decide(double sys_percent, int percent_valid, double threshold,
	int currently_open, int64_t *below_since, int64_t now_sec, int64_t cooldown_sec);
size_t perf_functions_freq_attempts(uint64_t requested, uint64_t *out, size_t cap);

/* perf_event ABI values used by the ring parser (no perf_event_open). */
#define PERF_FUNCTIONS_RING_PAGES_MAX 128u
#define PERF_FUNCTIONS_SAMPLE_REC 16u
#define PERF_FN_RECORD_LOST 2u
#define PERF_FN_RECORD_SAMPLE 9u
#define PERF_FN_MISC_CPUMODE_MASK 7u
#define PERF_FN_MISC_KERNEL 1u
#define PERF_FN_HDR_SIZE 8u

typedef struct {
	uint64_t tail;
	uint64_t dropped;
	uint64_t ip;
	int take_ip;
	int skip_window;
} perf_fn_ring_step;

/* Advance past one record, or skip a lost window to `head`.
 * Returns 1 when `step->tail` moves, 0 when `tail >= head` or `step` is NULL.
 * A gap past the data region, or a header that is too small, larger than the
 * data region, or past `head`, sets skip_window, sets tail to head, and sets
 * dropped to the skipped byte span divided by PERF_FUNCTIONS_SAMPLE_REC
 * (at least 1). PERF_RECORD_LOST adds the record's lost count and does not
 * set take_ip. A kernel PERF_RECORD_SAMPLE sets take_ip and advances by hdr.size. */
int perf_functions_ring_step(const void *data, uint64_t data_size,
	uint64_t head, uint64_t tail, perf_fn_ring_step *step);

/* Power-of-two data pages in [8, PERF_FUNCTIONS_RING_PAGES_MAX] for one scrape
 * at `freq` over `period_ms`. Returns `freq`, or a lower rate so one interval
 * fits in the page cap. */
uint64_t perf_functions_ring_fit(uint64_t freq, uint64_t period_ms, uint64_t page_size,
	uint64_t *data_pages);

void perf_functions_config_set(int enable, uint64_t freq, uint64_t top, double sys_percent,
	const char *const *names, size_t nnames);
void get_perf_functions_stats(void);
void perf_functions_cleanup(void);
