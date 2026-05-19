#include "common/selector.h"
#include "common/logs.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "mapping/type.h"

int mapping_bucket_compare(const void *i, const void *j)
{
	return *(int64_t *)i - *(int64_t *)j;
}

int64_t mapping_quantile_from_json(json_t *obj)
{
	if (!obj)
		return 0;

	if (json_is_string(obj))
	{
		const char *percentile_str = json_string_value(obj);
		if (!percentile_str || !percentile_str[0])
			return 0;
		if (percentile_str[0] > '0')
			return -1;
		if (percentile_str[0] == '0' && percentile_str[1] == '.')
			return strtoll(percentile_str + 2, NULL, 10);
		return 0;
	}

	if (json_is_real(obj) || json_is_integer(obj))
	{
		double val = json_number_value(obj);
		if (val >= 1.0)
			return -1;

		char buf[32];
		snprintf(buf, sizeof(buf), "%.10g", val);
		if (buf[0] > '0')
			return -1;
		if (buf[0] == '0' && buf[1] == '.')
			return strtoll(buf + 2, NULL, 10);
	}

	return 0;
}

json_t *mapping_quantile_to_json(int64_t percentile)
{
	if (percentile == -1)
		return json_real(1.0);

	char buf[32];
	snprintf(buf, sizeof(buf), "0.%" PRId64, percentile);
	return json_real(strtod(buf, NULL));
}

static const char *mapping_match_name(uint64_t match)
{
	switch (match) {
	case MAPPING_MATCH_PCRE:
		return "pcre";
	case MAPPING_MATCH_GROK:
		return "grok";
	default:
		return "glob";
	}
}

json_t *mapping_metric_to_json(mapping_metric *mm)
{
	json_t *obj = json_object();
	if (!mm || !obj)
		return obj;

	if (mm->template)
		json_object_set_new(obj, "template", json_string(mm->template));
	if (mm->metric_name)
		json_object_set_new(obj, "name", json_string(mm->metric_name));

	if (mm->match != MAPPING_MATCH_GLOB || mm->glob_size)
		json_object_set_new(obj, "match", json_string(mapping_match_name(mm->match)));

	if (mm->bucket_size)
	{
		json_t *buckets = json_array();
		for (uint64_t i = 0; i < mm->bucket_size; i++)
			json_array_append_new(buckets, json_real(mm->bucket[i]));
		json_object_set_new(obj, "buckets", buckets);
	}

	if (mm->le_size)
	{
		json_t *les = json_array();
		for (uint64_t i = 0; i < mm->le_size; i++)
			json_array_append_new(les, json_real(mm->le[i]));
		json_object_set_new(obj, "le", les);
	}

	if (mm->percentile_size)
	{
		json_t *quantiles = json_array();
		for (uint64_t i = 0; i < mm->percentile_size; i++)
			json_array_append_new(quantiles, mapping_quantile_to_json(mm->percentile[i]));
		json_object_set_new(obj, "quantiles", quantiles);
	}

	if (mm->label_head)
	{
		json_t *label = json_object();
		for (mapping_label *ml = mm->label_head; ml; ml = ml->next)
		{
			if (ml->name && ml->key)
				json_object_set_new(label, ml->name, json_string(ml->key));
		}
		json_object_set_new(obj, "label", label);
	}

	return obj;
}

json_t *mapping_metric_list_to_json(mapping_metric *mm)
{
	json_t *arr = json_array();
	if (!arr)
		return NULL;

	for (; mm; mm = mm->next)
		json_array_append_new(arr, mapping_metric_to_json(mm));

	return arr;
}

mapping_metric* json_mapping_parser(json_t *mapping)
{
	mapping_metric *mm = calloc(1, sizeof(*mm));
	if (!mapping)
		return mm;

	json_t *template = json_object_get(mapping, "template");
	if (!template)
		return mm;

	mm->template = strdup((char*)json_string_value(template));
	size_t template_len = mm->template_len = json_string_length(template);

	json_t *name = json_object_get(mapping, "name");
	if (name)
		mm->metric_name = strdup((char*)json_string_value(name));

	json_t *match = json_object_get(mapping, "match");
	char *match_str = NULL;
	if (match)
		match_str = strdup((char*)json_string_value(match));
	if (match_str && !strcmp(match_str, "glob"))
	{
		mm->match = MAPPING_MATCH_GLOB;

		uint64_t i;
		char *tmp;
		for (i = 0, tmp = mm->template; tmp - mm->template < template_len; i++)
		{
			tmp += strcspn(tmp, ".");
			tmp += strspn(tmp, ".");
		}

		mm->glob_size = i;
		mm->glob = malloc(mm->glob_size*sizeof(void*));

		for (i = 0, tmp = mm->template; tmp - mm->template < template_len; i++)
		{
			uint64_t glob_len = strcspn(tmp, ".");
			mm->glob[i] = strndup(tmp, glob_len);
			tmp += glob_len;
			tmp += strspn(tmp, ".");
		}
	}
	else if (match_str && !strcmp(match_str, "pcre"))
		mm->match = MAPPING_MATCH_PCRE;
	if (match_str && !strcmp(match_str, "grok"))
		mm->match = MAPPING_MATCH_GROK;
	else
		mm->match = MAPPING_MATCH_GLOB;

	json_t *bucket = json_object_get(mapping, "buckets");
	uint64_t bucket_size = json_array_size(bucket);
	if (bucket_size)
	{
		double *buckets = calloc(bucket_size, sizeof(double));
		for (uint64_t i = 0; i < bucket_size; i++)
		{
			json_t *bucket_obj = json_array_get(bucket, i);
			buckets[i] = json_real_value(bucket_obj);
		}

		qsort(buckets, bucket_size, sizeof(int64_t), mapping_bucket_compare);
		mm->bucket = buckets;
		mm->bucket_size = bucket_size;
	}

	json_t *le = json_object_get(mapping, "le");
	uint64_t le_size = json_array_size(le);
	if (le_size)
	{
		double *les = calloc(le_size, sizeof(double));
		for (uint64_t i = 0; i < le_size; i++)
		{
			json_t *le_obj = json_array_get(le, i);
			les[i] = json_real_value(le_obj);
		}

		mm->le = les;
		mm->le_size = le_size;
	}

	json_t *percentile = json_object_get(mapping, "quantiles");
	uint64_t percentile_size = json_array_size(percentile);
	if (percentile_size)
	{
		int64_t *percentiles = calloc(percentile_size, sizeof(int64_t));
		for (uint64_t i = 0; i < percentile_size; i++)
		{
			json_t *percentile_obj = json_array_get(percentile, i);
			percentiles[i] = mapping_quantile_from_json(percentile_obj);
		}

		mm->percentile = percentiles;
		mm->percentile_size = percentile_size;
	}

	json_t *label = json_object_get(mapping, "label");
	if (label)
	{
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
			glog(L_INFO, "\tcreated label [%p] for template '%s': '%s'='%s', next is %p\n", ml, mm->template, ml->name, ml->key, ml->next);
		}
	}

	glog(L_INFO, "created mapping %p from template '%s' with ml %p\n", mm, mm->template, mm->label_head);
	return mm;
}

