#include <string.h>
#include "config/plain.h"
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

config_parser_stat plain_get_word(char *str, config_parser_stat pre)
{
	config_parser_stat ret = pre;
	char tmp[1000];

	ret.length = strcspn(str, " \r\t\n;\"'{}");
	ret.fact_length = strcspn(str, " \r\t\n");

	ret.quotas1 = 0;
	ret.quotas2 = 0;

	if (ret.semicolon || ret.start || ret.end)
	{
		ret.context = 0;
		ret.argument = 0;
		ret.operator = 0;
		ret.semicolon = 0;
		ret.start = 0;
		ret.end = 0;
	}

	if (ac->log_level > 2)
		printf("%d:%d:%d:%d:%d:%d:%d:%d\n", ret.operator, ret.argument, ret.context, ret.start, ret.semicolon,ret.quotas1, ret.quotas2, ret.end);

	if (!ret.operator && !ret.argument && !ret.context && !ret.start && !ret.semicolon && !ret.quotas1 && !ret.quotas2 && !ret.end)
	{
		uint64_t sq1 = strcspn(str, "'");
		uint64_t sq2 = strcspn(str, "\"");

		uint64_t sm = strcspn(str, ";");
		uint64_t st = strcspn(str, "{");

		if (ac->log_level > 3)
		{
			printf("OP CTX parser: %"u64" ({) < %"u64" (;)\n", st, sm);
			printf("quotas1 parser: %"u64" < %"u64" || %"u64" < %"u64"\n", sq1, sm, sq1, st);
			printf("quotas2 parser: %"u64" < %"u64" || %"u64" < %"u64"\n", sq2, sm, sq2, st);
		}

		if (sq1 < sm)
		{
			if (ac->log_level > 3)
				printf("quotas1 parser selected\n");

			sq1 += strcspn(str+sq1, "'");
			sm = strcspn(str+sq1, ";");
			st = strcspn(str+sq1, "{");
		}
		if (sq1 < st)
		{
			if (ac->log_level > 3)
				printf("quotas1 parser selected\n");

			sq1 += strcspn(str+sq1, "'");
			st = strcspn(str+sq1, "{");
		}
		if (sq2 < sm)
		{
			if (ac->log_level > 3)
				printf("quotas2 parser selected\n");

			sq2 += strcspn(str+sq2, "'");
			sm = strcspn(str+sq2, ";");
		}
		if (sq2 < st)
		{
			if (ac->log_level > 3)
				printf("quotas2 parser selected\n");

			st = strcspn(str+sq2, "{");
		}

		if (st < sm) {
			if (ac->log_level > 3)
				printf("config parser plain CTX: %"u64" < %"u64"\n", st, sm);

			ret.context = 1;
			ret.operator = 0;
		}
		else
		{
			if (ac->log_level > 3)
				printf("config parser plain OP: %"u64" < %"u64"\n", st, sm);
			ret.context = 0;
			ret.operator = 1;
		}

		//return ret;
	}
	else
	{

		ret.quotas1 = 0;
		ret.quotas2 = 0;
		ret.start = 0;
		ret.end = 0;

		if (ret.operator)
		{
			ret.operator = 0;
			ret.argument = 1;
		}

		if (ret.context)
		{
			ret.context = 0;
		}
	}

	strlcpy(tmp, str, ret.fact_length+1);

	if (ac->log_level > 3)
		printf("ret.length: %"u64", ret.fact_length: %"u64"\n", ret.length, ret.fact_length);

	for (uint64_t trigger = 0; trigger < ret.fact_length; trigger++)
	{
		trigger += strcspn(tmp+trigger, "'\"{};");
		printf("%d/%d: {%c} '%s'\n", trigger, ret.length, tmp[trigger], tmp);

		if (tmp[trigger] == '\'')
		{
			ret.quotas1 = 1;
			break;
		}
		if (tmp[trigger] == '"')
		{
			ret.quotas2 = 1;
			break;
		}
		if (tmp[trigger] == ';')
		{
			ret.semicolon = 1;
		}
		if (tmp[trigger] == '{')
		{
			ret.start = 1;
		}
		if (tmp[trigger] == '}')
		{
			ret.end = 1;
		}
	}

	tmp[ret.length] = 0;

	if (!ret.length)
	{
		ret.argument = 0;
		ret.context = 0;
		ret.operator = 0;
	}

	return ret;
}

string *plain_get_quotas_word(char *str, uint64_t len, config_parser_stat *wstat)
{
	uint64_t i;
	string *ret = string_init(255);
	char quotas_sym = 0;
	for (i = 0; i < len; i++)
	{
		if ((str[i-1] != '\\') && ((str[i] == '\'') || (str[i] == '"')))
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
				break;
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

	printf("replace fact length from %"u64" to %"u64"\n", wstat->fact_length, i);
	wstat->fact_length = i;

	return ret;
}

string** string_tokenizer(string *context, uint64_t *outlen, char *space)
{
	if (!context)
		return NULL;

	string **ret = calloc(10000, sizeof(**ret));
	char *tmp = context->s;
	config_parser_stat wstat = { 0 };

	uint64_t i;
	for (i = 0; (tmp - context->s) < context->l; i++)
	{
		//printf("ccc: %d < %d\n", (tmp - context->s), context->l);
		puts("=======================");
		char *start = plain_skip_spaces(tmp, space);
		if (!start)
			break;
		tmp = start;

		wstat = plain_get_word(start, wstat);

		string *elem = string_init(wstat.length);

		if (wstat.quotas1)
		{
			string *quotastring = plain_get_quotas_word(start, context->l, &wstat);
			//printf("quotastring1\n");
			string_merge(elem, quotastring);
		}
		else if (wstat.quotas2)
		{
			string *quotastring = plain_get_quotas_word(start, context->l, &wstat);
			//printf("quotastring2\n");
			string_merge(elem, quotastring);
		}
		else
			string_cat(elem, tmp, wstat.length);

		ret[i] = elem;
		printf("'%s', (%u) '=%d, \"=%d ;=%d start=%d end=%d op=%d arg=%d ctx=%d\n", elem->s, elem->l, wstat.quotas1, wstat.quotas2, wstat.semicolon, wstat.start, wstat.end, wstat.operator, wstat.argument, wstat.context);

		tmp = tmp + wstat.fact_length;
	}

	return ret;
}

char* config_plain_to_json(string *context)
{
	uint64_t tklen;
	string **tokens = string_tokenizer(context, &tklen, " \n\r\t");
	printf("tokens %p\n", tokens);
	return NULL;
}
