#include "query/promql.h"
#include "metric/labels.h"
#include "common/selector.h"
#include "common/http.h"
#include "common/logs.h"
#include <ctype.h>

static void promql_skip_ws(const char **p, const char *end)
{
	while (*p < end && isspace((unsigned char)**p))
		++(*p);
}

static void promql_trim_range(const char **begin, const char **end)
{
	while (*begin < *end && isspace((unsigned char)**begin))
		++(*begin);
	while (*end > *begin && isspace((unsigned char)*((*end) - 1)))
		--(*end);
}

static int promql_ident_char(int c)
{
	return isalnum((unsigned char)c) || c == '_' || c == ':' || c == '.';
}

static const char *promql_find_matching(const char *start, const char *end, char open, char close)
{
	int depth = 0;
	int in_quotes = 0;
	int escaped = 0;

	for (const char *p = start; p < end; ++p)
	{
		char c = *p;
		if (in_quotes)
		{
			if (escaped)
				escaped = 0;
			else if (c == '\\')
				escaped = 1;
			else if (c == '"')
				in_quotes = 0;
			continue;
		}

		if (c == '"')
		{
			in_quotes = 1;
			continue;
		}

		if (c == open)
			++depth;
		else if (c == close)
		{
			--depth;
			if (depth == 0)
				return p;
		}
	}

	return NULL;
}

static int promql_parse_func(const char *s, size_t len)
{
	if (len == 5 && !strncasecmp(s, "count", 5))
		return QUERY_FUNC_COUNT;
	if (len == 3 && !strncasecmp(s, "sum", 3))
		return QUERY_FUNC_SUM;
	if (len == 6 && !strncasecmp(s, "absent", 6))
		return QUERY_FUNC_ABSENT;
	if (len == 7 && !strncasecmp(s, "present", 7))
		return QUERY_FUNC_PRESENT;
	if (len == 3 && !strncasecmp(s, "min", 3))
		return QUERY_FUNC_MIN;
	if (len == 3 && !strncasecmp(s, "max", 3))
		return QUERY_FUNC_MAX;
	if (len == 3 && !strncasecmp(s, "avg", 3))
		return QUERY_FUNC_AVG;
	return QUERY_FUNC_NONE;
}

static void promql_groupkey_add_range(string *groupkey, const char *begin, const char *end)
{
	const char *cur = begin;

	while (cur < end)
	{
		while (cur < end && (isspace((unsigned char)*cur) || *cur == ','))
			++cur;
		const char *token_begin = cur;
		while (cur < end && *cur != ',')
			++cur;
		const char *token_end = cur;
		promql_trim_range(&token_begin, &token_end);
		if (token_begin < token_end)
		{
			if (groupkey->l)
				string_cat(groupkey, ",", 1);
			string_cat(groupkey, (char *)token_begin, token_end - token_begin);
		}
	}
}

static int promql_parse_groupby_clause(const char **p, const char *end, string *groupkey)
{
	const char *cur = *p;
	promql_skip_ws(&cur, end);
	if (cur + 2 > end || strncasecmp(cur, "by", 2))
		return 0;

	if (cur + 2 < end && promql_ident_char(cur[2]))
		return 0;

	cur += 2;
	promql_skip_ws(&cur, end);
	if (cur >= end || *cur != '(')
		return 0;

	const char *close = promql_find_matching(cur, end, '(', ')');
	if (!close)
		return 0;

	promql_groupkey_add_range(groupkey, cur + 1, close);
	cur = close + 1;
	*p = cur;
	return 1;
}

static int promql_try_parse_trailing_groupby(const char *begin, const char *end, string *groupkey, const char **new_end)
{
	int in_quotes = 0;
	int escaped = 0;
	int depth_paren = 0;
	int depth_brace = 0;

	for (const char *p = begin; p + 1 < end; ++p)
	{
		char c = *p;
		if (in_quotes)
		{
			if (escaped)
				escaped = 0;
			else if (c == '\\')
				escaped = 1;
			else if (c == '"')
				in_quotes = 0;
			continue;
		}

		if (c == '"')
		{
			in_quotes = 1;
			continue;
		}
		if (c == '{')
		{
			++depth_brace;
			continue;
		}
		if (c == '}')
		{
			if (depth_brace > 0)
				--depth_brace;
			continue;
		}
		if (c == '(')
		{
			++depth_paren;
			continue;
		}
		if (c == ')')
		{
			if (depth_paren > 0)
				--depth_paren;
			continue;
		}
		if (depth_brace || depth_paren)
			continue;

		if (strncasecmp(p, "by", 2))
			continue;
		if (p > begin && promql_ident_char(p[-1]))
			continue;
		if (p + 2 < end && promql_ident_char(p[2]))
			continue;

		const char *tmp = p;
		if (!promql_parse_groupby_clause(&tmp, end, groupkey))
			continue;

		promql_skip_ws(&tmp, end);
		if (tmp == end || *tmp == '>' || *tmp == '<' || *tmp == '=' || *tmp == '!')
		{
			*new_end = p;
			return 1;
		}
	}

	return 0;
}

