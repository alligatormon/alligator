#include "common/selector.h"
#include "config/context.h"
#include <string.h>

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

void push_mapping_metric(mapping_metric *dest, mapping_metric *source)
{
	for (; dest->next; dest = dest->next);
	dest->next = source;
	source->next = 0;
}
