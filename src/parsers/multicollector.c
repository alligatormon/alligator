#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "parsers/pushgateway.h"
#include "parsers/http_proto.h"
#include "common/reject.h"
#include "config/mapping.h"
#include "common/http.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "cluster/pass.h"
#include "parsers/metric_types.h"
#include "common/logs.h"
#include "main.h"
#define METRIC_NAME_SIZE 255
#define MAX_LABEL_COUNT 10

typedef struct metric_datatypes {
	char *key;
	uint8_t type;

	tommy_node node;
} metric_datatypes;

void metric_datatype_foreach(void *arg)
{
	metric_datatypes *dt = arg;
	free(dt->key);
	free(dt);
}

int metric_datatypes_compare(const void* arg, const void* obj)
{
	char* s1 = (char*)arg;
	char* s2 = ((metric_datatypes*)obj)->key;
	return strcmp(s1, s2);
}


// metric name is a label value
int multicollector_get_metric_name(uint64_t *cur, const char *str, const size_t size, char *s2)
{
	char *ptr_to_start = strstr(str+*cur, "{");
	uint64_t syms = 0;
	if (ptr_to_start)
	{
		syms = strcspn(str+*cur, "\t\r={},# ");
	}
	else
	{
		syms = strcspn(str+*cur, "\t\r=:{},# ");
	}

	strlcpy(s2, str+*cur, syms + 1);
	*cur = *cur + syms;

	return syms;
}

// label name
uint64_t multicollector_get_name(uint64_t *cur, const char *str, const size_t size, char *s2)
{
	uint64_t syms = strcspn(str+*cur, "\t\r:={},# ");
	strlcpy(s2, str+*cur, syms + 1);
	*cur = *cur + syms;

	return syms;
}

// label value
int multicollector_get_value(uint64_t *cur, const char *str, const size_t size, char *s2)
{
	uint64_t syms = strcspn(str+*cur, "\t\r{},# ");
	strlcpy(s2, str+*cur, syms + 1);
	*cur = *cur + syms;

	return syms;
}

int multicollector_get_quotes(uint64_t *cur, const char *str, const size_t size, char *s2)
{
	if (str[*cur] != '"')
		return 0;

	++*cur;
	uint64_t syms = strcspn(str+*cur, "\"");
	uint64_t copy_size = syms > METRIC_NAME_SIZE ? METRIC_NAME_SIZE : syms + 1;
	strlcpy(s2, str+*cur, copy_size);
	*cur = *cur + syms + 1;

	return syms;
}

int multicollector_skip_spaces(uint64_t *cur, const char *str, const size_t size)
{

	uint64_t syms = strspn(str+*cur, "\t\r ");
	*cur = *cur + syms;

	return syms;
}

extern uint64_t fgets_counter;

int char_fgets(char *str, char *buf, int64_t *cnt, size_t len, context_arg *carg)
{
	if (*cnt >= len)
	{
		strncpy(buf, "\0", 1);
		return 0;
	}
	int64_t i = strcspn(str+(*cnt), "\n");

	r_time start_split_data = setrtime();

	memcpy(buf, str+(*cnt), i);
	buf[i] = 0;

	if (carg)
	{
		r_time end_split_data = setrtime();
		carg->push_split_data += getrtime_ns(start_split_data, end_split_data);
	}
	i++;
	*cnt += i;
	return i;
}

int8_t aglob(char *cmp1, char *cmp2)
{
	if (*cmp2 == '*')
		return 2;
	else if (!strcmp(cmp1, cmp2))
		return 1;
	else
		return 0;
}

void free_mapping_split_free(char **split, size_t len)
{
	uint64_t k;
	for (k=0; k<len; free(split[k++]));
	//for (k=0; k<len; printf("free %p (%"d64"/%zu)\n", split[k], k, len), free(split[k++])); #TODO: fix mapping statsd
	free(split);
}

char** mapping_str_split(char *str, size_t len, size_t *out_len, char **template_split, size_t template_split_size, uint64_t *arr)
{
	uint64_t k;
	uint64_t d = 0;
	size_t globs = 1;
	uint64_t offset = 0;
	size_t glob_size;
	*out_len = 0;

	for (k=0; k<len; ++k)
	{
		if (str[k] == '.')
			++globs;
	}

	if (!globs)
		return NULL;

	if (template_split && template_split_size < globs)
		return NULL;


	char **ret = malloc((globs+1)*sizeof(char*));

	uint64_t arr_i = 0;
	for (k=0; k<globs; ++k)
	{
		glob_size = strcspn(str+offset, ".");
		char *tmpret = strndup(str+offset, glob_size);
		
		//ret[k] = strndup(str+offset, glob_size);
		if (template_split)
		{
			//if (template_split[k][0] != '*')
			//{
				ret[d++] = tmpret;
			//}
			if (arr && template_split[k][0] == '*')
			{
				arr[arr_i++] = k;
			}
		}
		else
		{
			ret[k] = tmpret;
		}
		offset += glob_size+1;
	}
	ret[k] = NULL;
	if (d)
		*out_len = d;
	else
		*out_len = k;

	return ret;
}

