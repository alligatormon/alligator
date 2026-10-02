#pragma once
#include <stdint.h>
#include <pthread.h>
#include "dstructures/ht.h"

struct namespace_struct;
struct qw_cached_label;

/* What to do when a quantile window contains no samples. */
#define QUANTILE_WINDOW_EMPTY_DELETE 0
#define QUANTILE_WINDOW_EMPTY_ZERO 1

typedef struct percentile_buffer
{
	double *arr;       /* collect ring: writers only */
	double *sort_arr;  /* snapshot copied before heapsort */
	int64_t *ts;       /* unix seconds per slot; NULL when window is off */
	size_t n;
	int64_t cur;
	int64_t filled;    /* slots written, capped at n */
	int64_t sortsize;
	int64_t percentile_size;
	int64_t *percentile;
	int64_t *ipercentile;
	int64_t inserts_since_calc;
	int64_t calc_every; /* resolved at init: mapping override or global fallback */
	int64_t window_sec; /* 0 = sample-count ring (legacy) */
	uint8_t empty_policy; /* QUANTILE_WINDOW_EMPTY_* */
	uint8_t sorted;
	struct namespace_struct *ns;
	pthread_mutex_t lock;
	uint8_t lock_ready;
	/* Insert-path window recalc runs at most once per this unix second. */
	int64_t last_calc_sec;
	/* Resolved *_quantile name and labels (after carg merge and transform). */
	char *series_name;
	struct qw_cached_label *series_labels;
	uint8_t series_ready;
} percentile_buffer;

percentile_buffer* init_percentile_buffer(int64_t *percentile, size_t n);
/* buffer_min / calc_every / window_sec: -1 = use global ac fallback.
   empty_policy: -1 = use global ac fallback. */
percentile_buffer* init_percentile_buffer_opts(int64_t *percentile, size_t n, int64_t buffer_min, int64_t calc_every, int64_t window_sec, int8_t empty_policy);
void heapSort(double *arr, int64_t n);
int64_t* percentile_init_3n(int64_t n1, int64_t n2, int64_t n3);
void heap_insert(percentile_buffer *pb, double key);
void heap_insert_at(percentile_buffer *pb, double key, int64_t ts_sec);
void calc_percentiles(void *carg, percentile_buffer *pb, void *mnode, char *custom_mname, alligator_ht *custom_labels);
void free_percentile_buffer(percentile_buffer *pb);
/* Unknown or NULL policy logs a warning and returns DELETE. */
int8_t quantile_window_empty_parse_string(const char *pol);
