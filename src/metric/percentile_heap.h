#pragma once
#include <stdint.h>
#include "dstructures/ht.h"
typedef struct percentile_buffer
{
	int64_t *arr;
	size_t n;
	int64_t cur;
	int64_t sortsize;
	int64_t percentile_size;
	int64_t *percentile;
	int64_t *ipercentile;
} percentile_buffer;

percentile_buffer* init_percentile_buffer(int64_t *percentile, size_t n);
void heapSort(int64_t *arr, int64_t n);
int64_t* percentile_init_3n(int64_t n1, int64_t n2, int64_t n3);
void heap_insert(percentile_buffer *pb, int64_t key);
void calc_percentiles(void *carg, percentile_buffer *pb, void *mnode, char *custom_mname, alligator_ht *custom_labels);
void free_percentile_buffer(percentile_buffer *pb);
