#pragma once
#include <stdint.h>
#include "dstructures/ht.h"
typedef struct percentile_buffer
{
	double *arr;       /* collect ring: writers only */
	double *sort_arr;  /* snapshot copied before heapsort */
	size_t n;
	int64_t cur;
	int64_t sortsize;
	int64_t percentile_size;
	int64_t *percentile;
	int64_t *ipercentile;
	int64_t inserts_since_calc;
	int64_t calc_every; /* resolved at init: mapping override or global fallback */
	uint8_t sorted;
} percentile_buffer;

percentile_buffer* init_percentile_buffer(int64_t *percentile, size_t n);
/* buffer_min / calc_every: -1 = use global ac fallback */
percentile_buffer* init_percentile_buffer_opts(int64_t *percentile, size_t n, int64_t buffer_min, int64_t calc_every);
void heapSort(double *arr, int64_t n);
int64_t* percentile_init_3n(int64_t n1, int64_t n2, int64_t n3);
void heap_insert(percentile_buffer *pb, double key);
void calc_percentiles(void *carg, percentile_buffer *pb, void *mnode, char *custom_mname, alligator_ht *custom_labels);
void free_percentile_buffer(percentile_buffer *pb);
