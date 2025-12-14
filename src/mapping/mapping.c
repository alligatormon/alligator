#include "main.h"

void calc_buckets(context_arg *carg, mapping_metric *mm, metric_node *mnode, double dval)
{
	int64_t bucket_size = mm->bucket_size;
	char bucketkey[30];

	int64_t i;
	double pre_key = 0;
	for (i=0; i<bucket_size; i++)
	{
		alligator_ht *hash = alligator_ht_init(NULL);

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

		if (dval < mm->bucket[i] && dval >= pre_key)
		{
			snprintf(bucketkey, 30, "%lg", mm->bucket[i]);
			labels_hash_insert(hash, "bucket", bucketkey);
			int64_t val = 1;
			metric_update(metric_name, hash, &val, DATATYPE_INT, carg);
		}
		else if (dval > mm->bucket[i] && i+1 == bucket_size)
		{
			labels_hash_insert(hash, "bucket", "+Inf");
			int64_t val = 1;
			metric_update(metric_name, hash, &val, DATATYPE_INT, carg);
		}
		else
		{
			alligator_ht_done(hash);
			free(hash);
		}

		pre_key = mm->bucket[i];
	}
}

void calc_buckets_cumulative(context_arg *carg, mapping_metric *mm, metric_node *mnode, double dval)
{
	int64_t le_size = mm->le_size;
	char lekey[30];

	int64_t i;
	int8_t inf = 0;
	for (i=0; i<le_size; i++)
	{
		alligator_ht *hash = alligator_ht_init(NULL);

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

		if (dval < mm->le[i])
		{
			snprintf(lekey, 30, "%lg", mm->le[i]);
			labels_hash_insert(hash, "le", lekey);
			int64_t val = 1;
			metric_update(metric_name, hash, &val, DATATYPE_INT, carg);
		}
		else if (dval > mm->le[i] && i+1 == le_size && !inf)
		{
			labels_hash_insert(hash, "le", "+Inf");
			int64_t val = 1;
			metric_update(metric_name, hash, &val, DATATYPE_INT, carg);
			inf = 1;
		}
		else
		{
			alligator_ht_done(hash);
			free(hash);
		}
	}
}

void mapping_processing(context_arg *carg, metric_node *mnode, double dval)
{
	if (!carg)
		return;
	if (!carg->mm)
		return;

	mapping_metric *mm = carg->mm;
	for (; mm; ) {
		if (strcmp(mnode->labels->key, mm->metric_name))
		{
			mm = mm->next;
			continue;
		}

		if (mm->percentile)
		{
			if (!mnode->pb)
				mnode->pb = init_percentile_buffer(mm->percentile, mm->percentile_size);

			//printf("inserted heap %p with dval %f\n", mnode->pb, dval);
			heap_insert(mnode->pb, dval);
			calc_percentiles(carg, mnode->pb, mnode, NULL, NULL);
		}
		else if (mm->le)
			calc_buckets_cumulative(carg, mm, mnode, dval);
		else if (mm->bucket)
			calc_buckets(carg, mm, mnode, dval);

		mm = mm->next;
	}
}