char** mapping_match(mapping_metric *mm, char *str, size_t size, size_t *split_size, uint64_t *arr)
{
	char **split = 0;
	if (mm->match == MAPPING_MATCH_GLOB)
	{
		uint8_t i;
		size_t str_splits;
		size_t template_splitsize;

		char **template_split = mapping_str_split(mm->template, mm->template_len, &template_splitsize, NULL, 0, NULL);
		split = mapping_str_split(str, size, &str_splits, template_split, template_splitsize, arr);
		if (!split)
		{
			free_mapping_split_free(template_split, template_splitsize);
			return 0;
		}
		if (str_splits != mm->glob_size)
		{
			free_mapping_split_free(template_split, template_splitsize);
			free_mapping_split_free(split, str_splits);
			return 0;
		}

		//for (i=0; i<mm->glob_size; ++i)
		for (i=0; i<str_splits; ++i)
		{
			int8_t selector = aglob(split[i], mm->glob[i]);
			if (!selector)
			{
				free_mapping_split_free(template_split, template_splitsize);
				free_mapping_split_free(split, str_splits);
				return 0;
			}
		}
		*split_size = str_splits;
		free_mapping_split_free(template_split, template_splitsize);
	}
	else if (mm->match == MAPPING_MATCH_PCRE)
	{
	}

	return split;
}

size_t mapping_template(context_arg *carg, char *dst, char *src, size_t size, char **metric_split, uint64_t *arr)
{
	size_t ret = 0;

	//printf("<<<<<< %s, maxsize %zu\n", src, size);
	char *cur = src;
	char *oldcur = cur;
	char *updatecur;
	uint64_t index;
	uint64_t csym = 0;
	*dst = 0;
	while (cur)
	{
		updatecur = strstr(cur, "$");
		if (updatecur)
			cur = updatecur;
		else
			cur += strlen(cur);

		carglog(carg, L_TRACE, "<<<<< found (non template data) \"$ '%s' (%p)\n", cur, cur);

		size_t copysize = cur - oldcur;
		carglog(carg, L_TRACE, "<<<< copy non template %s with %zu syms to %"u64" ptr\n", oldcur, copysize, csym);
		strncpy(dst+csym, oldcur, copysize);
		csym += copysize;

		if (!updatecur)
			break;

		cur += 1;
		index = strtoll(cur, &cur, 10);
		carglog(carg, L_TRACE, "<<<get index of template '%s': %"u64"\n", cur, index);
		if (index < 1)
			break;
		
		index--; // index decrement because take from null

		index = arr[index];
		carglog(carg, L_TRACE, "<<<templating element '%s': with [%"u64"] element\n", cur, index);

		carglog(carg, L_TRACE, "<<<<<1 dst %s\n", dst);

		oldcur = cur;
		carglog(carg, L_TRACE, "<<<<< metric_split %s\n", metric_split[index]);

		if (!metric_split[index])
			break;

		copysize = strlen(metric_split[index]);
		strlcpy(dst+csym, metric_split[index], copysize+1);
		carglog(carg, L_TRACE, "<<<< 2copy %s with %zu syms to %"u64" ptr\n", metric_split[index], copysize, csym);

		csym += copysize;
		carglog(carg, L_TRACE, "<<<<<%"u64" dst %s\n", index, dst);
	}
	dst[csym] = 0;
	

	return ret;
}

