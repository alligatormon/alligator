#include "main.h"

void calc_buckets(mapping_metric *mm, metric_node *mnode, int64_t ival)
{
	int64_t bucket_size = mm->bucket_size;
	char bucketkey[30];

	int64_t i;
	int64_t pre_key = 0;
	for (i=0; i<bucket_size; i++)
	{
		tommy_hashdyn *hash = malloc(sizeof(*hash));
		tommy_hashdyn_init(hash);

		labels_t *labels = mnode->labels;
		char metric_name[255];
		snprintf(metric_name, 255, "%s_bucket", labels->key);
		labels = labels->next;
		for (; labels; labels = labels->next)
		{
			if (!labels->key)
				continue;
			labels_hash_insert(hash, labels->name, labels->key);
		}

		if (ival < mm->bucket[i] && ival > pre_key)
		{
			snprintf(bucketkey, 30, "%"d64"", mm->bucket[i]);
			labels_hash_insert(hash, "bucket", bucketkey);
			int64_t val = 1;
			metric_update(metric_name, hash, &val, DATATYPE_INT, 0);
		}
		else if (ival > mm->bucket[i] && i+1 == bucket_size)
		{
			labels_hash_insert(hash, "bucket", "+Inf");
			int64_t val = 1;
			metric_update(metric_name, hash, &val, DATATYPE_INT, 0);
		}
		else
		{
			tommy_hashdyn_done(hash);
			free(hash);
		}

		pre_key = mm->bucket[i];
	}
}

void calc_buckets_cumulative(mapping_metric *mm, metric_node *mnode, int64_t ival)
{
	int64_t le_size = mm->le_size;
	char lekey[30];

	int64_t i;
	int8_t inf = 0;
	for (i=0; i<le_size; i++)
	{
		tommy_hashdyn *hash = malloc(sizeof(*hash));
		tommy_hashdyn_init(hash);

		labels_t *labels = mnode->labels;
		char metric_name[255];
		snprintf(metric_name, 255, "%s_le", labels->key);
		labels = labels->next;
		for (; labels; labels = labels->next)
		{
			if (!labels->key)
				continue;
			labels_hash_insert(hash, labels->name, labels->key);
		}

		if (ival < mm->le[i])
		{
			snprintf(lekey, 30, "%"d64"", mm->le[i]);
			labels_hash_insert(hash, "le", lekey);
			int64_t val = 1;
			metric_update(metric_name, hash, &val, DATATYPE_INT, 0);
		}
		else if (ival > mm->le[i] && i+1 == le_size && !inf)
		{
			labels_hash_insert(hash, "le", "+Inf");
			int64_t val = 1;
			metric_update(metric_name, hash, &val, DATATYPE_INT, 0);
			inf = 1;
		}
		else
		{
			tommy_hashdyn_done(hash);
			free(hash);
		}
	}
}

void mapping_processing(mapping_metric *mm, metric_node *mnode, int64_t ival)
{
	if (mm->percentile)
	{
		if (!mnode->pb)
			mnode->pb = init_percentile_buffer(mm->percentile, mm->percentile_size);

		heap_insert(mnode->pb, ival);
		calc_percentiles(mnode->pb, mnode);
	}
	else if (mm->le)
		calc_buckets_cumulative(mm, mnode, ival);
	else if (mm->bucket)
		calc_buckets(mm, mnode, ival);
}

