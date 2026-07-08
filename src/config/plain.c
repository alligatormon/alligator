#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "config/plain.h"
#include "common/json_query.h"
#include "common/selector.h"
#include "common/logs.h"
#include "main.h"

typedef struct config_parser_stat {
	uint8_t quotas1; // '
	uint8_t quotas2; // "
	uint8_t semicolon; // ;
	uint8_t end; // end of context }
	uint8_t context;
	uint8_t start; // start of context {
	uint8_t operator; // is operator
	uint8_t argument; // is argument
	uint64_t length; // length of word
	uint64_t fact_length; // length of word and control symbols
	string *token;
} config_parser_stat;

char *plain_skip_spaces(char *str, char *sep)
{
	//printf("in addr: %p\n", str);
	if (!str)
		return NULL;

	str += strspn(str, sep);

	//printf("out addr: %p\n", str);
	return str;
}

char *plain_get_token(char *str, char *sep)
{
	//printf("in addr: %p (%s)\n", str, sep);
	if (!str)
		return NULL;

	str += strcspn(str, sep);

	//printf("out addr: %p\n", str);
	return str;
}

void plain_get_word(char *str, config_parser_stat *ret)
{
	char tmp[1000];

	ret->length = strcspn(str, " \r\t\n;\"'{}");
	ret->fact_length = strcspn(str, " \r\t\n");

	ret->quotas1 = 0;
	ret->quotas2 = 0;

	if (ret->semicolon || ret->start || ret->end)
	{
		ret->context = 0;
		ret->argument = 0;
		ret->operator = 0;
		ret->semicolon = 0;
		ret->start = 0;
		ret->end = 0;
	}

	if (!ret->operator && !ret->argument && !ret->context && !ret->start && !ret->semicolon && !ret->quotas1 && !ret->quotas2 && !ret->end)
	{
		uint64_t sq1 = strcspn(str, "'");
		uint64_t sq2 = strcspn(str, "\"");

		uint64_t sm = strcspn(str, ";");
		uint64_t st = strcspn(str, "{");

		if ((sq1 < sm) && (sq1 < st))
		{
			sq1 += strcspn(str+sq1+1, "'");
			sm = strcspn(str+sq1, ";");
			st = strcspn(str+sq1, "{");
		}
		if ((sq2 < sm) && (sq2 < st))
		{
			sq2 += strcspn(str+sq2+1, "\"");
			sm = strcspn(str+sq2, ";");
			st = strcspn(str+sq2, "{");
		}

		if (st < sm) {
			ret->context = 1;
			ret->operator = 0;
		}
		else
		{
			ret->context = 0;
			ret->operator = 1;
		}
	}
	else
	{
		ret->quotas1 = 0;
		ret->quotas2 = 0;
		ret->start = 0;
		ret->end = 0;

		if (ret->operator)
		{
			ret->operator = 0;
			ret->argument = 1;
		}

		if (ret->context)
		{
			ret->context = 0;
		}
	}

	size_t tmp_copy = ret->fact_length < (sizeof(tmp) - 1) ? ret->fact_length : (sizeof(tmp) - 1);
	strlcpy(tmp, str, tmp_copy + 1);

	for (uint64_t trigger = 0; trigger < ret->fact_length; trigger++)
	{
		trigger += strcspn(tmp+trigger, "'\"{};");

		if (tmp[trigger] == '\'')
		{
			ret->quotas1 = 1;
			ret->length = 1;
			break;
		}
		if (tmp[trigger] == '"')
		{
			ret->quotas2 = 1;
			ret->length = 1;
			break;
		}
		if (tmp[trigger] == ';')
		{
			ret->semicolon = 1;
		}
		if (tmp[trigger] == '{')
		{
			ret->start = 1;
		}
		if (tmp[trigger] == '}')
		{
			ret->end = 1;
		}
	}

	tmp[ret->length] = 0;

	if (!ret->length)
	{
		ret->argument = 0;
		ret->context = 0;
		ret->operator = 0;
	}
}

string *plain_get_quotas_word(char *str, uint64_t len, config_parser_stat *wstat)
{
	uint64_t i;
	string *ret = string_init(255);
	char quotas_sym = 0;
	for (i = 0; i < len; i++)
	{
		if (((i == 0) || (str[i-1] != '\\')) && ((str[i] == '\'') || (str[i] == '"')))
		{
			if (!quotas_sym)
			{
				quotas_sym = str[i];
			}
			else if (quotas_sym == str[i])
			{
				quotas_sym = 0;
			}
			else
			{
				string_cat(ret, str+i, 1);
			}
		}
		else if (!quotas_sym)
		{
			if (isspace(str[i]))
			{
				break;
			}
			else if (str[i] == ';')
				wstat->semicolon = 1;
			else if (str[i] == '}')
				wstat->end = 1;
		}
		else if ((str[i] == '\\') && ((str[i+1] == '\'') || (str[i+1] == '"'))) {}
		//	string_cat(ret, str+i+1, 1);
		else
			string_cat(ret, str+i, 1);
	}

	//printf("replace fact length from %"u64" to %"u64"\n", wstat->fact_length, i);
	wstat->fact_length = i;

	return ret;
}

config_parser_stat* string_tokenizer(string *context, uint64_t *token_count)
{
	if (!context)
		return NULL;

	char *tmp = context->s;
	//config_parser_stat wstat = { 0 };
	config_parser_stat *retws = calloc(1, *token_count * sizeof(config_parser_stat));

	uint64_t i;
	for (i = 0; (tmp - context->s) < context->l; i++)
	{
		char *start = plain_skip_spaces(tmp, " \n\r\t");
		if (!start)
			break;
		tmp = start;

		if (i)
			memcpy(&retws[i], &retws[i-1], sizeof(config_parser_stat));

		plain_get_word(start, &retws[i]);

		string *elem = string_init(retws[i].length);

		if (retws[i].quotas1)
		{
			string *quotastring = plain_get_quotas_word(start, context->l, &retws[i]);
			//printf("quotastring1\n");
			string_merge(elem, quotastring);
		}
		else if (retws[i].quotas2)
		{
			string *quotastring = plain_get_quotas_word(start, context->l, &retws[i]);
			//printf("quotastring2\n");
			string_merge(elem, quotastring);
		}
		else
			string_cat(elem, tmp, retws[i].length);

		tmp = tmp + retws[i].fact_length;
		retws[i].token = elem;
	}

	*token_count = i;
	return retws;
}

uint64_t plain_count_get(string *context)
{
	uint64_t j, i;
	for (j = 0, i = 0; i < context->l; i++, ++j)
	{
		i += strcspn(context->s+i, " \t\r\n;{}");
	}

	return j;
}