uint8_t parse_statsd_labels(char *str, uint64_t *i, size_t size, alligator_ht **lbl, context_arg *carg)
{
	if ((str[*i] == '#') || (str[*i] == ','))
		++*i;

	char label_name[METRIC_NAME_SIZE];
	char label_key[METRIC_NAME_SIZE];

	int n=MAX_LABEL_COUNT;
	while ((str[*i] != ':') && (str[*i] != '\0') && n--)
	{
		//printf("str[*i] != '%c'\n", str[*i]);
		if ((str[*i] == '#') || (str[*i] == ','))
			++*i;

		//printf("1str+i '%s'\n", str+*i);
		uint64_t label_name_size = multicollector_get_name(i, str, size, label_name);
		//printf("2str+i '%s'\n", str+*i);
		multicollector_skip_spaces(i, str, size);

		++*i;

		//printf("3str+i '%s'\n", str+*i);
		multicollector_get_value(i, str, size, label_key);
		//printf("4str+i '%s'\n", str+*i);
		multicollector_skip_spaces(i, str, size);
		//printf("5str+i '%s'\n", str+*i);

		carglog(carg, L_DEBUG, "label name: %s\n", label_name);
		carglog(carg, L_DEBUG, "label key: %s\n", label_key);

		if (!*lbl)
		{
			*lbl = alligator_ht_init(NULL);
		}

		if (carg && reject_metric(carg->reject, label_name, label_key))
		{
			labels_hash_free(*lbl);
			return 0;
		}

		metric_label_value_validator_normalizer(label_key, strlen(label_key));

		if (metric_name_validator_promstatsd(label_name, label_name_size))
		{
			metric_name_normalizer(label_name, label_name_size);
			labels_hash_insert_nocache(*lbl, label_name, label_key);
		}
	}

	return 1;
}