static void promql_parse_comparator(const char *begin, const char *end, metric_query_context *mqc)
{
	int in_quotes = 0;
	int escaped = 0;
	int depth_brace = 0;

	for (const char *p = begin; p < end; ++p)
	{
		char c = *p;
		if (in_quotes)
		{
			if (escaped)
				escaped = 0;
			else if (c == '\\')
				escaped = 1;
			else if (c == '"')
				in_quotes = 0;
			continue;
		}

		if (c == '"')
		{
			in_quotes = 1;
			continue;
		}

		if (c == '{')
		{
			++depth_brace;
			continue;
		}
		if (c == '}')
		{
			if (depth_brace > 0)
				--depth_brace;
			continue;
		}
		if (depth_brace)
			continue;

		uint8_t op = QUERY_OPERATOR_NOOP;
		size_t op_len = 0;
		if (c == '=' && p + 1 < end && p[1] == '=')
		{
			op = QUERY_OPERATOR_EQ;
			op_len = 2;
		}
		else if (c == '!' && p + 1 < end && p[1] == '=')
		{
			op = QUERY_OPERATOR_NE;
			op_len = 2;
		}
		else if (c == '>' && p + 1 < end && p[1] == '=')
		{
			op = QUERY_OPERATOR_GE;
			op_len = 2;
		}
		else if (c == '<' && p + 1 < end && p[1] == '=')
		{
			op = QUERY_OPERATOR_LE;
			op_len = 2;
		}
		else if (c == '>')
		{
			op = QUERY_OPERATOR_GT;
			op_len = 1;
		}
		else if (c == '<')
		{
			op = QUERY_OPERATOR_LT;
			op_len = 1;
		}

		if (!op)
			continue;

		const char *rhs = p + op_len;
		promql_skip_ws(&rhs, end);
		mqc->op = op;
		mqc->opval = strtod(rhs, NULL);
		return;
	}
}

