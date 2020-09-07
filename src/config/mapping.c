#include "common/selector.h"
#include "config/context.h"
#include <string.h>
#include "config/mapping.h"

int mapping_bucket_compare(const void *i, const void *j)
{
	return *(int64_t *)i - *(int64_t *)j;
}

mapping_metric* context_mapping_parser(mtlen *mt, int64_t *i)
{
	++*i;

	mapping_metric *mm = calloc(1, sizeof(*mm));


	char *ptr;

	for (; *i<mt->m && !config_compare(mt, *i, "}", 1); ++*i)
	{
		//printf("i: %lld, 1:'%s', 2:'%s', 3:'%s'\n", *i, mt->st[*i].s, mt->st[*i+1].s, mt->st[*i+2].s);
		if (config_compare_begin(mt, *i, "name", 4))
		{
			ptr = config_get_arg(mt, *i, 1, NULL);
			if (ptr)
				mm->metric_name = strdup(ptr);
			else
				fprintf(stderr, "error parse name\n");
		}
		else if (config_compare_begin(mt, *i, "template", 8))
		{
			ptr = config_get_arg(mt, *i, 1, &mm->template_len);
			if (ptr)
				mm->template = strdup(ptr);
			else
				fprintf(stderr, "error parse template\n");
		}
		else if (config_compare_begin(mt, *i, "label", 5))
		{
			char *label_name = 0;
			char *label_key = 0;

			ptr = config_get_arg(mt, *i, 1, NULL);
			if (ptr)
				label_name = strdup(ptr);
			else
				fprintf(stderr, "error parse label_name\n");

			ptr = config_get_arg(mt, *i, 2, NULL);
			if (ptr)
				label_key = strdup(ptr);
			else
				fprintf(stderr, "error parse label_key\n");

			if (mm->label_head)
			{
				mapping_label *ml = mm->label_tail->next = malloc(sizeof(mapping_label));
				mm->label_tail = ml;
				ml->name = label_name;
				ml->key = label_key;
				ml->next = 0;
			}
			else
			{
				mapping_label *ml = mm->label_head = mm->label_tail = malloc(sizeof(mapping_label));
				ml->name = label_name;
				ml->key = label_key;
				ml->next = 0;
			}
		}
		else if (config_compare_begin(mt, *i, "match", 5))
		{
			char *ptr = config_get_arg(mt, *i, 1, NULL);
			if (ptr && !strncmp(ptr, "glob", 4))
				mm->match = MAPPING_MATCH_GLOB;
			else if (ptr && !strncmp(ptr, "pcre", 4))
				mm->match = MAPPING_MATCH_PCRE;
		}
		else if (config_compare_begin(mt, *i, "quantiles", 9))
		{
			int64_t j;
			size_t quantiles_size = config_get_field_size(mt, *i+1);
			int64_t *percentile = calloc(quantiles_size, sizeof(int64_t));
			for (j=0; j<quantiles_size; j++)
			{
				char *ptr = config_get_arg(mt, *i, j+1, NULL);
				if (ptr[0] > '0')
					percentile[j] = -1;
				else
					percentile[j] = atoll(ptr+2);
			}
			*i+=j;
			mm->percentile = percentile;
			mm->percentile_size = quantiles_size;
		}
		else if (config_compare_begin(mt, *i, "buckets", 7))
		{
			int64_t j;
			size_t buckets_size = config_get_field_size(mt, *i+1);
			int64_t *bucket = calloc(buckets_size, sizeof(int64_t));
			for (j=0; j<buckets_size; j++)
			{
				char *ptr = config_get_arg(mt, *i, j+1, NULL);
				bucket[j] = atoll(ptr);
			}
			*i+=j;

			qsort(bucket, buckets_size, sizeof(int64_t), mapping_bucket_compare);

			mm->bucket = bucket;
			mm->bucket_size = buckets_size;
		}
		else if (config_compare_begin(mt, *i, "le", 2))
		{
			int64_t j;
			size_t les_size = config_get_field_size(mt, *i+1);
			int64_t *le = calloc(les_size, sizeof(int64_t));
			for (j=0; j<les_size; j++)
			{
				char *ptr = config_get_arg(mt, *i, j+1, NULL);
				le[j] = atoll(ptr);
			}
			*i+=j;
			mm->le = le;
			mm->le_size = les_size;
		}
	}

	if (mm->match == MAPPING_MATCH_GLOB)
	{
		uint64_t k;
		uint64_t globs = 1;
		uint64_t wildcard = 0;
		uint64_t offset = 0;
		uint64_t glob_size;
		size_t template_len = mm->template_len;
		char *template = mm->template;
		for (k=0; k<template_len; ++k)
		{
			if (template[k] == '.')
				++globs;
			if (template[k] == '*')
				++wildcard;
		}
		mm->glob_size = globs;
		mm->glob = malloc(globs*sizeof(void*));
		mm->wildcard = wildcard;
		for (k=0; k<globs; ++k)
		{
			glob_size = strcspn(template+offset, ".");
			mm->glob[k] = strndup(template+offset, glob_size);
			offset += glob_size+1;
		}
	}

	return mm;
}

