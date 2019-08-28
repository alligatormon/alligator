#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "parsers/pushgateway.h"
#include "parsers/http_proto.h"
#define METRIC_NAME_SIZE 255

int multicollector_get_name(uint64_t *cur, const char *str, const size_t size, char *s2)
{
	uint64_t syms = strcspn(str+*cur, "\t\r:={} ");
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
	free(split);
}

char** mapping_str_split(char *str, size_t len, size_t *out_len)
{
	uint64_t k;
	size_t globs = 1;
	uint64_t offset = 0;
	size_t glob_size;

	for (k=0; k<len; ++k)
	{
		if (str[k] == '.')
			++globs;
	}
	*out_len = globs;

	if (!globs)
		return NULL;

	char **ret = malloc((globs+1)*sizeof(void*));

	for (k=0; k<globs; ++k)
	{
		glob_size = strcspn(str+offset, ".");
		ret[k] = strndup(str+offset, glob_size);
		offset += glob_size+1;
	}
	ret[k] = NULL;

	return ret;
}

char** mapping_match(mapping_metric *mm, char *str, size_t size)
{
	char **split = 0;
	//printf("mm->match = %"u64"\n", mm->match);
	if (mm->match == MAPPING_MATCH_GLOB)
	{
		uint8_t i;
		size_t str_splits;

		split = mapping_str_split(str, size, &str_splits);
		if (str_splits != mm->glob_size)
		{
			free_mapping_split_free(split, str_splits);
			return 0;
		}

		for (i=0; i<mm->glob_size; ++i)
		{
			int8_t selector = aglob(split[i], mm->glob[i]);
			if (!selector)
			{
				free_mapping_split_free(split, str_splits);
				return 0;
			}
		}
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
		updatecur = strstr(cur, "\"$");
		if (updatecur)
			cur = updatecur;
		else
			cur += strlen(cur);
		//printf("<<<<< find \"$ '%s' (%p)\n", cur, cur);

		size_t copysize = cur - oldcur;
		//printf("<<<< copy %s with %zu syms to %"u64" ptr\n", oldcur, copysize, csym);
		strncpy(dst+csym, oldcur, copysize);
		csym += copysize;

		//puts("<<<check updatecur");
		if (!updatecur)
			break;

		cur += 2;
		index = strtoll(cur, &cur, 10);
		//printf("<<<check index from '%s': %"u64"\n", cur, index);
		if (index < 1)
			break;
		index--;
		++cur;
		//printf("<<<checked index from '%s': %"u64"\n", cur, index);

		//printf("<<<<<1 dst %s\n", dst);
		oldcur = cur;
		//printf("<<<<< metric_split %s\n", metric_split[index]);

		copysize = strlen(metric_split[index]);
		strncpy(dst+csym, metric_split[index], copysize);
		//printf("<<<< copy %s with %zu syms to %"u64" ptr\n", metric_split[index], copysize, csym);
		csym += copysize;
		//printf("<<<<<%"u64" dst %s\n", index, dst);
	}
	//printf("null pointer to %"u64"\n", csym);
	dst[csym] = 0;
	

	return ret;
}

void multicollector_field_get(char *str, size_t size, tommy_hashdyn *lbl, context_arg *carg)
{
	extern aconf *ac;
	if (ac->log_level > 9)
		fprintf(stdout, "multicollector: parse metric string '%s'\n", str);
	double value = 0;
	uint64_t i = 0;
	int rc;
	char template_name[METRIC_NAME_SIZE];
	char metric_name[METRIC_NAME_SIZE];
	multicollector_skip_spaces(&i, str, size);
	size_t metric_len = multicollector_get_name(&i, str, size, template_name);

	// pass mapping
	mapping_metric *mm = NULL;
	strlcpy(metric_name, template_name, metric_len+1);

	if (carg)
		mm = carg->mm;

	for (; mm; mm = mm->next)
	{
		char **metric_split = mapping_match(mm, metric_name, metric_len);
		//printf("Match??? %p %s\n", metric_split, mm->metric_name);
		if (metric_split)
		{
			//puts("match!!!");
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
			int64_t ms_i;
			for (ms_i = 0; metric_split[ms_i]; ms_i++)
				free(metric_split[ms_i]);
			free(metric_split);
			break;
		}
	}

	if (ac->log_level > 9)
		fprintf(stdout, "> metric name = %s\n", metric_name);
	if (i == size)
	{
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
				labels_hash_free(lbl);
				if (ac->log_level > 2)
					fprintf(stdout, "metric '%s' has invalid label format\n", str);

				return;
			}

			if (ac->log_level > 9)
			{
				fprintf(stdout, "> label_name = %s\n", label_name);
				fprintf(stdout, "> label_key = %s\n", label_key);
			}
			labels_hash_insert_nocache(lbl, label_name, label_key);
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

		if (ac->log_level > 9)
			printf("> prometheus labels value: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
		value = atof(str+i);
	}
	else if (str[i] == ':')
	{
		// statsd
		++i;
		//printf("3i=%"u64"\n", i);
		if ((str[i] == '+') || (str[i] == '-'))
			printf("metric increment\n");
		else
		{
			if (ac->log_level > 9)
				printf("> statsd value: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
			value = atof(str+i);
		}
	}
	else if (isdigit(str[i]))
	{
		// prometheus without labels
		if (ac->log_level > 9)
			printf("> prometheus value: %lf (sym: %"u64"/%s)\n", atof(str+i), i, str+i);
		value = atof(str+i);
	}
	
	// replacing dot symbols and other from metric
	metric_name_normalizer(metric_name, metric_len);
	metric_add_ret(metric_name, lbl, &value, DATATYPE_DOUBLE, 0, mm);
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
			lbl = get_labels_from_url_pushgateway_format(http_data->uri, http_data->uri_size);
		multicollector_field_get(tmp, tmp_len, lbl, carg);
	}
}