static void promql_parse_labels(alligator_ht *lbl, const char *begin, const char *end)
{
	char key[255];
	char value[255];
	const char *cur = begin;

	while (cur < end)
	{
		while (cur < end && (isspace((unsigned char)*cur) || *cur == ','))
			++cur;
		if (cur >= end)
			break;

		const char *key_begin = cur;
		while (cur < end && !isspace((unsigned char)*cur) && *cur != '=' && *cur != ',')
			++cur;
		const char *key_end = cur;
		promql_trim_range(&key_begin, &key_end);
		if (key_begin >= key_end)
		{
			++cur;
			continue;
		}

		promql_skip_ws(&cur, end);
		if (cur >= end || *cur != '=')
		{
			while (cur < end && *cur != ',')
				++cur;
			continue;
		}
		++cur;
		promql_skip_ws(&cur, end);

		const char *value_begin = cur;
		const char *value_end = cur;
		if (cur < end && *cur == '"')
		{
			++value_begin;
			++cur;
			int escaped = 0;
			while (cur < end)
			{
				if (escaped)
				{
					escaped = 0;
					++cur;
					continue;
				}
				if (*cur == '\\')
				{
					escaped = 1;
					++cur;
					continue;
				}
				if (*cur == '"')
					break;
				++cur;
			}
			value_end = cur;
			if (cur < end && *cur == '"')
				++cur;
		}
		else
		{
			while (cur < end && *cur != ',' && !isspace((unsigned char)*cur))
				++cur;
			value_end = cur;
		}

		size_t key_len = key_end - key_begin;
		if (key_len > sizeof(key) - 1)
			key_len = sizeof(key) - 1;
		memcpy(key, key_begin, key_len);
		key[key_len] = 0;

		size_t value_len = value_end - value_begin;
		if (value_len > sizeof(value) - 1)
			value_len = sizeof(value) - 1;
		memcpy(value, value_begin, value_len);
		value[value_len] = 0;

		labels_hash_insert_nocache(lbl, key, value);

		while (cur < end && *cur != ',')
			++cur;
	}
}

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
	*name = 0;
	string *groupkey = mqc->groupkey = string_new();
	if (!size || !query)
		return mqc;

	if (!lbl)
		lbl = alligator_ht_init(NULL);

	mqc->func = QUERY_FUNC_NONE;
	const char *begin = query;
	const char *end = query + size;
	promql_trim_range(&begin, &end);
	if (begin < end)
	{
		const char *lhs_begin = begin;
		const char *lhs_end = end;
		promql_parse_comparator(begin, end, mqc);
		promql_trim_range(&lhs_begin, &lhs_end);
		if (lhs_begin < lhs_end)
		{
			const char *selector_begin = lhs_begin;
			const char *selector_end = lhs_end;
			const char *cur = lhs_begin;
			const char *ident_end = cur;
			while (ident_end < lhs_end && promql_ident_char(*ident_end))
				++ident_end;

			if (ident_end > cur)
			{
				int func = promql_parse_func(cur, ident_end - cur);
				const char *after_ident = ident_end;
				promql_skip_ws(&after_ident, lhs_end);
				if (func != QUERY_FUNC_NONE && (after_ident < lhs_end && *after_ident == '('))
				{
					const char *close = promql_find_matching(after_ident, lhs_end, '(', ')');
					if (close)
					{
						mqc->func = func;
						selector_begin = after_ident + 1;
						selector_end = close;
						cur = close + 1;
						promql_parse_groupby_clause(&cur, lhs_end, groupkey);
					}
				}
				else if (func != QUERY_FUNC_NONE)
				{
					const char *tmp = after_ident;
					if (promql_parse_groupby_clause(&tmp, lhs_end, groupkey))
					{
						promql_skip_ws(&tmp, lhs_end);
						if (tmp < lhs_end && *tmp == '(')
						{
							const char *close = promql_find_matching(tmp, lhs_end, '(', ')');
							if (close)
							{
								mqc->func = func;
								selector_begin = tmp + 1;
								selector_end = close;
								cur = close + 1;
								promql_parse_groupby_clause(&cur, lhs_end, groupkey);
							}
						}
					}
				}
			}

			if (!mqc->func)
			{
				const char *tmp = selector_begin;
				if (promql_parse_groupby_clause(&tmp, selector_end, groupkey))
					selector_begin = tmp;
				else
				{
					const char *trim_end = selector_end;
					if (promql_try_parse_trailing_groupby(selector_begin, selector_end, groupkey, &trim_end))
						selector_end = trim_end;
				}
			}

			promql_trim_range(&selector_begin, &selector_end);
			while (selector_begin < selector_end && *selector_begin == '(')
			{
				const char *close = promql_find_matching(selector_begin, selector_end, '(', ')');
				if (close == selector_end - 1)
				{
					++selector_begin;
					--selector_end;
					promql_trim_range(&selector_begin, &selector_end);
				}
				else
					break;
			}

			const char *metric_begin = selector_begin;
			const char *metric_end = metric_begin;
			while (metric_end < selector_end && !isspace((unsigned char)*metric_end) && *metric_end != '{' && *metric_end != '(' && *metric_end != ')')
				++metric_end;

			size_t name_len = metric_end - metric_begin;
			if (name_len > 254)
				name_len = 254;
			if (name_len)
			{
				memcpy(name, metric_begin, name_len);
				name[name_len] = 0;
			}
			else
			{
				free(name);
				mqc->name = NULL;
				name = NULL;
			}

			const char *lb = metric_end;
			while (lb < selector_end && *lb != '{')
				++lb;
			if (lb < selector_end && *lb == '{')
			{
				const char *rb = promql_find_matching(lb, selector_end, '{', '}');
				if (rb)
					promql_parse_labels(lbl, lb + 1, rb);
			}
		}
	}

	glog(L_DEBUG, "promql_parser: query='%.*s' name='%s' func=%d groupby='%s' labels=%zu op=%u opval=%lf\n", (int)size, query ? query : "", mqc->name ? mqc->name : "", mqc->func, (mqc->groupkey && mqc->groupkey->s) ? mqc->groupkey->s : "", lbl ? alligator_ht_count(lbl) : 0, mqc->op, mqc->opval);

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
