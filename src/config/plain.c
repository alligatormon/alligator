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

	if (ac->log_level > 2)
		printf("2 %d:%d:%d:%d:%d:%d:%d:%d\n", ret->operator, ret->argument, ret->context, ret->start, ret->semicolon,ret->quotas1, ret->quotas2, ret->end);

	if (!ret->operator && !ret->argument && !ret->context && !ret->start && !ret->semicolon && !ret->quotas1 && !ret->quotas2 && !ret->end)
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

		if ((sq1 < sm) && (sq1 < sm))
		{
			if (ac->log_level > 3)
				printf("quotas1 parser1 selected\n");

			sq1 += strcspn(str+sq1+1, "'");
			sm = strcspn(str+sq1, ";");
			st = strcspn(str+sq1, "{");
		}
		if ((sq2 < sm) && (sq2 < st))
		{
			if (ac->log_level > 3)
				printf("quotas2 parser1 selected\n");

			sq2 += strcspn(str+sq2+1, "'");
			sm = strcspn(str+sq2, ";");
			st = strcspn(str+sq2, "{");
		}

		if (st < sm) {
			if (ac->log_level > 3)
				printf("config parser plain CTX: %"u64" < %"u64"\n", st, sm);

			ret->context = 1;
			ret->operator = 0;
		}
		else
		{
			if (ac->log_level > 3)
				printf("config parser plain OP: %"u64" < %"u64"\n", st, sm);
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

	strlcpy(tmp, str, ret->fact_length+1);

	if (ac->log_level > 3)
		printf("ret->length: %"u64", ret->fact_length: %"u64"\n", ret->length, ret->fact_length);

	for (uint64_t trigger = 0; trigger < ret->fact_length; trigger++)
	{
		trigger += strcspn(tmp+trigger, "'\"{};");
		if (ac->log_level > 3)
			printf("%"u64"/%"u64": {%c} '%s'\n", trigger, ret->length, tmp[trigger], tmp);

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
		if (ac->log_level > 3)
			puts("=======================");

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

		if (ac->log_level > 0)
			printf("'%s', (%zu) '=%d, \"=%d ;=%d start=%d end=%d op=%d arg=%d ctx=%d\n", elem->s, elem->l, retws[i].quotas1, retws[i].quotas2, retws[i].semicolon, retws[i].start, retws[i].end, retws[i].operator, retws[i].argument, retws[i].context);

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

void json_array_object_insert(json_t *dst_json, char *key, json_t *src_json)
{
	int json_type = json_typeof(dst_json);
	if (json_type == JSON_ARRAY)
		json_array_append(dst_json, src_json);
	else if (json_type == JSON_OBJECT)
		json_object_set_new(dst_json, key, src_json);
}

json_t *json_integer_string_set(char *str)
{
	json_t *value;
	if (isdigit(*str))
	{
		int64_t num = strtoll(str, NULL, 10);
		value = json_integer(num);
	}
	else
		value = json_string(strdup(str));

	return value;
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
				if (isdigit(*wstokens[i].token->s))
				{
					int64_t num = strtoll(wstokens[i].token->s, NULL, 10);
					json_t *arg_json = json_integer(num);
					json_object_set_new(root, operator_name, arg_json);
				}
				else
				{
					json_t *arg_json = json_string(strdup(wstokens[i].token->s));
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

			if (ac->log_level > 0)
				printf("context: '%s'\n", wstokens[i].token->s);

			json_t *context_json = NULL;
			context_json = json_object_get(root, wstokens[i].token->s);
			if (!context_json)
			{
				if (!strcmp(wstokens[i].token->s, "aggregate") || !strcmp(wstokens[i].token->s, "x509") || !strcmp(wstokens[i].token->s, "entrypoint"))
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
				if (wstokens[i].operator)
				{
					if (ac->log_level > 0)
						printf("\tcontext_name: %s, operator: '%s'\n", context_name, wstokens[i].token->s);

					operator_name = wstokens[i].token->s;
					if (!strcmp(wstokens[i].token->s, "packages"))
					{
						operator_json = json_array();
						json_array_object_insert(context_json, operator_name, operator_json);
					}
					else if (!strcmp(context_name, "persistence") || !strcmp(context_name, "modules"))
					{
						++i;
						json_t *arg_json = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(context_json, operator_name, arg_json);
					}
					else if (!strcmp(wstokens[i].token->s, "process"))
					{
						operator_json = json_array();
						json_array_object_insert(context_json, operator_name, operator_json);
					}
					else if (!strcmp(wstokens[i].token->s, "cadvisor") || !strcmp(wstokens[i].token->s, "cpuavg"))
					{
						operator_json = json_object();

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
									arg_value = json_integer_string_set(wstokens[i].token->s+sep+1);

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
					else if (!strcmp(context_name, "x509"))
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
								strlcpy(arg_name, wstokens[i].token->s, 255);
								arg_value = json_string(strdup(wstokens[i].token->s));
								json_array_object_insert(operator_json, operator_name, arg_value);
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

						json_t *handler = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(operator_json, "handler", handler);

						++i;
						json_t *url= json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(operator_json, "url", url);

						for (; i < token_count; i++)
						{
							uint64_t sep = strcspn(wstokens[i].token->s, "=");
							if (sep < wstokens[i].token->l)
							{
								char arg_name[255];
								strlcpy(arg_name, wstokens[i].token->s, sep+1);

								uint64_t semisep = strcspn(wstokens[i].token->s+sep+1, ":") + sep;
								json_t *arg_value = NULL;
								if (semisep+1 < wstokens[i].token->l)
								{
									char kv_key[255];
									strlcpy(kv_key, wstokens[i].token->s+sep+1, semisep-sep+1);

									arg_value = json_object();
									json_t *kv_value = json_string(strdup(wstokens[i].token->s+semisep+2));
									json_array_object_insert(arg_value, kv_key, kv_value);
								}
								else
								{
									if (isdigit(wstokens[i].token->s[sep+1]))
									{
										int64_t num = strtoll(wstokens[i].token->s+sep+1, NULL, 10);
										arg_value = json_integer(num);
									}
									else
										arg_value = json_string(strdup(wstokens[i].token->s+sep+1));
								}

								json_array_object_insert(operator_json, arg_name, arg_value);
							}
							if (wstokens[i].semicolon)
							{
								break;
							}
						}
						json_array_object_insert(context_json, operator_name, operator_json);
					}
					else
					{
						operator_json = json_object();
						json_array_object_insert(context_json, operator_name, operator_json);
					}
				}
				else if (wstokens[i].argument)
				{
					if (!operator_json || !operator_name)
						continue;

					if (ac->log_level > 0)
						printf("\t\tcontext_name: %s, operator: '%s', argument: '%s'\n", context_name, operator_name, wstokens[i].token->s);

					if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "allow"))
					{
						json_t *arg_json = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(allow_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "deny"))
					{
						json_t *arg_json = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(deny_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "tcp"))
					{
						json_t *arg_json = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(tcp_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "udp"))
					{
						json_t *arg_json = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(udp_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "tls"))
					{
						json_t *arg_json = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(tls_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "unix"))
					{
						json_t *arg_json = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(unix_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && !strcmp(operator_name, "unixgram"))
					{
						json_t *arg_json = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(unixgram_entrypoint, operator_name, arg_json);
					}
					else
					{
						json_t *arg_json = json_string(strdup(wstokens[i].token->s));
						json_array_object_insert(operator_json, operator_name, arg_json);
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
	return ret;
}

char* config_plain_to_json(string *context)
{
	uint64_t token_count = plain_count_get(context);
	config_parser_stat *wstokens = string_tokenizer(context, &token_count);
	return build_json_from_tokens(wstokens, token_count);
}
