#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "parsers/pushgateway.h"
#include "mapping/mapping.h"
#include "parsers/http_proto.h"
#include "events/context_arg.h"
#include "common/reject.h"
#include "mapping/type.h"
#include "common/http.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "cluster/pass.h"
#include "parsers/metric_types.h"
#include "common/logs.h"
#include "main.h"
#define METRIC_NAME_SIZE 255
#define MAX_LABEL_COUNT 10

static uint8_t multicollector_is_http_header_line(const char *line)
{
	if (!line)
		return 0;

	const char *p = line + strspn(line, " \t");
	size_t key_len = strcspn(p, ":");
	if (!key_len || p[key_len] != ':')
		return 0;

	/* Header keys are token-like (letters/digits/dash). */
	for (size_t i = 0; i < key_len; ++i)
	{
		char c = p[i];
		if (!((c >= 'a' && c <= 'z') ||
		      (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') ||
		       c == '-'))
			return 0;
	}

	const char *value = p + key_len + 1;
	value += strspn(value, " \t");
	if (!*value)
		return 1;

	/* StatsD-style lines usually contain type delimiter ("|"). */
	if (strchr(value, '|'))
		return 0;

	/* Prom/StatsD values are numeric at this position. */
	if ((*value >= '0' && *value <= '9') || *value == '+' || *value == '-' || *value == '.')
		return 0;

	return 1;
}

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
	if (!str || !s2 || *cur >= size) {
		if (s2)
			*s2 = 0;
		return 0;
	}
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

	size_t copy_size = syms < (METRIC_NAME_SIZE - 1) ? syms : (METRIC_NAME_SIZE - 1);
	strlcpy(s2, str+*cur, copy_size + 1);
	*cur = *cur + syms;

	return syms;
}

// label name
uint64_t multicollector_get_name(uint64_t *cur, const char *str, const size_t size, char *s2)
{
	if (!str || !s2 || *cur >= size) {
		if (s2)
			*s2 = 0;
		return 0;
	}
	uint64_t syms = strcspn(str+*cur, "\t\r:={},# ");
	size_t copy_size = syms < (METRIC_NAME_SIZE - 1) ? syms : (METRIC_NAME_SIZE - 1);
	strlcpy(s2, str+*cur, copy_size + 1);
	*cur = *cur + syms;

	return syms;
}

// label value
int multicollector_get_value(uint64_t *cur, const char *str, const size_t size, char *s2)
{
	if (!str || !s2 || *cur >= size) {
		if (s2)
			*s2 = 0;
		return 0;
	}
	uint64_t syms = strcspn(str+*cur, "\t\r{},# ");
	size_t copy_size = syms < (METRIC_NAME_SIZE - 1) ? syms : (METRIC_NAME_SIZE - 1);
	strlcpy(s2, str+*cur, copy_size + 1);
	*cur = *cur + syms;

	return syms;
}

