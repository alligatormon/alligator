#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "common/logs.h"
#include "metric/percentile_heap.h"
#include "main.h"

void maxheapify(double *arr, int64_t i, int64_t heapsize)
{
	int64_t temp, largest, left, right;
	left = (2*i+1);
	right = ((2*i)+2);
	if (left >= heapsize)
		return;
	else
	{
		if (left < (heapsize) && arr[left] > arr[i])
			largest = left;
		else
			largest = i;
		if (right < (heapsize) && arr[right] > arr[largest])
			largest = right;
		if (largest != i)
		{
			temp = arr[i];
			arr[i] = arr[largest];
			arr[largest] = temp;
			maxheapify(arr, largest, heapsize);
		}
	}
}

void buildmaxheap(double *arr, int64_t n)
{
	int64_t heapsize = n;
	int64_t j;
	for (j = n/2; j >= 0; j--)
		maxheapify(arr, j, heapsize);
}

void heapify(double* arr, int64_t n, int64_t i)
{ 
	int64_t smallest = i;
	int64_t l = 2 * i + 1;
	int64_t r = 2 * i + 2;
  
	if (l < n && arr[l] < arr[smallest]) 
		smallest = l; 
  
	if (r < n && arr[r] < arr[smallest]) 
		smallest = r; 
  
	if (smallest != i)
	{ 
		double tmp = arr[i];
		arr[i] = arr[smallest];
		arr[smallest] = tmp;
  
		heapify(arr, n, smallest); 
	} 
} 

void heapSort(double *arr, int64_t n) 
{ 
	for (int64_t i = n / 2 - 1; i >= 0; i--) 
		heapify(arr, n, i); 
  
	for (int64_t i = n - 1; i >= 0; i--)
	{ 
		if (i)
		{
			double tmp = arr[0];
			arr[0] = arr[i];
			arr[i] = tmp;
		}
  
		heapify(arr, i, 0); 
	} 
} 

void calc_percentiles(void *arg, percentile_buffer *pb, void *node, char *custom_mname, alligator_ht *custom_labels)
{
	metric_node *mnode = node;
	context_arg *carg = arg;
	int64_t i;
	size_t n = pb->n;
	/* 0 = every insert (resolved at init from mapping or global) */
	int64_t every = pb->calc_every > 0 ? pb->calc_every : 1;
	int need_sort = !pb->sorted || pb->inserts_since_calc >= every;

	if (need_sort)
	{
		/* Snapshot collect ring so concurrent heap_insert cannot clobber the sort. */
		memcpy(pb->sort_arr, pb->arr, n * sizeof(double));
		buildmaxheap(pb->sort_arr, n);
		heapSort(pb->sort_arr, pb->sortsize);
		pb->inserts_since_calc = 0;
		pb->sorted = 1;
	}

	double *arr = pb->sort_arr;

	char quantilekey[30];
	for (i=0; i<pb->percentile_size; i++)
	{
		alligator_ht *hash = NULL;

		char metric_name[255];
		if (mnode)
		{
			hash = alligator_ht_init(NULL);
			labels_t *labels = mnode->labels;
			snprintf(metric_name, 255, "%s_quantile", labels->key);
			labels = labels->next;

			for (; labels; labels = labels->next)
			{
				if (!labels->key)
					continue;
				labels_hash_insert(hash, labels->name, labels->key);
			}
		}
		else
		{
			hash = labels_dup(custom_labels);
			snprintf(metric_name, 255, "%s", custom_mname);
		}

		if ( pb->percentile[i] == -1 )
		{
			labels_hash_insert(hash, "quantile", "1.0");
			metric_add(metric_name, hash, &arr[pb->ipercentile[i]], DATATYPE_DOUBLE, carg);
		}
		else
		{
			snprintf(quantilekey, 30, "0.%"d64"", pb->percentile[i]);
			labels_hash_insert(hash, "quantile", quantilekey);
			metric_add(metric_name, hash, &arr[pb->ipercentile[i]], DATATYPE_DOUBLE, carg);
		}
	}
}

void heap_insert(percentile_buffer *pb, double key)
{
	if (pb->cur >= (int64_t)pb->n)
		pb->cur = 0;

	pb->arr[pb->cur] = key;

	if (pb->cur == 0 && pb->arr[1] == 0)
	{
		for (uint64_t i = 1; i < pb->n; ++i)
		{
			pb->arr[i] = key;
		}
	}

	++pb->cur;
	++pb->inserts_since_calc;
}