static string *plain_strip_comments(string *context)
{
	if (!context || !context->s)
		return NULL;

	string *clean = string_init(context->l + 1);
	if (!clean)
		return NULL;

	uint8_t in_single_quote = 0;
	uint8_t in_double_quote = 0;
	uint8_t escaped = 0;
	uint8_t in_line_comment = 0;
	uint8_t in_block_comment = 0;

	for (size_t i = 0; i < context->l; ++i)
	{
		char ch = context->s[i];
		char next = (i + 1 < context->l) ? context->s[i + 1] : '\0';
		char prev_nonspace = '\0';
		for (int64_t p = (int64_t)i - 1; p >= 0; --p)
		{
			if (!isspace((unsigned char)context->s[p]))
			{
				prev_nonspace = context->s[p];
				break;
			}
		}

		if (in_line_comment)
		{
			if (ch == '\n')
			{
				in_line_comment = 0;
				string_cat(clean, &ch, 1);
			}
			else
			{
				char space = ' ';
				string_cat(clean, &space, 1);
			}
			continue;
		}

		if (in_block_comment)
		{
			if (ch == '*' && next == '/')
			{
				char space = ' ';
				string_cat(clean, &space, 1);
				string_cat(clean, &space, 1);
				in_block_comment = 0;
				++i;
			}
			else if (ch == '\n')
			{
				string_cat(clean, &ch, 1);
			}
			else
			{
				char space = ' ';
				string_cat(clean, &space, 1);
			}
			continue;
		}

		if (in_single_quote || in_double_quote)
		{
			string_cat(clean, &ch, 1);
			if (escaped)
			{
				escaped = 0;
			}
			else if (ch == '\\')
			{
				escaped = 1;
			}
			else if (in_single_quote && ch == '\'')
			{
				in_single_quote = 0;
			}
			else if (in_double_quote && ch == '"')
			{
				in_double_quote = 0;
			}
			continue;
		}

		if (ch == '#')
		{
			char space = ' ';
			string_cat(clean, &space, 1);
			in_line_comment = 1;
			continue;
		}

		if (ch == '/' && next == '*'
			&& (!prev_nonspace || prev_nonspace == ';' || prev_nonspace == '{' || prev_nonspace == '}'))
		{
			char space = ' ';
			string_cat(clean, &space, 1);
			string_cat(clean, &space, 1);
			in_block_comment = 1;
			++i;
			continue;
		}

		if (ch == '\'')
			in_single_quote = 1;
		else if (ch == '"')
			in_double_quote = 1;

		string_cat(clean, &ch, 1);
	}

	return clean;
}

static void plain_context_env_line(json_t *env_obj, string *tok)
{
	const char *s;
	size_t len;

	if (!env_obj || !tok)
		return;
	s = tok->s;
	len = tok->l;
	if (!s || !len)
		return;
	if (len >= 2 && s[0] == '\'' && s[len - 1] == '\'') {
		s++;
		len -= 2;
	} else if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
		s++;
		len -= 2;
	}
	const char *colon = memchr(s, ':', len);
	if (!colon || colon == s)
		return;
	size_t key_len = (size_t)(colon - s);
	while (key_len > 0 && isspace((unsigned char)s[key_len - 1]))
		key_len--;
	if (!key_len)
		return;
	const char *v = colon + 1;
	size_t vlen = len - (size_t)(v - s);
	while (vlen > 0 && isspace((unsigned char)v[0])) {
		v++;
		vlen--;
	}
	while (vlen > 0 && isspace((unsigned char)v[vlen - 1]))
		vlen--;
	char kbuf[256];
	if (key_len >= sizeof(kbuf))
		key_len = sizeof(kbuf) - 1;
	memcpy(kbuf, s, key_len);
	kbuf[key_len] = '\0';
	char *vbuf = calloc(1, vlen + 1);
	if (!vbuf)
		return;
	memcpy(vbuf, v, vlen);
	json_array_object_insert(env_obj, kbuf, json_string(vbuf));
	free(vbuf);
}

/* Native plain metricstransform (no JSON): metricstransform { include M match_type strict label L regex R replacement S; } */
static void plain_mtx_stmt_parse(config_parser_stat *wstokens, uint64_t a, uint64_t b, json_t *transforms)
{
	json_t *t = json_object();
	json_array_append_new(transforms, t);
	json_t *ops = json_array();
	json_object_set_new(t, "operations", ops);
	json_t *op = json_object();
	json_array_append_new(ops, op);
	json_object_set_new(op, "action", json_string("update_label"));
	json_t *vas = json_array();
	json_object_set_new(op, "value_actions", vas);

	json_t *cur_va = NULL;
	uint64_t p = a;

	while (p < b)
	{
		while (p < b && !wstokens[p].operator && !wstokens[p].argument)
			p++;
		if (p >= b)
			break;
		const char *kw = wstokens[p].token->s;
		if (!kw)
		{
			p++;
			continue;
		}
		if (!strcmp(kw, "regex"))
		{
			p++;
			if (p < b)
			{
				cur_va = json_object();
				json_object_set_new(cur_va, "regex", json_string(wstokens[p].token->s));
				json_array_append_new(vas, cur_va);
				p++;
			}
			continue;
		}
		if (!strcmp(kw, "replacement") || !strcmp(kw, "new_value"))
		{
			p++;
			if (p < b && cur_va)
				json_object_set_new(cur_va, "replacement", json_string(wstokens[p].token->s));
			if (p < b)
				p++;
			continue;
		}
		if (!strcmp(kw, "replace_all"))
		{
			p++;
			if (p < b && cur_va)
			{
				const char *vv = wstokens[p].token->s;
				json_t *bv = (!strcmp(vv, "true") || !strcmp(vv, "1")) ? json_true() : json_false();

				json_object_set_new(cur_va, "replace_all", bv);
			}
			if (p < b)
				p++;
			continue;
		}
		p++;
		if (p >= b)
			break;
		if (!strcmp(kw, "include"))
			json_object_set_new(t, "include", json_string(wstokens[p].token->s));
		else if (!strcmp(kw, "metric"))
			json_object_set_new(t, "metric", json_string(wstokens[p].token->s));
		else if (!strcmp(kw, "metric_regex"))
			json_object_set_new(t, "metric_regex", json_string(wstokens[p].token->s));
		else if (!strcmp(kw, "match_type"))
			json_object_set_new(t, "match_type", json_string(wstokens[p].token->s));
		else if (!strcmp(kw, "label"))
			json_object_set_new(op, "label", json_string(wstokens[p].token->s));
		else if (!strcmp(kw, "label_regex"))
			json_object_set_new(op, "label_regex", json_string(wstokens[p].token->s));
		else if (!strcmp(kw, "new_label"))
			json_object_set_new(op, "new_label", json_string(wstokens[p].token->s));
		p++;
	}
}

static int plain_metricstransform_has_native_block(config_parser_stat *wstokens, uint64_t idx, uint64_t token_count)
{
	if (!wstokens || idx >= token_count || !wstokens[idx].token->s || strcmp(wstokens[idx].token->s, "metricstransform"))
		return 0;
	if (wstokens[idx].start)
		return 1;

	for (uint64_t k = idx + 1; k < token_count; k++)
	{
		if (wstokens[k].start)
			return 1;
		/* In plain config tokenization, native blocks may begin immediately with
		 * operator/context tokens (without an explicit '{' token). Inline JSON/object
		 * form uses an argument token right after metricstransform, so keep that path
		 * as non-native for the fallback parser below. */
		if (wstokens[k].operator || wstokens[k].context)
			return 1;
		if (wstokens[k].argument || wstokens[k].end || wstokens[k].semicolon)
			return 0;
	}
	return 0;
}

static char *plain_strip_trailing_semicolon_ws(const char *value);

/* Join block tokens into one string (for JSON metricstransform { "transforms": ... }). */
static char *plain_metricstransform_join_tokens(config_parser_stat *wstokens, uint64_t from, uint64_t to)
{
	size_t cap = 256;
	size_t len = 0;
	char *buf = malloc(cap);

	if (!buf || from >= to)
		return buf;

	for (uint64_t i = from; i < to; ++i)
	{
		const char *s;
		size_t slen;

		if (!wstokens[i].token || !wstokens[i].token->s)
			continue;
		s = wstokens[i].token->s;
		slen = strlen(s);
		if (!slen)
			continue;
		if (len + slen + 2 > cap)
		{
			cap = (len + slen + 2) * 2;
			char *nb = realloc(buf, cap);
			if (!nb)
			{
				free(buf);
				return NULL;
			}
			buf = nb;
		}
		if (len)
			buf[len++] = ' ';
		memcpy(buf + len, s, slen);
		len += slen;
	}
	if (!len)
	{
		free(buf);
		return NULL;
	}
	buf[len] = '\0';
	return buf;
}