uint8_t multicollector_field_get(char *str, size_t size, alligator_ht *lbl, context_arg *carg, alligator_ht *counter_names)
{
	carg_or_glog(carg, L_DEBUG, "multicollector: parse metric string '%s'\n", str);
	r_time start_parsing = setrtime();
	double value = 0;
	uint64_t i = 0;
	int is_prom = 0;
	int rc;
	int8_t increment = 0;
	char template_name[METRIC_NAME_SIZE];
	char metric_name[METRIC_NAME_SIZE];
	multicollector_skip_spaces(&i, str, size);
	size_t metric_len = multicollector_get_metric_name(&i, str, size, template_name);
	//printf("multicollector from '%s' to '%s'\n", str, template_name);

	// pass mapping
	mapping_metric *mm = NULL;
	strlcpy(metric_name, template_name, metric_len+1);
	//printf("copy metric name %s from '%s' with size %zu\n", metric_name, template_name, metric_len);
	if (!metric_name_validator_promstatsd(metric_name, metric_len))
	{
		labels_hash_free(lbl);
		carg_or_glog(carg, L_DEBUG, "> metric name validator failed\n");
		return 0;
	}

	if (carg)
		mm = carg->mm;

	for (; mm; mm = mm->next)
	{
		uint64_t input_name_size = strcspn(str, ": \t\n");
		size_t metric_split_size = 0;
		char *matchres = strndup(str, input_name_size);
		uint64_t arr[255];
		char **metric_split = mapping_match(mm, matchres, input_name_size, &metric_split_size, arr);
		if (metric_split)
		{
			if (mm->metric_name)
			{
				metric_len = mapping_template(carg, metric_name, mm->metric_name, METRIC_NAME_SIZE, metric_split, arr);
				mapping_label *ml = mm->label_head;
				while (ml)
				{
					// graphite/statsd labels parse

					// init labels
					if (!lbl)
					{
						lbl = alligator_ht_init(NULL);
					}

					// init vars
					char label_name[METRIC_NAME_SIZE];
					char label_key[METRIC_NAME_SIZE];
					mm->label_tail = ml;

					// template exec
					mapping_template(carg, label_name, ml->name, METRIC_NAME_SIZE, metric_split, arr);
					//printf("from template '%s' rendered: '%s'\n", ml->name, label_name);
					mapping_template(carg, label_key, ml->key, METRIC_NAME_SIZE, metric_split, arr);

					// insert
					labels_hash_insert_nocache(lbl, label_name, label_key);

					ml = ml->next;
				}
			}
			free_mapping_split_free(metric_split, metric_split_size);
			free(matchres);
			break;
		}
		free(matchres);
	}

	carg_or_glog(carg, L_DEBUG, "> metric name = %s\n", metric_name);
	if (i == size)
	{
		carg_or_glog(carg, L_DEBUG, "%s: increment\n", metric_name);
		labels_hash_free(lbl);
		return 0;
	}

	multicollector_skip_spaces(&i, str, size);

	if (str[i] == '{')
	{
		if (!lbl)
		{
			lbl = alligator_ht_init(NULL);
		}
		is_prom = 1;

		// prometheus with labels
		char label_name[METRIC_NAME_SIZE];
		char label_key[METRIC_NAME_SIZE];
		++i;
		while (i<size)
		{
			//printf("metric name %s, i %d, size %d: %s\n", metric_name, i, size, str);
			// get label name
			multicollector_skip_spaces(&i, str, size);
			uint64_t label_name_size =  multicollector_get_name(&i, str, size, label_name);
			multicollector_skip_spaces(&i, str, size);
			if (str[i] != '=')
			{
				labels_hash_free(lbl);
				return 0;
			}
			else
			{
				++i;
				//printf("1i=%"u64"\n", i);
			}

			// get label key
			multicollector_skip_spaces(&i, str, size);
			rc = multicollector_get_quotes(&i, str, size, label_key);
			//printf("trying for rc = 0: %d\n", rc);
			if (rc == 0)
			{
			//	labels_hash_free(lbl);
			//	carglog(carg, L_DEBUG, "metric '%s' has invalid label format\n", str);

			//	return 0;
			}

			carg_or_glog(carg, L_DEBUG, "> label_name = %s\n", label_name);
			carg_or_glog(carg, L_DEBUG, "> label_key = %s\n", label_key);

			if (metric_name_validator(label_name, label_name_size))
			{
				if (carg && reject_metric(carg->reject, label_name, label_key))
				{
					labels_hash_free(lbl);
					return 0;
				}
				metric_label_value_validator_normalizer(label_key, strlen(label_key));

				prometheus_metric_name_normalizer(label_name, label_name_size);
				labels_hash_insert_nocache(lbl, label_name, label_key);
			}
			else
			{
				labels_hash_free(lbl);
				carg_or_glog(carg, L_DEBUG, "metric '%s' has invalid label format: %s\n", str, label_name);
				return 0;
			}
			// go to next label or end '}'
			multicollector_skip_spaces(&i, str, size);
			if (str[i] == ',')
			{
				++i;
				multicollector_skip_spaces(&i, str, size);
				if (str[i] == '}')
					break;
				//printf("2i=%"u64"\n", i);
			}
			else if (str[i] == '}')
				break;
			else
			{
				labels_hash_free(lbl);
				return 0;
			}
		}

		i += strcspn(str+i, " \t");
		i += strspn(str+i, " \t");

		carg_or_glog(carg, L_DEBUG, "> prometheus labels value: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
		value = atof(str+i);
	}
	else if (str[i] == ':' || str[i] == '#' || str[i] == ',')
	{
		// don't need free labels
		if (!parse_statsd_labels(str, &i, size, &lbl, carg))
			return 0;
		// statsd
		++i;
		//printf("3i=%"u64"\n", i);
		if ((str[i] == '+') || (str[i] == '-'))
		{
			increment = 1;
			value = 1;
		}
		else
		{
			carg_or_glog(carg, L_DEBUG, "> statsd value: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
			value = atof(str+i);
			if (strstr(str+i, "|c"))
			{
				increment = 1;
				carg_or_glog(carg, L_DEBUG, "> statsd value increment: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
			}
		}

		i += strcspn(str+i, "#");
		//printf("> last parse: %s)\n", str+i);

		// don't need free labels
		if (!parse_statsd_labels(str, &i, size, &lbl, carg))
			return 0;
	}
	else if (isdigit(str[i]))
	{
		// prometheus without labels
		carg_or_glog(carg, L_DEBUG, "> prometheus value: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
		value = atof(str+i);
	}
	else
	{
		labels_hash_free(lbl);
		return 0;
	}

	// replacing dot symbols and other from metric
	if (is_prom)
		prometheus_metric_name_normalizer(metric_name, metric_len);
	else
		metric_name_normalizer(metric_name, metric_len);
	//metric_add_ret(metric_name, lbl, &value, DATATYPE_DOUBLE, mm);

	r_time end_parsing = setrtime();

	metric_datatypes *dt = NULL;
	uint8_t data_type = METRIC_TYPE_UNTYPED;
	if (carg && carg->metric_aggregation)
	{
		uint32_t key_hash = tommy_strhash_u32(0, metric_name);
		dt = alligator_ht_search(counter_names, metric_datatypes_compare, metric_name, key_hash);
		if (dt)
			data_type = dt->type;
	}

	if (cluster_pass(carg, metric_name, lbl, &value, DATATYPE_DOUBLE, data_type))
	{
		carg_or_glog(carg, L_TRACE, "metric went to the replication, don't need to make: %s\n", metric_name);
        labels_hash_free(lbl);
		return 1;
	}

	carg_or_glog(carg, L_INFO, "multicollector result: %lu %lf '%s'\n", pthread_self(), value, str);
	if (increment)
		metric_update(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
	else if (data_type == METRIC_TYPE_COUNTER || data_type == METRIC_TYPE_HISTOGRAM)
		metric_update(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
	else
		metric_add(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);

	r_time end_metrictree = setrtime();

	if (carg)
	{
		carg->push_parsing_time += getrtime_ns(start_parsing, end_parsing);
		carg->push_metric_time += getrtime_ns(end_parsing, end_metrictree);
	}

	return 1;
}

void multicollector(http_reply_data* http_data, char *str, size_t size, context_arg *carg)
{
	char tmp[65535];
	int64_t cnt = 0;
	size_t tmp_len;
	if (!str)
	{
		str = http_data->body;
		size = http_data->body_size;
	}

	alligator_ht *counter_names = alligator_ht_init(NULL);
	uint64_t fgets_counter = 0;

	while ( (tmp_len = char_fgets(str, tmp, &cnt, size, carg)) )
	{
		++fgets_counter;

		if (tmp[0] == '#')
		{
			if (carg->metric_aggregation)
			{
				uint16_t cursor = 1;
				cursor += strspn(tmp + cursor, " \t");
				if (!strncmp(tmp + cursor, "TYPE", 4))
				{
					size_t size;
					char metric_name[255];
					cursor += strcspn(tmp + cursor, " \t");
					cursor += strspn(tmp + cursor, " \t");
					size = strcspn(tmp + cursor, " \t");
					if (size > 255)
						continue;

					strlcpy(metric_name, tmp + cursor, size + 1);

					cursor += size;
					cursor += strspn(tmp + cursor, " \t");

					if (!strncmp(tmp + cursor, "counter", 7))
					{
						uint32_t key_hash = tommy_strhash_u32(0, metric_name);
						metric_datatypes *dt = alligator_ht_search(counter_names, metric_datatypes_compare, metric_name, key_hash);
						if (!dt)
						{
							dt = calloc(1, sizeof(*dt));
							dt->key = strdup(metric_name);
							dt->type = METRIC_TYPE_COUNTER;
							alligator_ht_insert(counter_names, &(dt->node), dt, tommy_strhash_u32(0, dt->key));
						}
					}
					else if (!strncmp(tmp + cursor, "histogram", 9))
					{
						uint32_t key_hash = tommy_strhash_u32(0, metric_name);
						metric_datatypes *dt = alligator_ht_search(counter_names, metric_datatypes_compare, metric_name, key_hash);
						if (!dt)
						{
							dt = calloc(1, sizeof(*dt));
							dt->key = strdup(metric_name);
							dt->type = METRIC_TYPE_HISTOGRAM;
							alligator_ht_insert(counter_names, &(dt->node), dt, tommy_strhash_u32(0, dt->key));
						}
					}
				}
			}
			continue;
		}

		if (tmp[0] == 0)
			continue;

		alligator_ht *lbl = NULL;
		if (http_data)
			lbl = get_labels_from_url_pushgateway_format(http_data->uri, http_data->uri_size, carg);

		if (lbl && (uint64_t)lbl == 1)
			continue;

		// carg is null if directory persistence read
		if (carg && http_data && http_data->expire != -1)
			carg->curr_ttl = http_data->expire;

		uint8_t rc = multicollector_field_get(tmp, tmp_len, lbl, carg, counter_names);
		if (carg) {
			carg->push_accepted_lines += rc;
			carg->parser_status = rc;
		}
	}

	if (carg)
		carglog(carg, L_INFO, "parsed metrics multicollector: %"u64", accepted %"u64", full size read: %zu; timers: parsing %lf, metric %lf, string-split %lf\n", fgets_counter, carg->push_accepted_lines, size, carg->push_parsing_time / 1000000000.0, carg->push_metric_time / 1000000000.0, carg->push_split_data / 1000000000.0);

	if (carg && !carg->no_metric)
	{
		metric_add_labels("alligator_push_parsing_time_ns", &carg->push_parsing_time, DATATYPE_UINT, carg, "key", carg->key);
		metric_add_labels("alligator_push_metrictree_time_ns", &carg->push_metric_time, DATATYPE_UINT, carg, "key", carg->key);
		metric_add_labels("alligator_push_split_time_ns", &carg->push_split_data, DATATYPE_UINT, carg, "key", carg->key);
		metric_add_labels("alligator_push_parsed_lines", &fgets_counter, DATATYPE_UINT, carg, "key", carg->key);
		metric_add_labels("alligator_push_accepted_lines", &carg->push_accepted_lines, DATATYPE_UINT, carg, "key", carg->key);
	}

	alligator_ht_foreach(counter_names, metric_datatype_foreach);
	alligator_ht_done(counter_names);
	free(counter_names);
}

string* prometheus_metrics_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, "1.1", env, proxy_settings, NULL));
	else
		return NULL;
}

void prometheus_metrics_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("prometheus_metrics");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = prometheus_metrics_mesg;
	strlcpy(actx->handler[0].key,"prometheus_metrics", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
