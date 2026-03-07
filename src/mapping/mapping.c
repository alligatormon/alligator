#include "main.h"
#include "mapping/mapping.h"

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

int match_and_extract(const char *pattern, const char *str, char *fields[], int *field_count)
{
    char pat[1024];
    char txt[1024];

    strncpy(pat, pattern, sizeof(pat));
    strncpy(txt, str, sizeof(txt));

    char *pat_ctx;
    char *str_ctx;

    char *p = strtok_r(pat, ".", &pat_ctx);
    char *s = strtok_r(txt, ".", &str_ctx);

    *field_count = 0;

    while (p && s)
    {
        if (strcmp(p, "*") == 0)
        {
            fields[*field_count] = s;
            (*field_count)++;
        }
        else if (strcmp(p, s) != 0)
        {
            return 0;
        }


        p = strtok_r(NULL, ".", &pat_ctx);
        s = strtok_r(NULL, ".", &str_ctx);
    }

    return (!p && !s);
}

void template_render(const char *template, char *fields[], int field_count, char *output)
{
    const char *t = template;
    char *o = output;

    while (*t)
    {
        if (*t == '$')
        {
            t++;

            int index = -1;

            if (*t == '{')
            {
                t++;

                index = 0;
                while (isdigit(*t))
                {
                    index = index * 10 + (*t - '0');
                    t++;
                }

                if (*t == '}')
                    t++;
            }
            else if (isdigit(*t))
            {
                index = *t - '0';
                t++;
            }

            index--; // convert to 0-based

            if (index >= 0 && index < field_count)
            {
                const char *f = fields[index];
                while (*f)
                    *o++ = *f++;
            }
        }
        else
        {
            *o++ = *t++;
        }
    }

    *o = 0;
}

void mapping_processing(context_arg *carg, metric_node *mnode, double dval)
{
	if (!carg)
		return;
	if (!carg->mm)
		return;

	mapping_metric *mm = carg->mm;
	for (; mm; ) {
		char *fields[MAPPING_MAX_EXTRACT_FIELDS];
		int num_fields;
		if (!match_and_extract(mm->template, mnode->labels->key, fields, &num_fields))
		{
			//printf("not matched: template %s, labels->key %s\n", mm->template, mnode->labels->key);
			mm = mm->next;
			continue;
		}


		//printf("matched mapping processing: %p, next %p, metric_name %s, labels->key %s\n", mm, mm->next, mm->template, mnode->labels->key);
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