static json_t *plain_try_parse_metricstransform_json_text(const char *text)
{
	json_error_t error;
	json_t *parsed;
	char *t;
	char *wrapped;

	if (!text)
		return NULL;

	t = plain_strip_trailing_semicolon_ws(text);
	if (!t)
		return NULL;

	parsed = json_loads(t, 0, &error);
	if (!parsed && !strncmp(t, "\"transforms\"", 12))
	{
		size_t wlen = strlen(t) + 3;
		wrapped = malloc(wlen);
		if (wrapped)
		{
			snprintf(wrapped, wlen, "{%s}", t);
			parsed = json_loads(wrapped, 0, &error);
			free(wrapped);
		}
	}
	free(t);
	if (!parsed)
		return NULL;

	if (json_is_object(parsed) && json_object_get(parsed, "transforms"))
		return parsed;
	if (json_is_array(parsed))
		return parsed;

	json_decref(parsed);
	return NULL;
}

/* *idx = index of metricstransform context token; on return *idx = index of inner closing }; that token's .end is cleared. */
static json_t *plain_metricstransform_parse_native_block(config_parser_stat *wstokens, uint64_t *idx, uint64_t token_count)
{
	json_t *root;
	json_t *transforms;
	uint64_t block_end;

	if (!idx || *idx >= token_count || !wstokens[*idx].token->s || strcmp(wstokens[*idx].token->s, "metricstransform"))
		return NULL;

	block_end = *idx + 1;
	while (block_end < token_count && !wstokens[block_end].end)
		block_end++;
	if (block_end < token_count && wstokens[block_end].end)
	{
		char *joined = plain_metricstransform_join_tokens(wstokens, *idx + 1, block_end);
		json_t *json_mtx = joined ? plain_try_parse_metricstransform_json_text(joined) : NULL;

		free(joined);
		if (json_mtx)
		{
			*idx = block_end;
			wstokens[block_end].end = 0;
			return json_mtx;
		}
	}

	root = json_object();
	transforms = json_array();
	json_object_set_new(root, "transforms", transforms);

	uint64_t k = *idx + 1;
	int closed = 0;

	while (k < token_count)
	{
		if (wstokens[k].end)
		{
			*idx = k;
			wstokens[k].end = 0;
			closed = 1;
			break;
		}
		while (k < token_count && !wstokens[k].operator && !wstokens[k].argument && !wstokens[k].end)
			k++;
		if (k >= token_count)
			break;
		if (wstokens[k].end)
		{
			*idx = k;
			wstokens[k].end = 0;
			closed = 1;
			break;
		}
		uint64_t stmt_start = k;
		while (k < token_count && !wstokens[k].semicolon && !wstokens[k].end)
			k++;
		uint64_t stmt_end = k;
		/* plain_get_quotas_word marks trailing ';' on the same token as a quoted value (e.g. 'active';).
		 * Without extending stmt_end, plain_mtx_stmt_parse would stop before that value. */
		if (k < token_count && wstokens[k].semicolon && !wstokens[k].end &&
		    (wstokens[k].argument || wstokens[k].quotas1 || wstokens[k].quotas2))
			stmt_end = k + 1;
		if (stmt_end > stmt_start)
			plain_mtx_stmt_parse(wstokens, stmt_start, stmt_end, transforms);
		if (k < token_count && wstokens[k].semicolon)
			k++;
	}
	if (!closed)
	{
		json_decref(root);
		return NULL;
	}
	return root;
}

/* Strip trailing semicolon / spaces (plain-config tokens often include `;` before `}`). */
static char *plain_strip_trailing_semicolon_ws(const char *value)
{
	size_t len;
	char *out;

	if (!value)
		return NULL;
	len = strlen(value);
	while (len > 0 && isspace((unsigned char)value[len - 1]))
		len--;
	while (len > 0 && value[len - 1] == ';')
		len--;
	while (len > 0 && isspace((unsigned char)value[len - 1]))
		len--;
	out = malloc(len + 1);
	if (!out)
		return NULL;
	memcpy(out, value, len);
	out[len] = '\0';
	return out;
}

/* Parse inline JSON object for metricstransform (handles optional '...' / "..." wrapping). */
static json_t *plain_parse_metricstransform_object(const char *value)
{
	json_error_t error;
	json_t *parsed;
	char *t;
	char *inner;
	size_t len;

	if (!value)
		return NULL;
	t = plain_strip_trailing_semicolon_ws(value);
	if (!t)
		return NULL;

	len = strlen(t);
	if (len >= 2 && t[0] == '\'' && t[len - 1] == '\'')
	{
		inner = malloc(len - 1);
		if (!inner)
		{
			free(t);
			return NULL;
		}
		memcpy(inner, t + 1, len - 2);
		inner[len - 2] = '\0';
		free(t);
		t = plain_strip_trailing_semicolon_ws(inner);
		free(inner);
		if (!t)
			return NULL;
	}
	else if (len >= 2 && t[0] == '"' && t[len - 1] == '"')
	{
		inner = malloc(len - 1);
		if (!inner)
		{
			free(t);
			return NULL;
		}
		memcpy(inner, t + 1, len - 2);
		inner[len - 2] = '\0';
		free(t);
		t = plain_strip_trailing_semicolon_ws(inner);
		free(inner);
		if (!t)
			return NULL;
	}

	parsed = json_loads(t, 0, &error);
	free(t);
	if (parsed && (json_is_object(parsed) || json_is_array(parsed)))
		return parsed;
	if (parsed)
		json_decref(parsed);
	return NULL;
}

static json_t* plain_json_or_string(const char *value);

/* Parse metricstransform from current token index.
 * Supports native block form and the "metricstransform <json>" argument form.
 * On success updates *idx to the last consumed token. */
static json_t *plain_metricstransform_parse(config_parser_stat *wstokens, uint64_t *idx, uint64_t token_count)
{
	json_t *mtx;
	uint64_t j;

	if (!wstokens || !idx || *idx >= token_count || !wstokens[*idx].token->s ||
	    strcmp(wstokens[*idx].token->s, "metricstransform"))
		return NULL;

	j = *idx;
	mtx = plain_metricstransform_has_native_block(wstokens, *idx, token_count)
		? plain_metricstransform_parse_native_block(wstokens, &j, token_count)
		: NULL;
	if (!mtx && *idx + 1 < token_count && wstokens[*idx + 1].argument)
	{
		mtx = plain_parse_metricstransform_object(wstokens[*idx + 1].token->s);
		if (!mtx)
			mtx = plain_json_or_string(wstokens[*idx + 1].token->s);
		if (mtx)
			j = *idx + 1;
	}

	if (!mtx)
		return NULL;

	*idx = j;
	return mtx;
}