mapping_metric* json_mapping_parser(json_t *mapping)
{
	mapping_metric *mm = NULL;
	if (!mapping)
		return mm;

	json_t *template = json_object_get(mapping, "template");
	if (!template)
		return mm;

	mm->template = (char*)json_string_value(template);

	json_t *name = json_object_get(mapping, "name");
	mm->metric_name = (char*)json_string_value(name);

	json_t *match = json_object_get(mapping, "match");
	char *match_str = (char*)json_string_value(match);
	if (match_str && !strcmp(match_str, "glob"))
		mm->match = MAPPING_MATCH_GLOB;
	else if (match_str && !strcmp(match_str, "pcre"))
		mm->match = MAPPING_MATCH_PCRE;
	if (match_str && !strcmp(match_str, "grok"))
		mm->match = MAPPING_MATCH_GROK;
	else
		mm->match = MAPPING_MATCH_GLOB;

	json_t *bucket = json_object_get(mapping, "bucket");
	uint64_t bucket_size = json_array_size(bucket);
	if (bucket_size)
	{
		int64_t *buckets = calloc(bucket_size, sizeof(int64_t));
		for (uint64_t i = 0; i < bucket_size; i++)
		{
			json_t *bucket_obj = json_array_get(bucket, i);
			buckets[i] = json_integer_value(bucket_obj);
		}

		qsort(buckets, bucket_size, sizeof(int64_t), mapping_bucket_compare);
		mm->bucket = buckets;
		mm->bucket_size = bucket_size;
	}

	json_t *le = json_object_get(mapping, "le");
	uint64_t le_size = json_array_size(le);
	if (le_size)
	{
		int64_t *les = calloc(le_size, sizeof(int64_t));
		for (uint64_t i = 0; i < le_size; i++)
		{
			json_t *le_obj = json_array_get(le, i);
			les[i] = json_integer_value(le_obj);
		}

		mm->le = les;
		mm->le_size = le_size;
	}

	json_t *percentile = json_object_get(mapping, "quantile");
	uint64_t percentile_size = json_array_size(percentile);
	if (percentile_size)
	{
		int64_t *percentiles = calloc(percentile_size, sizeof(int64_t));
		for (uint64_t i = 0; i < percentile_size; i++)
		{
			json_t *percentile_obj = json_array_get(percentile, i);
			char *percentile_str = (char*)json_string_value(percentile_obj);
			int64_t percentile_int;
			if (percentile_str[0] > 0)
				percentile_int = -1;
			else
				percentile_int = strtoll(percentile_str+2, NULL, 10);
			percentiles[i] = percentile_int;
		}

		mm->percentile = percentiles;
		mm->percentile_size = percentile_size;
	}

	json_t *label = json_object_get(mapping, "label");
	const char *label_key;
	json_t *label_value;
	json_object_foreach(label, label_key, label_value)
	{
		char *label_value_str = (char*)json_string_value(label_value);

		mapping_label *ml;
		if (mm->label_head)
		{
			ml = mm->label_tail->next = malloc(sizeof(mapping_label));
			mm->label_tail = ml;
		}
		else
			ml = mm->label_head = mm->label_tail = malloc(sizeof(mapping_label));

		ml->name = strdup(label_key);
		ml->key = strdup(label_value_str);
		ml->next = 0;
	}

	return mm;
}

void push_mapping_metric(mapping_metric *dest, mapping_metric *source)
{
	for (; dest->next; dest = dest->next);
	dest->next = source;
	source->next = 0;
}
