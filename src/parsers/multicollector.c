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
#include "main.h"
#define METRIC_NAME_SIZE 255
#define MAX_LABEL_COUNT 10

int multicollector_get_name(uint64_t *cur, const char *str, const size_t size, char *s2)
{
	uint64_t syms = strcspn(str+*cur, "\t\r:={},# ");
	strlcpy(s2, str+*cur, syms + 1);
	*cur = *cur + syms;

	return syms;
}

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
	strlcpy(s2, str+*cur, syms+1);
	*cur = *cur + syms + 1;

	return syms;
}

int multicollector_skip_spaces(uint64_t *cur, const char *str, const size_t size)
{

	uint64_t syms = strspn(str+*cur, "\t\r ");
	*cur = *cur + syms;

	return syms;
}

int char_fgets(char *str, char *buf, int64_t *cnt, size_t len)
{
	if (*cnt >= len)
	{
		strncpy(buf, "\0", 1);
		return 0;
	}
	int64_t i = strcspn(str+(*cnt), "\n");
	strlcpy(buf, str+(*cnt), i+1);
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

char** mapping_str_split(char *str, size_t len, size_t *out_len, char **template_split, size_t template_split_size)
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


	char **ret = malloc((globs+1)*sizeof(void*));

	for (k=0; k<globs; ++k)
	{
		glob_size = strcspn(str+offset, ".");
		char *tmpret = strndup(str+offset, glob_size);
		
		//ret[k] = strndup(str+offset, glob_size);
		if (template_split)
		{
			if (template_split[k][0] != '*')
			{
				ret[d++] = tmpret;
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

char** mapping_match(mapping_metric *mm, char *str, size_t size, size_t *split_size)
{
	char **split = 0;
	if (mm->match == MAPPING_MATCH_GLOB)
	{
		uint8_t i;
		size_t str_splits;

		char **template_split = mapping_str_split(mm->template, size, &str_splits, NULL, 0);
		split = mapping_str_split(str, size, &str_splits, template_split, str_splits);
		if (!split)
			return 0;
		//printf("%d <> %d\n", str_splits, mm->glob_size);
		//if (str_splits != mm->glob_size)
		//{
		//	free_mapping_split_free(split, str_splits);
		//	puts("return 1");
		//	return 0;
		//}

		//for (i=0; i<mm->glob_size; ++i)
		for (i=0; i<str_splits; ++i)
		{
			int8_t selector = aglob(split[i], mm->glob[i]);
			if (!selector)
			{
				free_mapping_split_free(split, str_splits);
				return 0;
			}
		}
		*split_size = str_splits;
	}
	else if (mm->match == MAPPING_MATCH_PCRE)
	{
	}

	return split;
}

size_t mapping_template(char *dst, char *src, size_t size, char **metric_split)
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
		if (ac->log_level > 4)
			printf("<<<<< found (non template data) \"$ '%s' (%p)\n", cur, cur);

		size_t copysize = cur - oldcur;
		if (ac->log_level > 4)
			printf("<<<< copy non template %s with %zu syms to %"u64" ptr\n", oldcur, copysize, csym);
		strncpy(dst+csym, oldcur, copysize);
		csym += copysize;

		if (!updatecur)
			break;

		cur += 1;
		index = strtoll(cur, &cur, 10);
		if (ac->log_level > 4)
			printf("<<<get index of template '%s': %"u64"\n", cur, index);
		if (index < 1)
			break;
		
		index--; // index decrement because take from null

		if (ac->log_level > 4)
			printf("<<<templating element '%s': with [%"u64"] element\n", cur, index);

		if (ac->log_level > 4)
			printf("<<<<<1 dst %s\n", dst);

		oldcur = cur;
		if (ac->log_level > 4)
			printf("<<<<< metric_split %s\n", metric_split[index]);

		if (!metric_split[index])
			break;
		copysize = strlen(metric_split[index]);
		strncpy(dst+csym, metric_split[index], copysize);
		if (ac->log_level > 4)
			printf("<<<< 2copy %s with %zu syms to %"u64" ptr\n", metric_split[index], copysize, csym);

		csym += copysize;
		if (ac->log_level > 4)
			printf("<<<<<%"u64" dst %s\n", index, dst);
	}
	dst[csym] = 0;
	

	return ret;
}

void parse_statsd_labels(char *str, uint64_t *i, size_t size, tommy_hashdyn **lbl, context_arg *carg)
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
		multicollector_get_name(i, str, size, label_name);
		//printf("2str+i '%s'\n", str+*i);
		multicollector_skip_spaces(i, str, size);

		++*i;

		//printf("3str+i '%s'\n", str+*i);
		multicollector_get_value(i, str, size, label_key);
		//printf("4str+i '%s'\n", str+*i);
		multicollector_skip_spaces(i, str, size);
		//printf("5str+i '%s'\n", str+*i);

		if (ac->log_level > 3)
		{
			printf("label name: %s\n", label_name);
			printf("label key: %s\n", label_key);
		}

		if (!*lbl)
		{
			*lbl = malloc(sizeof(tommy_hashdyn));
			tommy_hashdyn_init(*lbl);
		}

		if (carg && reject_metric(carg->reject, label_name, label_key))
		{
			labels_hash_free(lbl);
			return;
		}

		if (metric_name_validator_promstatsd(label_name, strlen(label_name)))
			labels_hash_insert_nocache(*lbl, label_name, label_key);
	}
}