static uint8_t plain_puppeteer_insert_option(json_t *dst_obj, config_parser_stat *wstokens, uint64_t *idx, uint64_t token_count)
{
	const char *token;
	const char *sep;
	const char *value;
	const char *colon;
	size_t key_len;
	size_t value_len;
	size_t nested_key_len;
	char key[256];
	char nested_key[256];
	char *value_copy;
	json_t *nested_obj;
	json_t *final_value;

	if (!dst_obj || !wstokens || !idx || *idx >= token_count)
		return 0;

	token = wstokens[*idx].token->s;
	if (!token)
		return 0;

	sep = strchr(token, '=');
	if (!sep || sep == token)
		return 0;

	key_len = (size_t)(sep - token);
	if (key_len >= sizeof(key))
		key_len = sizeof(key) - 1;

	memcpy(key, token, key_len);
	key[key_len] = '\0';

	value = sep + 1;
	value_len = strlen(value);
	if (!value_len && *idx + 1 < token_count && !strcmp(key, "metricstransform"))
	{
		++(*idx);
		value = wstokens[*idx].token->s;
		value_len = strlen(value);
	}
	else if (!value_len && (*idx + 1 < token_count) && wstokens[*idx + 1].argument)
	{
		++(*idx);
		value = wstokens[*idx].token->s;
		value_len = strlen(value);
	}

	if (!strcmp(key, "headers") || !strcmp(key, "env") || !strcmp(key, "add_label") || !strcmp(key, "screenshot"))
	{
		colon = strchr(value, ':');
		if (!colon || colon == value)
			return 0;

		nested_key_len = (size_t)(colon - value);
		if (nested_key_len >= sizeof(nested_key))
			nested_key_len = sizeof(nested_key) - 1;
		memcpy(nested_key, value, nested_key_len);
		nested_key[nested_key_len] = '\0';

		nested_obj = json_object_get(dst_obj, key);
		if (!nested_obj || json_typeof(nested_obj) != JSON_OBJECT)
		{
			nested_obj = json_object();
			json_array_object_insert(dst_obj, key, nested_obj);
		}

		value_copy = strdup(colon + 1);
		if (!value_copy)
			return 0;

		if (!strcmp(key, "screenshot") && !strcmp(nested_key, "fullPage"))
		{
			if (!strcmp(value_copy, "true"))
				final_value = json_true();
			else if (!strcmp(value_copy, "false"))
				final_value = json_false();
			else
				final_value = json_string(value_copy);
		}
		else if (!strcmp(key, "screenshot") && !strcmp(nested_key, "minimum_code"))
		{
			if (sisdigit(value_copy))
				final_value = json_integer(strtoll(value_copy, NULL, 10));
			else
				final_value = json_string(value_copy);
		}
		else
		{
			final_value = json_string(value_copy);
		}

		json_array_object_insert(nested_obj, nested_key, final_value);
		free(value_copy);
		return 1;
	}

	if (!strcmp(key, "metricstransform"))
	{
		json_t *parsed = plain_parse_metricstransform_object(value);
		if (!parsed)
			return 0;
		json_array_object_insert(dst_obj, key, parsed);
		return 1;
	}

	json_array_object_insert(dst_obj, key, json_string(value));
	return 1;
}

static json_t* plain_json_or_string(const char *value)
{
	if (!value)
		return NULL;
	json_error_t error;
	json_t *parsed = json_loads(value, 0, &error);
	if (parsed)
		return parsed;
	return json_string(value);
}