void push_mapping_metric(mapping_metric *dest, mapping_metric *source)
{
	for (; dest->next; dest = dest->next);
	dest->next = source;
	source->next = 0;
}

void mm_list(mapping_metric *dest)
{
	for (; dest; dest = dest->next) {
		printf("mm(%p)->%p mm_list %s %s\n", dest, dest->next, dest->metric_name, dest->template);
	}
}

mapping_metric* mapping_copy(mapping_metric *src)
{
	if (!src)
		return NULL;

	mapping_metric *mm = calloc(1, sizeof(*mm)), *ret_mm = mm;

	for (; src; ) {
		mm->metric_name = src->metric_name ? strdup(src->metric_name) : NULL;
		mm->template = src->template ? strdup(src->template) : NULL;
		mm->template_len = src->template_len;
		mm->match = src->match;
		mm->wildcard = src->wildcard;

		if (src->glob_size)
		{
			mm->glob_size = src->glob_size;
			mm->glob = malloc(mm->glob_size * sizeof(char *));
			for (size_t i = 0; i < mm->glob_size; i++)
				mm->glob[i] = src->glob[i] ? strdup(src->glob[i]) : NULL;
		}

		if (src->percentile_size)
		{
			mm->percentile_size = src->percentile_size;
			mm->percentile = calloc(1, sizeof(int64_t) * src->percentile_size);
			memcpy(mm->percentile, src->percentile, sizeof(int64_t) * src->percentile_size);
		}

		if (src->bucket_size)
		{
			mm->bucket_size = src->bucket_size;
			mm->bucket = calloc(1, sizeof(int64_t) * src->bucket_size);
			memcpy(mm->bucket, src->bucket, sizeof(int64_t) * src->bucket_size);
		}

		if (src->le_size)
		{
			mm->le_size = src->le_size;
			mm->le = calloc(1, sizeof(int64_t) * src->le_size);
			memcpy(mm->le, src->le, sizeof(int64_t) * src->le_size);
		}

		if (src->label_head)
		{
			mapping_label *srcml = src->label_head;
			mm->label_head = malloc(sizeof(mapping_label));
			mapping_label *dstml = mm->label_head;
			mapping_label *tail = NULL;
			while (srcml) {
				tail = dstml;
				dstml->name = strdup(srcml->name);
				dstml->key = strdup(srcml->key);

				if (srcml->next)
					dstml->next = malloc(sizeof(mapping_label));
				else {
					dstml->next = NULL;
					break;
				}

				srcml = srcml->next;
				dstml = dstml->next;
			}

			mm->label_tail = tail;
		}

		if (!src->next) {
			break;
		}

		mm->next = calloc(1, sizeof(*mm));
		mm = mm->next;
		src = src->next;
	}

	return ret_mm;
}

void mapping_free(mapping_metric *src)
{
	if (!src)
		return;

	free(src->metric_name);
	free(src->template);

	if (src->percentile_size)
	{
		free(src->percentile);
		src->percentile = NULL;
	}

	if (src->bucket_size)
	{
		free(src->bucket);
		src->bucket = NULL;
	}

	if (src->le_size)
	{
		free(src->le);
		src->le = NULL;
	}

	if (src->label_head)
	{
		mapping_label *ml = src->label_head;
		while (ml) {
			free(ml->name);
			free(ml->key);
			mapping_label *prev = ml;
			ml = ml->next;
			free(prev);
		}
		src->label_head = NULL;
		src->label_tail = NULL;
	}
}

void mapping_free_recurse(mapping_metric *src) {
	if (!src)
		return;

	for (; src; ) {
		mapping_metric *prev = src;
		mapping_free(src);
		if (!src->next) {
			free(prev);
			break;
		}
		src = src->next;
		free(prev);
	}
}