void free_percentile_buffer(percentile_buffer *pb)
{
	if (!pb)
		return;

	if (pb->ipercentile)
		free(pb->ipercentile);

	if (pb->percentile)
		free(pb->percentile);

	if (pb->arr)
		free(pb->arr);

	if (pb->sort_arr)
		free(pb->sort_arr);

	free(pb);
}

percentile_buffer* init_percentile_buffer_opts(int64_t *percentile, size_t n, int64_t buffer_min, int64_t calc_every)
{
	percentile_buffer *pb = calloc(1, sizeof(*pb));
	int default_percentile = 0;
	if (!pb)
		return NULL;
	if (!percentile || n == 0) {
		default_percentile = 1;
		n = 1;
		percentile = calloc(1, sizeof(*percentile));
		if (!percentile) {
			free(pb);
			return NULL;
		}
		percentile[0] = -1;
	}
	pb->percentile_size = n;
	uint64_t i;

	pb->ipercentile = calloc(n, sizeof(int64_t));
	if (!pb->ipercentile) {
		if (default_percentile)
			free(percentile);
		free(pb);
		return NULL;
	}
	pb->percentile = percentile;

	int64_t percentilelong = percentile[0];
	for (i=0; i<n; i++)
		if (percentilelong < percentile[i])
			percentilelong = percentile[i];
	if (percentilelong <= 0)
		percentilelong = 1;

	int8_t digits = (int64_t)log10(percentilelong) + 1;
	size_t natural_n = (size_t)pow(10, digits);
	pb->n = natural_n;

	/* Resolve floor: mapping override (-1 unset) → global ac fallback */
	int64_t resolved_min = buffer_min;
	if (resolved_min < 0)
		resolved_min = ac ? ac->percentile_buffer_min : 0;
	/*
	 * Floor only: e.g. quantiles 0.9 → natural 10, with percentile_buffer_min 1000
	 * grows to 1000 while label stays "0.9". Fine specs like 0.9000 stay at 10000.
	 */
	if (resolved_min > 0 && (size_t)resolved_min > pb->n)
		pb->n = (size_t)resolved_min;

	int64_t percentilemax = 0;
	for (i=0; i<n; i++)
	{
		if (pb->percentile[i] <= 0)
			pb->ipercentile[i] = 0;
		else
		{
			int8_t percdigits = (int64_t)log10(pb->percentile[i]);
			int64_t curpercentile = pb->percentile[i]*pow(10, digits - percdigits - 1);
			int64_t natural_ip = (int64_t)natural_n - curpercentile;
			if (pb->n == natural_n)
				pb->ipercentile[i] = natural_ip;
			else
				pb->ipercentile[i] = (natural_ip * (int64_t)pb->n) / (int64_t)natural_n;

			if (pb->ipercentile[i] < 0)
				pb->ipercentile[i] = 0;
			if (pb->ipercentile[i] >= (int64_t)pb->n)
				pb->ipercentile[i] = (int64_t)pb->n - 1;

			if (percentilemax < pb->ipercentile[i])
				percentilemax = pb->ipercentile[i];
		}
	}

	pb->cur = 0;
	pb->inserts_since_calc = 0;
	pb->sorted = 0;
	/* Resolve calc interval: mapping override → global; 0 = every insert */
	int64_t resolved_every = calc_every;
	if (resolved_every < 0)
		resolved_every = ac ? ac->percentile_calc_every : 0;
	if (resolved_every < 0)
		resolved_every = 0;
	pb->calc_every = resolved_every;
	for (i=1; i<percentilemax; i*=2);
	if (i >= pb->n)
		pb->sortsize = pb->n;
	else
		pb->sortsize = i;

	glog(L_TRACE, "init_percentile_buffer: n %zu natural %zu sortsize %"d64" calc_every %"d64" for diff %"d64"\n",
		pb->n, natural_n, pb->sortsize, pb->calc_every, percentilemax);

	pb->arr = calloc(pb->n, sizeof(double));
	if (!pb->arr) {
		free(pb->ipercentile);
		free(pb->percentile);
		free(pb);
		return NULL;
	}

	pb->sort_arr = calloc(pb->n, sizeof(double));
	if (!pb->sort_arr) {
		free(pb->arr);
		free(pb->ipercentile);
		free(pb->percentile);
		free(pb);
		return NULL;
	}

	return pb;
}

percentile_buffer* init_percentile_buffer(int64_t *percentile, size_t n)
{
	return init_percentile_buffer_opts(percentile, n, -1, -1);
}

int64_t* percentile_init_3n(int64_t n1, int64_t n2, int64_t n3)
{
	int64_t *percentile = calloc(3, sizeof(int64_t));
	percentile[0] = n1;
	percentile[1] = n2;
	percentile[2] = n3;

	return percentile;
}
