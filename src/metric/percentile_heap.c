#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "metric/percentile_heap.h"
#include "main.h"

void maxheapify(int64_t *arr, int64_t i, int64_t heapsize)
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

void buildmaxheap(int64_t *arr, int64_t n)
{
	int64_t heapsize = n;
	int64_t j;
	for (j = n/2; j >= 0; j--)
		maxheapify(arr, j, heapsize);
}

void heapify(int64_t* arr, int64_t n, int64_t i) 
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
		arr[i] = arr[i] ^ arr[smallest];
		arr[smallest] = arr[i] ^ arr[smallest];
		arr[i] = arr[smallest] ^ arr[i];
  
		heapify(arr, n, smallest); 
	} 
} 

void heapSort(int64_t *arr, int64_t n) 
{ 
	for (int64_t i = n / 2 - 1; i >= 0; i--) 
		heapify(arr, n, i); 
  
	for (int64_t i = n - 1; i >= 0; i--)
	{ 
		if (i)
		{
			arr[0] = arr[0] ^ arr[i];
			arr[i] = arr[0] ^ arr[i];
			arr[0] = arr[i] ^ arr[0];
		}
  
		heapify(arr, i, 0); 
	} 
} 

void calc_percentiles(context_arg *carg, percentile_buffer *pb, metric_node *mnode)
{
	int64_t i;
	int64_t *arr = pb->arr;
	size_t n = pb->n;

	buildmaxheap(arr, n);
	heapSort(arr, pb->sortsize);
	//for (i = 0; i < n; i++)
	//{
	//	printf("%"d64" ", arr[i]);
	//	if ((i == 0) || (i == 2) || (i == 6) || (i == 14) || (i == 30) || (i == 62) || (i == 126) || (i == 254) || (i == 510) || (i == 1022) || (i == 2046) || (i == 4094))
	//		printf("\n\n");
	//}
	//puts("");

	char quantilekey[30];
	for (i=0; i<pb->percentile_size; i++)
	{
		alligator_ht *hash = alligator_ht_init(NULL);

		labels_t *labels = mnode->labels;
		char metric_name[255];
		//printf("labels: %p\n", labels);
		//printf("labels->key: %p\n", labels->key);
		snprintf(metric_name, 255, "%s_quantile", labels->key);
		labels = labels->next;
		for (; labels; labels = labels->next)
		{
			if (!labels->key)
				continue;
			labels_hash_insert(hash, labels->name, labels->key);
			//printf("labels name %s, key %s\n", labels->name, labels->key);
		}

		if ( pb->percentile[i] == -1 )
		{
			//printf("p1.0[%"d64"]   %"d64"\n", pb->ipercentile[i], arr[pb->ipercentile[i]]);
			labels_hash_insert(hash, "quantile", "1.0");
			metric_add(metric_name, hash, &arr[pb->ipercentile[i]], DATATYPE_INT, carg);
		}
		else
		{
			//printf("p0.%"d64"[%"d64"]   %"d64"\n", pb->percentile[i], pb->ipercentile[i], arr[pb->ipercentile[i]]);
			snprintf(quantilekey, 30, "0.%"d64"", pb->percentile[i]);
			labels_hash_insert(hash, "quantile", quantilekey);
			metric_add(metric_name, hash, &arr[pb->ipercentile[i]], DATATYPE_INT, carg);
		}
	}
}

void heap_insert(percentile_buffer *pb, int64_t key)
{
	if (pb->cur >= pb->n)
		pb->cur = 0;

	pb->arr[pb->cur] = key;

	++pb->cur;
}

void free_percentile_buffer(percentile_buffer *pb)
{
	if (pb->ipercentile)
		free(pb->ipercentile);

	if (pb->percentile)
		free(pb->percentile);

	if (pb->arr)
		free(pb->arr);

	free(pb);
}

percentile_buffer* init_percentile_buffer(int64_t *percentile, size_t n)
{
	percentile_buffer *pb = calloc(1, sizeof(*pb));
	pb->percentile_size = n;
	uint64_t i;

	pb->ipercentile = calloc(n, sizeof(int64_t));
	pb->percentile = percentile;

	int64_t percentilelong = percentile[0];
	for (i=0; i<n; i++)
		if (percentilelong < percentile[i])
			percentilelong = percentile[i];

	int8_t digits = (int64_t)log10(percentilelong) + 1;
	pb->n = pow(10, digits);

	int64_t percentilemax = 0;
	for (i=0; i<n; i++)
	{
		if (pb->percentile[i] == -1)
			pb->ipercentile[i] = 0;
		else
		{
			int8_t percdigits = (int64_t)log10(pb->percentile[i]);
			int64_t curpercentile = pb->percentile[i]*pow(10, digits - percdigits - 1);
			pb->ipercentile[i] = pb->n - curpercentile;

			if (percentilemax < pb->ipercentile[i])
				percentilemax = pb->ipercentile[i];
		}
	}

	pb->cur = 0;
	for (i=1; i<percentilemax; i*=2);
	if (i >= pb->n)
		pb->sortsize = pb->n;
	else
		pb->sortsize = i;

	extern aconf* ac;
	if (ac->log_level > 3)
		printf("sortsize %"d64" for diff %"d64"\n", pb->sortsize, percentilemax);

	pb->arr = calloc(pb->n, sizeof(int64_t));

	return pb;
}

// ADD PERCEntiLES CONF
//	int64_t *percentile = calloc(4, sizeof(int64_t));
//	percentile[0] = 999;
//	percentile[1] = 99;
//	percentile[2] = 90;
//	percentile[3] = -1;
//
// INIT PERCENTILE BUFFER
//	percentile_buffer *pb = init_percentile_buffer(percentile, 4);
//
// INSERT VALUE TO HEAP
//	int64_t a = 10;
//	heap_insert(pb, a);
//
// CALC PERCENTILES
//	calc_percentiles(pb);
//
// FREE PERCENTILE BUFFER
//	free_percentile_buffer(pb);
