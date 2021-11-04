#include "query/promql.h"
#include "common/selector.h"
#include "common/http.h"

metric_query_context *query_context_new(char *name)
{
	metric_query_context *mqc = calloc(1, sizeof(*mqc));
	mqc->name = name;
	//mqc->lbl = calloc(1, sizeof(alligator_ht));
	//alligator_ht_init(mqc->lbl);

	return mqc;
}

metric_query_context *promql_parser(alligator_ht* lbl, char *query, size_t size)
{
	metric_query_context *mqc = query_context_new(NULL);
	char *name = mqc->name = malloc(255);
	string *groupkey = mqc->groupkey = string_new();
	//printf("==== parse %s\n", query);
	if (!size)
		return mqc;

	if (!query)
		return mqc;

	if (!lbl)
	{
		lbl = alligator_ht_init(NULL);
	}

	mqc->func = QUERY_FUNC_COUNT;

	uint64_t i;
	uint64_t cur;
	uint64_t funcur;
	char key[255];
	char value[255];
	uint64_t new;
	char *str = query;
	str += strspn(str, " \t\r\n");
	//printf("2str %p\n", str);
	cur = strcspn(str, "{");
	funcur = strcspn(str, "( \t");

	// find function
	uint64_t jump = 0;
	//printf("cur %"u64" > funcur %"u64"\n", cur, funcur);
	if (cur > funcur)
	{
		if (!strncasecmp(str, "count", 5))
		{
			mqc->func = QUERY_FUNC_COUNT;
			jump = 5;
		}
		else if (!strncasecmp(str, "sum", 3))
		{
			mqc->func = QUERY_FUNC_SUM;
			jump = 3;
		}
		else if (!strncasecmp(str, "absent", 6))
		{
			mqc->func = QUERY_FUNC_ABSENT;
			jump = 6;
		}
		else if (!strncasecmp(str, "present", 7))
		{
			mqc->func = QUERY_FUNC_PRESENT;
			jump = 7;
		}
		else if (!strncasecmp(str, "min", 3))
		{
			mqc->func = QUERY_FUNC_MIN;
			jump = 3;
		}
		else if (!strncasecmp(str, "max", 3))
		{
			mqc->func = QUERY_FUNC_MAX;
			jump = 3;
		}
		else if (!strncasecmp(str, "avg", 3))
		{
			mqc->func = QUERY_FUNC_AVG;
			jump = 3;
		}
	}

	// find metric name
	//printf("jump %"u64"\n", jump);
	str += jump;
	str += strspn(str, " \t\r\n()");

	// skip group by
	if (!strncmp(str, "by", 2))
	{
		i = strcspn(str, "(");
		str += i + 1;
		i = strcspn(str, ")");
		uint64_t k;
		for (uint64_t j = 0; j < i; j++)
		{
			k = strspn(str, ", \t\r\n");
			str += k;
			k += k;
			if (*str == ')')
				break;

			//printf("\t(%d/%d) cur position: '%s'\n", j, i, str);
			k = strcspn(str, ",)");
			string_cat(groupkey, str, k);
			string_cat(groupkey, ",", 1);
			str += k;
			j += k;
		}
		groupkey->s[groupkey->l - 1] = 0;
		//printf("group_key '%s'\n", groupkey->s);

		str += strspn(str, " \t\r\n()");
	}

	//printf("3str %p\n", str);
	cur = strcspn(str, "{) \t");
	for (i = 0; i < cur; i++)
	{
		name[i] = str[i];
	}
	name[i] = 0;
	//printf("name is %s, %d\n", name, *func);

	int check_open = strcspn(str+cur, "{");
	//printf("{ sym: %d(%s) '%c'\n", check_open, str+cur, str[cur+check_open]);
	if (str[cur+check_open] == '{')
	{
		cur += strspn(str+cur, "{ \t");
		size = size - (str - query);
		int size_to_end = strcspn(str+cur, "}") + cur;
		for (; cur <= size_to_end;)
		{
			//printf("parsing (%d/%d) '%s'\n", cur, size_to_end, str+cur);
			new = strcspn(str+cur, " \t=\"");
			if (new)
			{
				strlcpy(key, str+cur, new+1);

				cur += new;
				cur += strspn(str+cur, " \t=\"");

				new = strcspn(str+cur, " \"");
				if (new)
				{
					strlcpy(value, str+cur, new+1);
					cur += new;

					//printf("key: %s, value: %s\n", key, value);
					labels_hash_insert_nocache(lbl, key, value);
				}
			}

			cur++;
			if (cur <= size_to_end)
			{
				cur += strcspn(str+cur, ",");
				cur += strspn(str+cur, ", \t");
			}
		}
	}

	if (!groupkey->l)
	{
		str = strstr(query, "by");
		if (str)
		{
			i = strcspn(str, "(");
			str += i + 1;
			i = strcspn(str, ")");
			uint64_t k;
			for (uint64_t j = 0; j < i; j++)
			{
				k = strspn(str, ", \t\r\n");
				str += k;
				k += k;
				if (*str == ')')
					break;

				k = strcspn(str, ",)");
				string_cat(groupkey, str, k);
				string_cat(groupkey, ",", 1);
				str += k;
				j += k;
			}
			groupkey->s[groupkey->l - 1] = 0;
			//printf("group_key '%s'\n", groupkey->s);

			str += strspn(str, " \t\r\n()");
		}
	}

	// get expression
	str = strstr(query, "}");
	if (!str)
		str = strstr(query, ")");

	if (str)
	{
		str += strcspn(str, "><=");

		if (!strncmp(str, "==", 2))
			mqc->op = QUERY_OPERATOR_EQ;
		else if (!strncmp(str, "!=", 2))
			mqc->op = QUERY_OPERATOR_NE;
		else if (!strncmp(str, ">", 1))
			mqc->op = QUERY_OPERATOR_GT;
		else if (!strncmp(str, "<", 1))
			mqc->op = QUERY_OPERATOR_LT;
		else if (!strncmp(str, ">=", 2))
			mqc->op = QUERY_OPERATOR_GE;
		else if (!strncmp(str, "<=", 2))
			mqc->op = QUERY_OPERATOR_LE;

		str += strspn(str, "><=! \t");
		mqc->opval = strtod(str, NULL);
		//printf("mqc->op == %d, mqc->opval = %lf\n", mqc->op, mqc->opval);
	}

	mqc->query = query;
	mqc->size = size;

	if (lbl)
	{
		free(mqc->lbl);
		mqc->lbl = lbl;
	}

	return mqc;
}

void query_context_set_name(metric_query_context *mqc, char *name)
{
	mqc->name = name;
}

void query_context_set_label(metric_query_context *mqc, char *key, char *value)
{
	labels_hash_insert_nocache(mqc->lbl, key, value);
}

void http_args_to_query_context(void *funcarg, void* arg)
{
	http_arg *harg = arg;
	if (!harg)
		return;

	metric_query_context *mqc = funcarg;

	if (strcmp(harg->key, "query") && strcmp(harg->key, "delimiter"))
	{
		//printf("key '%s', value '%s'\n", harg->key, harg->value);
		query_context_set_label(mqc, harg->key, harg->value);
	}
}

void query_context_convert_http_args_to_query(metric_query_context *mqc, alligator_ht *arg)
{
	alligator_ht_foreach_arg(arg, http_args_to_query_context, mqc);
}

void query_context_free(metric_query_context *mqc)
{
	//if (mqc->lbl)
	//	labels_hash_free(mqc->lbl);
	if (mqc->name)
		free(mqc->name);
	if (mqc->groupkey)
		string_free(mqc->groupkey);
	free(mqc);
}