int multicollector_get_quotes(uint64_t *cur, const char *str, const size_t size, char *s2)
{
	if (s2)
		*s2 = 0;
	if (!str || !s2 || *cur >= size)
		return 0;
	if (str[*cur] != '"')
		return 0;

	++*cur;
	uint64_t syms = strcspn(str+*cur, "\"");
	if ((*cur + syms) >= size || str[*cur + syms] != '"')
		return 0;
	size_t copy_size = syms < (METRIC_NAME_SIZE - 1) ? syms : (METRIC_NAME_SIZE - 1);
	strlcpy(s2, str+*cur, copy_size + 1);
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

int char_fgets(char *str, char *buf, size_t bufsize, int64_t *cnt, size_t len, context_arg *carg)
{
	if (!buf || bufsize == 0)
		return 0;
	if (*cnt < 0 || *cnt >= (int64_t)len)
	{
		buf[0] = '\0';
		return 0;
	}

	/* Bounded scan: str is not necessarily NUL-terminated within len. */
	int64_t line = 0;
	while (*cnt + line < (int64_t)len && str[*cnt + line] != '\n')
		line++;

	size_t copy = (size_t)line;
	if (copy >= bufsize)
		copy = bufsize - 1;

	r_time start_split_data = setrtime();

	memcpy(buf, str + *cnt, copy);
	buf[copy] = '\0';

	if (carg)
	{
		r_time end_split_data = setrtime();
		carg->push_split_data += getrtime_ns(start_split_data, end_split_data);
	}

	int has_nl = (*cnt + line < (int64_t)len && str[*cnt + line] == '\n');
	int64_t adv = line + (has_nl ? 1 : 0);
	*cnt += adv;
	return adv > 0 ? (int)adv : 0;
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

uint8_t parse_statsd_labels(char *str, uint64_t *i, size_t size, alligator_ht **lbl, context_arg *carg)
{
	if (!str || !i || *i >= size)
		return 0;
	if ((str[*i] == '#') || (str[*i] == ','))
		++*i;

	char label_name[METRIC_NAME_SIZE];
	char label_key[METRIC_NAME_SIZE];

	int n=MAX_LABEL_COUNT;
	while (*i < size && (str[*i] != ':') && (str[*i] != '\0') && n--)
	{
		//printf("str[*i] != '%c'\n", str[*i]);
		if (*i < size && ((str[*i] == '#') || (str[*i] == ',')))
			++*i;

		//printf("1str+i '%s'\n", str+*i);
		uint64_t label_name_size = multicollector_get_name(i, str, size, label_name);
		//printf("2str+i '%s'\n", str+*i);
		multicollector_skip_spaces(i, str, size);
		if (*i >= size)
			break;

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
		if (!mm)
			break;

		if (mm->metric_name)
		{
			char *fields[MAPPING_MAX_EXTRACT_FIELDS];
			int num_fields;

			if (!match_and_extract(mm->template, metric_name, fields, &num_fields))
			{
				carg_or_glog(carg, L_TRACE, "> mapping not matched: template %s, metric_name is %s, skip\n", mm->template, metric_name);
				continue;
			}

			carg_or_glog(carg, L_DEBUG, "> mapping matched: mapping %p template %s, metric name is '%s'\n", mm, mm->template, metric_name);
			template_render(mm->metric_name, fields, num_fields, metric_name, METRIC_NAME_SIZE);


			mapping_label *ml = mm->label_head;
			carg_or_glog(carg, L_DEBUG, "> mapping matched: mapping %p template %s, metric name updated to '%s'\n", mm, mm->template, metric_name);

			while (ml)
			{
				if (!lbl)
					lbl = alligator_ht_init(NULL);

				char label_name[METRIC_NAME_SIZE];
				char label_key[METRIC_NAME_SIZE];

				template_render(ml->name, fields, num_fields, label_name, METRIC_NAME_SIZE);
				template_render(ml->key, fields, num_fields, label_key, METRIC_NAME_SIZE);

				carg_or_glog(carg, L_DEBUG, "\t> mapping %p for template '%s' generated label '%s'='%s'\n", mm, mm->template, label_name, label_key);
				labels_hash_insert_nocache(lbl, label_name, label_key);
				ml = ml->next;
			}
		}

	}

	carg_or_glog(carg, L_DEBUG, "> metric name = %s\n", metric_name);
	if (i == size)
	{
		carg_or_glog(carg, L_DEBUG, "%s: increment\n", metric_name);
		labels_hash_free(lbl);
		return 0;
	}

	multicollector_skip_spaces(&i, str, size);
	if (i >= size)
	{
		labels_hash_free(lbl);
		return 0;
	}

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
			if (i >= size) {
				labels_hash_free(lbl);
				return 0;
			}
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
			if (i >= size)
			{
				labels_hash_free(lbl);
				return 0;
			}
			if (str[i] == ',')
			{
				++i;
				multicollector_skip_spaces(&i, str, size);
				if (i >= size)
				{
					labels_hash_free(lbl);
					return 0;
				}
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
		if (i >= size)
		{
			labels_hash_free(lbl);
			return 0;
		}
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
	else if (isdigit((unsigned char)str[i]))
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
	uint8_t skip_http_headers = 0;

	while ( (tmp_len = char_fgets(str, tmp, sizeof(tmp), &cnt, size, carg)) )
	{
		++fgets_counter;

		/* Generic HTTP preamble filter for handler paths where http_data is NULL. */
		if (!skip_http_headers)
		{
			char *line = tmp + strspn(tmp, " \t");
			if (!strncasecmp(line, "HTTP/", 5))
			{
				carglog(carg, L_DEBUG, "detected HTTP response preamble, skip headers\n");
				skip_http_headers = 1;
				continue;
			}
		}

		if (skip_http_headers)
		{
			char *line = tmp + strspn(tmp, " \t\r");
			if (!*line)
			{
				skip_http_headers = 0;
				carglog(carg, L_DEBUG, "HTTP header block skipped, continue with body\n");
			}
			continue;
		}

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
						char bucket_name[1024];
						snprintf(bucket_name, 1023, "%s_bucket", metric_name);
						uint32_t key_hash = tommy_strhash_u32(0, bucket_name);
						metric_datatypes *dt = alligator_ht_search(counter_names, metric_datatypes_compare, bucket_name, key_hash);
						if (!dt)
						{
							dt = calloc(1, sizeof(*dt));
							dt->key = strdup(bucket_name);
							dt->type = METRIC_TYPE_HISTOGRAM;
							alligator_ht_insert(counter_names, &(dt->node), dt, tommy_strhash_u32(0, dt->key));
						}

						char sum_name[1024];
						snprintf(sum_name, 1023, "%s_sum", metric_name);
						key_hash = tommy_strhash_u32(0, sum_name);
						dt = alligator_ht_search(counter_names, metric_datatypes_compare, sum_name, key_hash);
						if (!dt)
						{
							dt = calloc(1, sizeof(*dt));
							dt->key = strdup(sum_name);
							dt->type = METRIC_TYPE_HISTOGRAM;
							alligator_ht_insert(counter_names, &(dt->node), dt, tommy_strhash_u32(0, dt->key));
						}

						char count_name[1024];
						snprintf(count_name, 1023, "%s_count", metric_name);
						key_hash = tommy_strhash_u32(0, count_name);
						dt = alligator_ht_search(counter_names, metric_datatypes_compare, count_name, key_hash);
						if (!dt)
						{
							dt = calloc(1, sizeof(*dt));
							dt->key = strdup(count_name);
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


		if (http_data && multicollector_is_http_header_line(tmp))
		{
			carglog(carg, L_DEBUG, "skip header-like line in metrics payload: '%s'\n", tmp);
			continue;
		}

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

	if (counter_names) {
		alligator_ht_foreach(counter_names, metric_datatype_foreach);
		alligator_ht_done(counter_names);
		free(counter_names);
	}
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