char *build_json_from_tokens(config_parser_stat *wstokens, uint64_t token_count)
{
	json_t *root = json_object();
	for (uint64_t i = 0; i < token_count; i++)
	{
		if (wstokens[i].operator)
		{
			char *operator_name = wstokens[i].token->s;
			++i;
			if (wstokens[i].argument)
			{
				if (sisdigit(wstokens[i].token->s))
				{
					int64_t num = strtoll(wstokens[i].token->s, NULL, 10);
					json_t *arg_json = json_integer(num);
					json_object_set_new(root, operator_name, arg_json);
				}
				else if (!strcmp(wstokens[i-1].token->s, "grok_patterns"))
				{
					json_t *grok_patterns_data = json_array();
					json_object_set_new(root, operator_name, grok_patterns_data);

						for (; i < token_count; i++)
						{
							if (wstokens[i].argument)
							{
								json_t *arg_value = json_string(wstokens[i].token->s);
								json_array_object_insert(grok_patterns_data, NULL, arg_value);
							}

							if (wstokens[i].semicolon)
							{
								break;
							}
						}
				}
				else
				{
					json_t *arg_json = json_string(wstokens[i].token->s);
					json_object_set_new(root, operator_name, arg_json);
				}
			}
			
		}
		else if (wstokens[i].context)
		{
			char *context_name = wstokens[i].token->s;
			char *operator_name = NULL;
			json_t *allow_entrypoint = NULL;
			json_t *deny_entrypoint = NULL;
			json_t *tcp_entrypoint = NULL;
			json_t *udp_entrypoint = NULL;
			json_t *tls_entrypoint = NULL;
			json_t *unix_entrypoint = NULL;
			json_t *unixgram_entrypoint = NULL;
			json_t *handler_entrypoint = NULL;
			json_t *mapping_entrypoint = NULL;
			json_t *api_entrypoint = NULL;
			json_t *ttl_entrypoint = NULL;
			json_t *mtail_full_export_interval_entrypoint = NULL;
			json_t *read_metric_interval_entrypoint = NULL;
			json_t *log_level_entrypoint = NULL;
			json_t *log_channel_entrypoint = NULL;
			json_t *cluster_entrypoint = NULL;
			json_t *instance_entrypoint = NULL;
			json_t *threads_entrypoint = NULL;
			json_t *lang_entrypoint = NULL;
			json_t *metric_aggregation_entrypoint = NULL;
			json_t *key_entrypoint = NULL;
			json_t *return_entrypoint = NULL;
			json_t *grok_entrypoint = NULL;
			json_t *mtail_entrypoint = NULL;
			json_t *namespace_entrypoint = NULL;
			json_t *auth_entrypoint = NULL;
			json_t *auth_header_entrypoint = NULL;
			json_t *env_entrypoint = NULL;
			json_t *add_label_entrypoint = NULL;
			json_t *tls_certificate_entrypoint = NULL;
			json_t *tls_key_entrypoint = NULL;
			json_t *tls_ca_entrypoint = NULL;
			json_t *grok_quantiles = NULL;
			json_t *grok_le = NULL;
			json_t *grok_counter = NULL;
			json_t *grok_splited_quantiles = NULL;
			json_t *grok_splited_le = NULL;
			json_t *grok_splited_counter = NULL;
			json_t *grok_splited_tags = NULL;
			json_t *grok_splited_inherit_tag = NULL;

			json_t *context_json = NULL;
			context_json = json_object_get(root, wstokens[i].token->s);
			if (!context_json)
			{
				if (!strcmp(wstokens[i].token->s, "aggregate") || !strcmp(wstokens[i].token->s, "x509") || !strcmp(wstokens[i].token->s, "mtail") || !strcmp(wstokens[i].token->s, "entrypoint") || !strcmp(wstokens[i].token->s, "query") || !strcmp(wstokens[i].token->s, "grok") || !strcmp(wstokens[i].token->s, "action") || !strcmp(wstokens[i].token->s, "probe") || !strcmp(wstokens[i].token->s, "lang") || !strcmp(wstokens[i].token->s, "cluster") || !strcmp(wstokens[i].token->s, "instance") || !strcmp(wstokens[i].token->s, "resolver") || !strcmp(wstokens[i].token->s, "scheduler") || !strcmp(wstokens[i].token->s, "threaded_loop") || !strcmp(wstokens[i].token->s, "tls_certificate") || !strcmp(wstokens[i].token->s, "tls_key") || !strcmp(wstokens[i].token->s, "tls_ca") || !strcmp(wstokens[i].token->s, "namespace") || !strcmp(wstokens[i].token->s, "log_channel"))
					context_json = json_array();
				else
					context_json = json_object();
				json_object_set_new(root, wstokens[i].token->s, context_json);
			}

			json_t *operator_json = NULL;
			if (!strcmp(context_name, "entrypoint"))
			{
				operator_json = json_object();
				json_array_object_insert(context_json, operator_name, operator_json);
			}

			for (; i < token_count; i++)
			{
			if ((!strcmp(context_name, "puppeteer") || !strcmp(context_name, "chromecdp")) &&
			    wstokens[i].token->s && !strcmp(wstokens[i].token->s, "metricstransform"))
				{
					if (!operator_json)
					{
						operator_json = json_object();
						json_array_object_insert(context_json, "", operator_json);
					}
					if (!json_object_get(operator_json, "metricstransform"))
					{
						uint64_t j = i;
						json_t *mtx = plain_metricstransform_parse(wstokens, &j, token_count);
						if (mtx)
						{
							json_array_object_insert(operator_json, "metricstransform", mtx);
							i = j;
						}
					}
					continue;
				}

				if (wstokens[i].operator ||
			    ((!strcmp(context_name, "aggregate") ||
			      !strcmp(context_name, "action") ||
			      !strcmp(context_name, "puppeteer") ||
			      !strcmp(context_name, "chromecdp")) &&
			     wstokens[i].context &&
			     wstokens[i].token->s &&
			     strcmp(wstokens[i].token->s, context_name) &&
			     strcmp(wstokens[i].token->s, "metricstransform")))
				{
					operator_name = wstokens[i].token->s;
					if (!strcmp(context_name, "system") && (!strcmp(wstokens[i].token->s, "packages") || !strcmp(wstokens[i].token->s, "process") || !strcmp(wstokens[i].token->s, "services") || !strcmp(wstokens[i].token->s, "services_process") || !strcmp(wstokens[i].token->s, "services_checking_users") || !strcmp(wstokens[i].token->s, "pidfile") || !strcmp(wstokens[i].token->s, "userprocess") || !strcmp(wstokens[i].token->s, "groupprocess") || !strcmp(wstokens[i].token->s, "cgroup") || !strcmp(wstokens[i].token->s, "sysctl")))
					{
						operator_json = json_array();
						json_array_object_insert(context_json, operator_name, operator_json);
					}
					else if (!strcmp(context_name, "resolver"))
					{
						json_t *resolver_data = json_string(wstokens[i].token->s);
						json_array_object_insert(context_json, NULL, resolver_data);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(wstokens[i].token->s, "log_level"))
					{
						if (!log_level_entrypoint)
						{
							++i;
							log_level_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "log_level", log_level_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(wstokens[i].token->s, "log_channel"))
					{
						if (!log_channel_entrypoint)
						{
							++i;
							log_channel_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "log_channel", log_channel_entrypoint);
						}
					}
					else if (!strcmp(context_name, "persistence") || !strcmp(context_name, "modules") || !strcmp(wstokens[i].token->s, "sysfs") || !strcmp(wstokens[i].token->s, "procfs") || !strcmp(wstokens[i].token->s, "rundir") || !strcmp(wstokens[i].token->s, "usrdir") || !strcmp(wstokens[i].token->s, "etcdir"))
					{
						++i;
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(context_json, operator_name, arg_json);
					}
					else if (json_typeof(context_json) == JSON_OBJECT && (!strcmp(wstokens[i].token->s, "log_level") || !strcmp(wstokens[i].token->s, "log_channel")))
					{
						++i;
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(context_json, operator_name, arg_json);
					}
					else if (!strcmp(wstokens[i].token->s, "cadvisor") || !strcmp(wstokens[i].token->s, "cpuavg") || !strcmp(wstokens[i].token->s, "firewall"))
					{
						operator_json = json_object();

						char *object_context = wstokens[i].token->s;
						for (; i < token_count; i++)
						{
							json_t *arg_value = NULL;
							char arg_name[255];
							if (wstokens[i].operator)
							{
								strlcpy(arg_name, wstokens[i].token->s, 255);
							}

							else if (wstokens[i].argument)
							{
								uint64_t sep = strcspn(wstokens[i].token->s, "=");
								if (sep < wstokens[i].token->l)
								{
									strlcpy(arg_name, wstokens[i].token->s, sep+1);
									if (!strcmp(arg_name, "add_label") && !strcmp(object_context, "cadvisor")) {
										json_t *add_label_obj = json_object();
										json_array_object_insert(operator_json, "add_label", add_label_obj);

										uint64_t semisep = strcspn(wstokens[i].token->s+sep+1, ":") + sep;
										char kv_key[255];
										strlcpy(kv_key, wstokens[i].token->s+sep+1, semisep-sep+1);

										json_t *kv_value = json_string(wstokens[i].token->s+semisep+2);
										arg_value = json_object();
										json_array_object_insert(add_label_obj, kv_key, kv_value);
									}
									//if (!strcmp(arg_name, "pquery")) {
									//	json_t *pquery_array = json_array();
									//	json_array_object_insert(operator_json, "pquery", pquery_array);

									//	json_t *kv_value = json_string(wstokens[i].token->s);
									//	json_array_object_insert(pquery_array, "", kv_value);
									//}
									else {
										arg_value = json_integer_string_set(wstokens[i].token->s+sep+1);

										json_array_object_insert(operator_json, arg_name, arg_value);
									}
								}
							}

							if (wstokens[i].semicolon)
							{
								break;
							}
						}


						json_array_object_insert(context_json, operator_name, operator_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "allow"))
					{
						if (!allow_entrypoint)
						{
							allow_entrypoint = json_array();
							json_array_object_insert(operator_json, "allow", allow_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "deny"))
					{
						if (!deny_entrypoint)
						{
							deny_entrypoint = json_array();
							json_array_object_insert(operator_json, "deny", deny_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && (!strcmp(operator_name, "env") || !strcmp(operator_name, "header")))
					{
						if (!env_entrypoint)
						{
							env_entrypoint = json_object();
							json_array_object_insert(operator_json, "env", env_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "add_label"))
					{
						if (!add_label_entrypoint)
						{
							add_label_entrypoint = json_object();
							json_array_object_insert(operator_json, "add_label", add_label_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "metricstransform"))
					{
						if (!json_object_get(operator_json, "metricstransform"))
						{
							uint64_t j = i;
							json_t *mtx = plain_metricstransform_parse(wstokens, &j, token_count);
							if (mtx)
							{
								json_array_object_insert(operator_json, "metricstransform", mtx);
								i = j;
							}
						}
					}
					else if ((!strcmp(context_name, "puppeteer") || !strcmp(context_name, "chromecdp")) && !strcmp(operator_name, "metricstransform"))
					{
						if (!json_object_get(operator_json, "metricstransform"))
						{
							uint64_t j = i;
							json_t *mtx = plain_metricstransform_parse(wstokens, &j, token_count);
							if (mtx)
							{
								json_array_object_insert(operator_json, "metricstransform", mtx);
								i = j;
							}
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "tcp"))
					{
						if (!tcp_entrypoint)
						{
							tcp_entrypoint = json_array();
							json_array_object_insert(operator_json, "tcp", tcp_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "udp"))
					{
						if (!udp_entrypoint)
						{
							udp_entrypoint = json_array();
							json_array_object_insert(operator_json, "udp", udp_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "tls"))
					{
						if (!tls_entrypoint)
						{
							tls_entrypoint = json_array();
							json_array_object_insert(operator_json, "tls", tls_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "unix"))
					{
						if (!unix_entrypoint)
						{
							unix_entrypoint = json_array();
							json_array_object_insert(operator_json, "unix", unix_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "unixgram"))
					{
						if (!unixgram_entrypoint)
						{
							unixgram_entrypoint = json_array();
							json_array_object_insert(operator_json, "unixgram", unixgram_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "handler"))
					{
						if (!handler_entrypoint)
						{
							++i;
							handler_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "handler", handler_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "mapping"))
					{
						if (!mapping_entrypoint)
						{
							mapping_entrypoint = json_array();
							json_array_object_insert(operator_json, "mapping", mapping_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "api"))
					{
						if (!api_entrypoint)
						{
							++i;
							api_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "api", api_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "ttl"))
					{
						if (!ttl_entrypoint)
						{
							++i;
							ttl_entrypoint = json_integer(strtoll(wstokens[i].token->s, NULL, 10));
							json_array_object_insert(operator_json, "ttl", ttl_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "mtail_full_export_interval"))
					{
						if (!mtail_full_export_interval_entrypoint)
						{
							++i;
							mtail_full_export_interval_entrypoint = json_integer(strtoll(wstokens[i].token->s, NULL, 10));
							json_array_object_insert(operator_json, "mtail_full_export_interval", mtail_full_export_interval_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && (!strcmp(operator_name, "read_metric_interval")))
					{
						if (!read_metric_interval_entrypoint)
						{
							++i;
							read_metric_interval_entrypoint = json_integer(strtoll(wstokens[i].token->s, NULL, 10));
							json_array_object_insert(operator_json, "read_metric_interval", read_metric_interval_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "cluster"))
					{
						if (!cluster_entrypoint)
						{
							++i;
							cluster_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "cluster", cluster_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "instance"))
					{
						if (!instance_entrypoint)
						{
							++i;
							instance_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "instance", instance_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "threads"))
					{
						if (!threads_entrypoint)
						{
							++i;
							uint64_t inttoken = strtoull(wstokens[i].token->s, NULL, 10);
							threads_entrypoint = json_integer(inttoken);
							json_array_object_insert(operator_json, "threads", threads_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "metric_aggregation"))
					{
						if (!metric_aggregation_entrypoint)
						{
							++i;
							metric_aggregation_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "metric_aggregation", metric_aggregation_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "key"))
					{
						if (!key_entrypoint)
						{
							++i;
							key_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "key", key_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "tls_certificate"))
					{
						if (!tls_certificate_entrypoint)
						{
							++i;
							tls_certificate_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "tls_certificate", tls_certificate_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "tls_key"))
					{
						if (!tls_key_entrypoint)
						{
							++i;
							tls_key_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "tls_key", tls_key_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "tls_ca"))
					{
						if (!tls_ca_entrypoint)
						{
							++i;
							tls_ca_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "tls_ca", tls_ca_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "lang"))
					{
						if (!lang_entrypoint)
						{
							++i;
							lang_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "lang", lang_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "return"))
					{
						if (!return_entrypoint)
						{
							++i;
							return_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "return", return_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "grok"))
					{
						if (!grok_entrypoint)
						{
							++i;
							grok_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "grok", grok_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "mtail"))
					{
						if (!mtail_entrypoint)
						{
							++i;
							mtail_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "mtail", mtail_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "namespace"))
					{
						if (!namespace_entrypoint)
						{
							++i;
							namespace_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "namespace", namespace_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "auth"))
					{
						if (!auth_entrypoint)
						{
							auth_entrypoint = json_array();
							json_array_object_insert(operator_json, "auth", auth_entrypoint);
						}

						++i;
						json_t *type_auth = json_string(wstokens[i].token->s);

						++i;
						json_t *data_auth = json_string(wstokens[i].token->s);

						json_t *instance_auth = json_object();
						json_array_object_insert(auth_entrypoint, NULL, instance_auth);
						json_array_object_insert(instance_auth, "type", type_auth);
						json_array_object_insert(instance_auth, "data", data_auth);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "auth_header"))
					{
						if (!auth_header_entrypoint)
						{
							++i;
							auth_header_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "auth_header", auth_header_entrypoint);
						}
					}
					else if (!strcmp(context_name, "x509") || !strcmp(context_name, "mtail") || !strcmp(context_name, "query") || !strcmp(context_name, "action") || !strcmp(context_name, "probe") || !strcmp(context_name, "lang") || !strcmp(context_name, "cluster") || !strcmp(context_name, "scheduler") || !strcmp(context_name, "threaded_loop") || !strcmp(context_name, "namespace") || !strcmp(context_name, "log_channel"))
					{
						operator_json = json_object();
						char arg_name[255];
						char operator_name[255];

						for (; i < token_count; i++)
						{
							json_t *arg_value = NULL;
							if (!strcmp(context_name, "action") &&
							    wstokens[i].token->s && !strcmp(wstokens[i].token->s, "metricstransform"))
							{
								if (!json_object_get(operator_json, "metricstransform"))
								{
									uint64_t j = i;
									json_t *mtx = plain_metricstransform_parse(wstokens, &j, token_count);
									if (mtx)
									{
										json_array_object_insert(operator_json, "metricstransform", mtx);
										i = j;
									}
								}
								continue;
							}
							if (wstokens[i].operator ||
							    (!strcmp(context_name, "action") &&
							     wstokens[i].context && wstokens[i].token->s &&
							     strcmp(wstokens[i].token->s, context_name)))
							{
								strlcpy(operator_name, wstokens[i].token->s, 255);

								if (!strcmp(context_name, "action") && !strcmp(operator_name, "metricstransform"))
								{
									if (!json_object_get(operator_json, "metricstransform"))
									{
										uint64_t j = i;
										json_t *mtx = plain_metricstransform_parse(wstokens, &j, token_count);
										if (mtx)
										{
											json_array_object_insert(operator_json, "metricstransform", mtx);
											i = j;
										}
									}
									continue;
								}

								if (!strcmp(context_name, "action") && !strcmp(operator_name, "metric_name_transform_pattern"))
								{
									if (!json_object_get(operator_json, "metric_name_transform_pattern"))
									{
										++i;
										if (i < token_count && wstokens[i].argument)
											json_array_object_insert(operator_json, "metric_name_transform_pattern", json_string(wstokens[i].token->s));
									}
									continue;
								}
								if (!strcmp(context_name, "action") && !strcmp(operator_name, "metric_name_transform_replacement"))
								{
									if (!json_object_get(operator_json, "metric_name_transform_replacement"))
									{
										++i;
										if (i < token_count && wstokens[i].argument)
											json_array_object_insert(operator_json, "metric_name_transform_replacement", json_string(wstokens[i].token->s));
									}
									continue;
								}

								if (!strcmp(operator_name, "field") || !strcmp(operator_name, "jpath") || !strcmp(operator_name, "valid_status_codes") || !strcmp(operator_name, "servers") || !strcmp(operator_name, "sharding_key") || !strcmp(operator_name, "match"))
								{
									json_t *arg_json = json_object_get(operator_json, operator_name);
									if (!arg_json)
									{
										arg_json = json_array();
										json_array_object_insert(operator_json, operator_name, arg_json);
									}
									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											json_t *str_json = json_string(wstokens[i].token->s);
											json_array_object_insert(arg_json, operator_name, str_json);
										}
										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								}
								else if (!strcmp(operator_name, "dry_run"))
								{
									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											if (!strcmp(wstokens[i].token->s, "true")) {
												json_array_object_insert(operator_json, operator_name, json_true());
											}
											else {
												json_array_object_insert(operator_json, operator_name, json_false());
											}
										}
										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								}
								else if (!strcmp(operator_name, "env") || !strcmp(operator_name, "header"))
								{
									json_t *env_obj = json_object_get(operator_json, "env");
									if (!env_obj)
									{
										env_obj = json_object();
										json_array_object_insert(operator_json, "env", env_obj);
									}
									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											plain_context_env_line(env_obj, wstokens[i].token);
											break;
										}
										if (wstokens[i].semicolon)
											break;
									}
								}
								else if (!strcmp(operator_name, "add_label"))
								{
									json_t *add_label_obj = json_object_get(operator_json, "add_label");
									if (!add_label_obj)
									{
										add_label_obj = json_object();
										json_array_object_insert(operator_json, "add_label", add_label_obj);
									}
									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											plain_context_env_line(add_label_obj, wstokens[i].token);
											break;
										}
										if (wstokens[i].semicolon)
											break;
									}
								}
							}
							else if (wstokens[i].argument)
							{
								strlcpy(arg_name, wstokens[i].token->s, 255);
								arg_value = json_string(wstokens[i].token->s);
								json_array_object_insert(operator_json, operator_name, arg_value);
							}

							if (wstokens[i].end)
							{
								break;
							}
						}
						json_array_object_insert(context_json, operator_name, operator_json);
					}
					else if (!strcmp(context_name, "grok"))
					{
						operator_json = json_object();
						char arg_name[255];
						char operator_name[255];

						for (; i < token_count; i++)
						{
							json_t *arg_value = NULL;
							if (wstokens[i].operator)
							{
								strlcpy(operator_name, wstokens[i].token->s, 255);
							}
							else if (wstokens[i].argument)
							{
								if (!strcmp(operator_name, "quantiles"))
								{
									json_t *quantiles_data = json_array();
									if (!grok_quantiles)
									{
										grok_quantiles = json_array();
										json_array_object_insert(operator_json, "quantiles", grok_quantiles);
									}

									json_array_object_insert(grok_quantiles, operator_name, quantiles_data);

									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											json_t *arg_value = json_string(wstokens[i].token->s);
											json_array_object_insert(quantiles_data, NULL, arg_value);
										}

										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								}
								else if (!strcmp(operator_name, "bucket"))
								{
									json_t *le_data = json_array();
									if (!grok_le)
									{
										grok_le = json_array();
										json_array_object_insert(operator_json, "bucket", grok_le);
									}

									json_array_object_insert(grok_le, operator_name, le_data);

									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											json_t *arg_value = json_string(wstokens[i].token->s);
											json_array_object_insert(le_data, NULL, arg_value);
										}

										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								}
								else if (!strcmp(operator_name, "counter"))
								{
									json_t *counter_data = json_array();
									if (!grok_counter)
									{
										grok_counter = json_array();
										json_array_object_insert(operator_json, "counter", grok_counter);
									}

									json_array_object_insert(grok_counter, operator_name, counter_data);

									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											json_t *arg_value = json_string(wstokens[i].token->s);
											json_array_object_insert(counter_data, NULL, arg_value);
										}

										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								}
								else if (!strcmp(operator_name, "splited_quantiles"))
								{
									json_t *splited_quantiles_data = json_array();
									if (!grok_splited_quantiles)
									{
										grok_splited_quantiles = json_array();
										json_array_object_insert(operator_json, "splited_quantiles", grok_splited_quantiles);
									}

									json_array_object_insert(grok_splited_quantiles, operator_name, splited_quantiles_data);

									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											json_t *arg_value = json_string(wstokens[i].token->s);
											json_array_object_insert(splited_quantiles_data, NULL, arg_value);
										}

										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								}
								else if (!strcmp(operator_name, "splited_bucket"))
								{
									json_t *splited_le_data = json_array();
									if (!grok_splited_le)
									{
										grok_splited_le = json_array();
										json_array_object_insert(operator_json, "splited_bucket", grok_splited_le);
									}

									json_array_object_insert(grok_splited_le, operator_name, splited_le_data);

									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											json_t *arg_value = json_string(wstokens[i].token->s);
											json_array_object_insert(splited_le_data, NULL, arg_value);
										}

										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								}
								else if (!strcmp(operator_name, "splited_counter"))
								{
									json_t *splited_counter_data = json_array();
									if (!grok_splited_counter)
									{
										grok_splited_counter = json_array();
										json_array_object_insert(operator_json, "splited_counter", grok_splited_counter);
									}

									json_array_object_insert(grok_splited_counter, operator_name, splited_counter_data);

									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											json_t *arg_value = json_string(wstokens[i].token->s);
											json_array_object_insert(splited_counter_data, NULL, arg_value);
										}

										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								}
								else if (!strcmp(operator_name, "splited_tags"))
								{
									json_t *splited_tags_data = json_array();
									if (!grok_splited_tags)
									{
										grok_splited_tags = json_array();
										json_array_object_insert(operator_json, "splited_tags", grok_splited_tags);
									}

									json_array_object_insert(grok_splited_tags, operator_name, splited_tags_data);

									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											json_t *arg_value = json_string(wstokens[i].token->s);
											json_array_object_insert(splited_tags_data, NULL, arg_value);
										}

										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								}
								else if (!strcmp(operator_name, "splited_inherit_tag"))
								{
									json_t *splited_inherit_tag_data = json_array();
									if (!grok_splited_inherit_tag)
									{
										grok_splited_inherit_tag = json_array();
										json_array_object_insert(operator_json, "splited_inherit_tag", grok_splited_inherit_tag);
									}

									json_array_object_insert(grok_splited_inherit_tag, operator_name, splited_inherit_tag_data);

									for (; i < token_count; i++)
									{
										if (wstokens[i].argument)
										{
											json_t *arg_value = json_string(wstokens[i].token->s);
											json_array_object_insert(splited_inherit_tag_data, NULL, arg_value);
										}

										if (wstokens[i].semicolon)
										{
											break;
										}
									}
								} else {
									strlcpy(arg_name, wstokens[i].token->s, 255);
									arg_value = json_string(wstokens[i].token->s);
									json_array_object_insert(operator_json, operator_name, arg_value);
								}
							}

							if (wstokens[i].end)
							{
								break;
							}
						}
						json_array_object_insert(context_json, operator_name, operator_json);
					}
					else if (!strcmp(context_name, "aggregate"))
					{
						operator_json = json_object();
						json_t *env_obj = json_object();
						json_t *add_label_obj = json_object();
						json_t *pquery_arr = json_array();
						json_array_object_insert(operator_json, "env", env_obj);
						json_array_object_insert(operator_json, "add_label", add_label_obj);
						json_array_object_insert(operator_json, "pquery", pquery_arr);

						json_t *handler = json_string(wstokens[i].token->s);
						json_array_object_insert(operator_json, "handler", handler);

						++i;
						json_t *url= json_string(wstokens[i].token->s);
						json_array_object_insert(operator_json, "url", url);

						for (; i < token_count; i++)
						{
							char *opt_tok = wstokens[i].token->s;
							if (plain_metricstransform_has_native_block(wstokens, i, token_count))
							{
								if (!json_object_get(operator_json, "metricstransform"))
								{
									uint64_t j = i;
									json_t *mtx = plain_metricstransform_parse_native_block(wstokens, &j, token_count);
									if (mtx)
									{
										json_array_object_insert(operator_json, "metricstransform", mtx);
										i = j;
									}
								}
								continue;
							}
							uint64_t sep = strcspn(opt_tok, "=");
							size_t toklen = opt_tok ? strlen(opt_tok) : 0;
							if (sep < toklen && opt_tok[sep] == '=')
							{
								glog(L_TRACE, "aggregate option token='%s'\n", opt_tok);
								char arg_name[255];
								strlcpy(arg_name, opt_tok, sep+1);
								glog(L_TRACE, "aggregate option key='%s'\n", arg_name);

								uint64_t semisep = strcspn(opt_tok+sep+1, ":") + sep;
								if (!strcmp(arg_name, "instance") || !strcmp(arg_name, "bind_address"))
									semisep = toklen;
								if (!strcmp(arg_name, "metricstransform"))
									semisep = toklen;

								json_t *arg_value = NULL;
								if (!strcmp(arg_name, "metricstransform") && opt_tok[sep + 1] == '\0' && i + 1 < token_count)
								{
									arg_value = plain_parse_metricstransform_object(wstokens[i + 1].token->s);
									if (!arg_value)
										arg_value = plain_json_or_string(wstokens[i + 1].token->s);
									if (arg_value)
										json_array_object_insert(operator_json, arg_name, arg_value);
									i++;
								}
								else if (semisep+1 < toklen)
								{
									char kv_key[255];
									strlcpy(kv_key, opt_tok+sep+1, semisep-sep+1);
									glog(L_TRACE, "aggregate key/value key='%s'\n", kv_key);

									if (!strcmp(arg_name, "env"))
									{
										arg_value = env_obj;
									}
									else if (!strcmp(arg_name, "header"))
									{
										arg_value = env_obj;
									}
									else if (!strcmp(arg_name, "add_label"))
										arg_value = add_label_obj;
									else
										arg_value = json_object();
									json_t *kv_value = json_string(opt_tok+semisep+2);
									json_array_object_insert(arg_value, kv_key, kv_value);
								}
								else
								{
									if (sisdigit(opt_tok+sep+1))
									{
										int64_t num = strtoll(opt_tok+sep+1, NULL, 10);
										arg_value = json_integer(num);
									}
									else
									{
										if (!strcmp(arg_name, "metricstransform"))
										{
											arg_value = plain_parse_metricstransform_object(opt_tok+sep+1);
											if (!arg_value)
												arg_value = plain_json_or_string(opt_tok+sep+1);
										}
										else
											arg_value = json_string(opt_tok+sep+1);
									}

									if (!strcmp(arg_name, "pquery")) {
										json_array_object_insert(pquery_arr, "", arg_value);
									}
									else
										json_array_object_insert(operator_json, arg_name, arg_value);
								}
							}
							if (wstokens[i].semicolon)
							{
								break;
							}
						}
						json_array_object_insert(context_json, operator_name, operator_json);
					}
					else if ((!strcmp(context_name, "puppeteer") || !strcmp(context_name, "chromecdp")) && operator_json && strchr(wstokens[i].token->s, '=') && plain_puppeteer_insert_option(operator_json, wstokens, &i, token_count))
						continue;
					else
					{
						if (!strcmp(context_name, "entrypoint") && operator_json)
							;
						else
						{
							operator_json = json_object();
							json_array_object_insert(context_json, operator_name, operator_json);
						}
					}
				}
				else if (wstokens[i].argument)
				{
					if (!operator_json || !operator_name)
						continue;

					if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "log_level"))
					{
						if (!log_level_entrypoint)
						{
							log_level_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "log_level", log_level_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "log_channel"))
					{
						if (!log_channel_entrypoint)
						{
							log_channel_entrypoint = json_string(wstokens[i].token->s);
							json_array_object_insert(operator_json, "log_channel", log_channel_entrypoint);
						}
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "allow"))
					{
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(allow_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && (!strcmp(operator_name, "env") || !strcmp(operator_name, "header")))
					{
						json_t *value_json = json_string(wstokens[++i].token->s);
						json_array_object_insert(env_entrypoint, wstokens[i-1].token->s, value_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "add_label"))
					{
						plain_context_env_line(add_label_entrypoint, wstokens[i].token);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "deny"))
					{
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(deny_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "tcp"))
					{
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(tcp_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "udp"))
					{
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(udp_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "tls"))
					{
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(tls_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "unix"))
					{
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(unix_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "unixgram"))
					{
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(unixgram_entrypoint, operator_name, arg_json);
					}
					//else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "handler"))
					//{
					//	json_t *arg_json = json_string(wstokens[i].token->s);
					//	json_array_object_insert(handler_entrypoint, operator_name, arg_json);
					//}
					else if (!strcmp(operator_name, "ttl"))
					{
						json_t *arg_json = json_integer(strtoll(wstokens[i].token->s, NULL, 10));
						json_array_object_insert(operator_json, operator_name, arg_json);
					}
				else if ((!strcmp(context_name, "puppeteer") || !strcmp(context_name, "chromecdp")) && plain_puppeteer_insert_option(operator_json, wstokens, &i, token_count))
				{
					/* parsed key=value inline option for puppeteer/chromecdp */
				}
					else
					{
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(operator_json, operator_name, arg_json);
					}
				}
				else if (wstokens[i].context)
				{
					if (!strcmp(context_name, "entrypoint") && !strcmp(wstokens[i].token->s, "mapping"))
					{
						if (!mapping_entrypoint)
						{
							mapping_entrypoint = json_array();
							json_array_object_insert(operator_json, "mapping", mapping_entrypoint);
						}

						json_t *mapping_object = json_object();
						json_array_object_insert(mapping_entrypoint, "", mapping_object);

						char *mapping_name = NULL;
						json_t *label_json = NULL;
						for (; i < token_count; i++)
						{
							if (wstokens[i].token->l)
							{
								if (wstokens[i].operator)
								{
									mapping_name = wstokens[i].token->s;
								}
								else if (wstokens[i].argument)
								{
									json_t *arg_json = NULL;
									if (!strcmp(mapping_name, "le") || !strcmp(mapping_name, "buckets") || !strcmp(mapping_name, "quantiles"))
									{
										glog(L_TRACE, "mapping aggregate field='%s'\n", mapping_name);
										arg_json = json_array();

										for (; i < token_count; i++)
										{
											glog(L_TRACE, "mapping quantile/bucket value='%s'\n", wstokens[i].token->s);
											if (wstokens[i].token->l)
											{
												json_t *str_json = json_real(strtod(wstokens[i].token->s, NULL));
												json_array_object_insert(arg_json, "", str_json);
											}
											if (wstokens[i].end || wstokens[i].semicolon)
											{
												break;
											}
										}
									}
									else if (!strcmp(mapping_name, "label"))
									{
										if (!label_json)
											label_json = json_object();

										char *label_name = wstokens[i].token->s;
										++i;
										json_t *label_value = json_string(wstokens[i].token->s);
										json_array_object_insert(label_json, label_name, label_value);
									}
									else
									{
										arg_json = json_string(wstokens[i].token->s);
									}

									if (arg_json)
										json_array_object_insert(mapping_object, mapping_name, arg_json);
									//printf("mapping arg %s=%s to %p\n========\n%s\n=========\n", mapping_name, wstokens[i].token->s, mapping_object, json_dumps(mapping_object, 0));
								}
							}
							if (wstokens[i].end)
							{
								if (label_json)
									json_array_object_insert(mapping_object, "label", label_json);
								wstokens[i].end = 0;
								break;
							}
						}
					}
				else if ((!strcmp(context_name, "entrypoint") || !strcmp(context_name, "puppeteer") || !strcmp(context_name, "chromecdp")) && plain_metricstransform_has_native_block(wstokens, i, token_count))
				{
					if (!operator_json && (!strcmp(context_name, "puppeteer") || !strcmp(context_name, "chromecdp")))
						{
							operator_json = json_object();
							json_array_object_insert(context_json, "", operator_json);
						}
						if (operator_json && !json_object_get(operator_json, "metricstransform"))
						{
							uint64_t j = i;
							json_t *mtx = plain_metricstransform_parse(wstokens, &j, token_count);
							if (mtx)
							{
								json_array_object_insert(operator_json, "metricstransform", mtx);
								i = j;
							}
						}
					}

				}
				if (wstokens[i].end)
				{
					break;
				}
			}
		}
	}
	char *ret = json_dumps(root, JSON_INDENT(2));
	json_decref(root);
	return ret;
}

void config_parser_stat_free(config_parser_stat *wstokens, uint64_t token_count)
{
	for (uint64_t i = 0; i < token_count; ++i)
		string_free(wstokens[i].token);
	free(wstokens);
}

char* config_plain_to_json(string *context)
{
	string *clean_context = plain_strip_comments(context);
	if (!clean_context)
		return strdup("{}");

	uint64_t token_count = plain_count_get(clean_context);
	config_parser_stat *wstokens = string_tokenizer(clean_context, &token_count);
	char *ret = build_json_from_tokens(wstokens, token_count);
	config_parser_stat_free(wstokens, token_count);
	string_free(clean_context);
	return ret;
}