void multicollector_field_get(char *str, size_t size, tommy_hashdyn *lbl, context_arg *carg)
{
	if (ac->log_level > 3)
		fprintf(stdout, "multicollector: parse metric string '%s'\n", str);
	double value = 0;
	uint64_t i = 0;
	int rc;
	int8_t increment = 0;
	char template_name[METRIC_NAME_SIZE];
	char metric_name[METRIC_NAME_SIZE];
	multicollector_skip_spaces(&i, str, size);
	size_t metric_len = multicollector_get_name(&i, str, size, template_name);
	//printf("multicpllector from '%s' to '%s'\n", str, template_name);

	// pass mapping
	mapping_metric *mm = NULL;
	strlcpy(metric_name, template_name, metric_len+1);
	//printf("copy metric name %s from '%s' with size %zu\n", metric_name, template_name, metric_len);
	if (!metric_name_validator_promstatsd(metric_name, metric_len))
		return;

	if (carg)
		mm = carg->mm;

	for (; mm; mm = mm->next)
	{
		uint64_t input_name_size = strcspn(str, ": \t\n");
		size_t metric_split_size = 0;
		char *matchres = strndup(str, input_name_size);
		char **metric_split = mapping_match(mm, matchres, input_name_size, &metric_split_size);
		if (metric_split)
		{
			if (mm->metric_name)
			{
				metric_len = mapping_template(metric_name, mm->metric_name, METRIC_NAME_SIZE, metric_split);
				mapping_label *ml = mm->label_head;
				while (ml)
				{
					// graphite/statsd labels parse

					// init labels
					if (!lbl)
					{
						lbl = malloc(sizeof(*lbl));
						tommy_hashdyn_init(lbl);
					}

					// init vars
					char label_name[METRIC_NAME_SIZE];
					char label_key[METRIC_NAME_SIZE];
					mm->label_tail = ml;

					// template exec
					mapping_template(label_name, ml->name, METRIC_NAME_SIZE, metric_split);
					//printf("from template '%s' rendered: '%s'\n", ml->name, label_name);
					mapping_template(label_key, ml->key, METRIC_NAME_SIZE, metric_split);

					// insert
					labels_hash_insert_nocache(lbl, label_name, label_key);

					ml = ml->next;
				}
			}
			free_mapping_split_free(metric_split, metric_split_size);
			break;
		}
	}

	if (ac->log_level > 3)
		fprintf(stdout, "> metric name = %s\n", metric_name);
	if (i == size)
	{
		if (ac->log_level > 3)
			printf("%s: increment\n", metric_name);
		//metric_increment();
		return;
	}

	multicollector_skip_spaces(&i, str, size);

	if (str[i] == '{')
	{
		if (!lbl)
		{
			lbl = malloc(sizeof(*lbl));
			tommy_hashdyn_init(lbl);
		}

		// prometheus with labels
		char label_name[METRIC_NAME_SIZE];
		char label_key[METRIC_NAME_SIZE];
		++i;
		while (i<size)
		{
			// get label name
			multicollector_skip_spaces(&i, str, size);
			multicollector_get_name(&i, str, size, label_name);
			multicollector_skip_spaces(&i, str, size);
			if (str[i] != '=')
			{
				labels_hash_free(lbl);
				return;
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
			//	if (ac->log_level > 3)
			//		fprintf(stdout, "metric '%s' has invalid label format\n", str);

			//	return;
			}

			if (ac->log_level > 3)
			{
				fprintf(stdout, "> label_name = %s\n", label_name);
				fprintf(stdout, "> label_key = %s\n", label_key);
			}

			if (metric_name_validator(label_name, strlen(label_name)))
			{
				if (carg && reject_metric(carg->reject, label_name, label_key))
				{
					labels_hash_free(lbl);
					return;
				}

				labels_hash_insert_nocache(lbl, label_name, label_key);
			}
			else
			{
				labels_hash_free(lbl);
				if (ac->log_level > 3)
					fprintf(stdout, "metric '%s' has invalid label format: %s\n", str, label_name);
				return;
			}
			// go to next label or end '}'
			multicollector_skip_spaces(&i, str, size);
			if (str[i] == ',')
			{
				++i;
				//printf("2i=%"u64"\n", i);
			}
			else if (str[i] == '}')
				break;
			else
			{
				labels_hash_free(lbl);
				return;
			}
		}

		i += strcspn(str+i, " \t");
		i += strspn(str+i, " \t");

		if (ac->log_level > 3)
			printf("> prometheus labels value: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
		value = atof(str+i);
	}
	else if (str[i] == ':' || str[i] == '#' || str[i] == ',')
	{
		parse_statsd_labels(str, &i, size, &lbl, carg);
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
			if (ac->log_level > 3)
				printf("> statsd value: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
			value = atof(str+i);
			if (strstr(str+i, "|c"))
			{
				increment = 1;
				if (ac->log_level > 3)
					printf("> statsd value increment: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
			}
		}

		i += strcspn(str+i, "#");
		//printf("> last parse: %s)\n", str+i);
		parse_statsd_labels(str, &i, size, &lbl, carg);
	}
	else if (isdigit(str[i]))
	{
		// prometheus without labels
		if (ac->log_level > 3)
			printf("> prometheus value: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
		value = atof(str+i);
	}
	else
		return;

	// replacing dot symbols and other from metric
	metric_name_normalizer(metric_name, metric_len);
	//metric_add_ret(metric_name, lbl, &value, DATATYPE_DOUBLE, mm);

	if (increment)
		metric_update(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
	else
		metric_add(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
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

	while ( (tmp_len = char_fgets(str, tmp, &cnt, size)) )
	{
		if (tmp[0] == '#')
			continue;

		if (tmp[0] == 0)
			continue;

		tommy_hashdyn *lbl = NULL;
		if (http_data)
			lbl = get_labels_from_url_pushgateway_format(http_data->uri, http_data->uri_size, carg);

		if (lbl && (uint64_t)lbl == 1)
			continue;

		if (carg && http_data)
			carg->curr_ttl = http_data->expire;
		multicollector_field_get(tmp, tmp_len, lbl, carg);
	}
}

string* prometheus_metrics_mesg(host_aggregator_info *hi, void *arg)
{
	return string_init_add(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1), 0, 0);
}

void prometheus_metrics_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("prometheus_metrics");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = prometheus_metrics_mesg;
	strlcpy(actx->handler[0].key,"prometheus_metrics", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
