#include <string.h>
#include <stdlib.h>
#include "config/plain.h"
#include "common/json_query.h"
#include "common/selector.h"
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

		if ((sq1 < sm) && (sq1 < st))
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

			sq2 += strcspn(str+sq2+1, "\"");
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
			json_t *log_level_entrypoint = NULL;
			json_t *cluster_entrypoint = NULL;
			json_t *instance_entrypoint = NULL;
			json_t *threads_entrypoint = NULL;
			json_t *lang_entrypoint = NULL;
			json_t *metric_aggregation_entrypoint = NULL;
			json_t *key_entrypoint = NULL;
			json_t *return_entrypoint = NULL;
			json_t *grok_entrypoint = NULL;
			json_t *auth_entrypoint = NULL;
			json_t *auth_header_entrypoint = NULL;
			json_t *env_entrypoint = NULL;
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

			if (ac->log_level > 0)
				printf("context: '%s'\n", wstokens[i].token->s);

			json_t *context_json = NULL;
			context_json = json_object_get(root, wstokens[i].token->s);
			if (!context_json)
			{
				if (!strcmp(wstokens[i].token->s, "aggregate") || !strcmp(wstokens[i].token->s, "x509") || !strcmp(wstokens[i].token->s, "entrypoint") || !strcmp(wstokens[i].token->s, "query") || !strcmp(wstokens[i].token->s, "grok") || !strcmp(wstokens[i].token->s, "action") || !strcmp(wstokens[i].token->s, "probe") || !strcmp(wstokens[i].token->s, "lang") || !strcmp(wstokens[i].token->s, "cluster") || !strcmp(wstokens[i].token->s, "instance") || !strcmp(wstokens[i].token->s, "resolver") || !strcmp(wstokens[i].token->s, "scheduler") || !strcmp(wstokens[i].token->s, "threaded_loop") || !strcmp(wstokens[i].token->s, "tls_certificate") || !strcmp(wstokens[i].token->s, "tls_key") || !strcmp(wstokens[i].token->s, "tls_ca"))
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
					if (!strcmp(context_name, "system") && (!strcmp(wstokens[i].token->s, "packages") || !strcmp(wstokens[i].token->s, "process") || !strcmp(wstokens[i].token->s, "services") || !strcmp(wstokens[i].token->s, "pidfile") || !strcmp(wstokens[i].token->s, "userprocess") || !strcmp(wstokens[i].token->s, "groupprocess") || !strcmp(wstokens[i].token->s, "cgroup") || !strcmp(wstokens[i].token->s, "sysctl")))
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
					else if (!strcmp(context_name, "persistence") || !strcmp(context_name, "modules") || !strcmp(wstokens[i].token->s, "sysfs") || !strcmp(wstokens[i].token->s, "procfs") || !strcmp(wstokens[i].token->s, "rundir") || !strcmp(wstokens[i].token->s, "usrdir") || !strcmp(wstokens[i].token->s, "etcdir") || !strcmp(wstokens[i].token->s, "log_level"))
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
					else if (!strcmp(context_name, "x509") || !strcmp(context_name, "query") || !strcmp(context_name, "action") || !strcmp(context_name, "probe") || !strcmp(context_name, "lang") || !strcmp(context_name, "cluster") || !strcmp(context_name, "scheduler") || !strcmp(context_name, "threaded_loop"))
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

								if (!strcmp(operator_name, "field") || !strcmp(operator_name, "valid_status_codes") || !strcmp(operator_name, "servers") || !strcmp(operator_name, "sharding_key") || !strcmp(operator_name, "match"))
								{
									json_t *arg_json = json_array();
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
									json_array_object_insert(operator_json, operator_name, arg_json);
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
							uint64_t sep = strcspn(wstokens[i].token->s, "=");
							if (sep < wstokens[i].token->l)
							{
								if (ac->log_level > 1)
									printf("aggregate token '%s'\n", wstokens[i].token->s);
								char arg_name[255];
								strlcpy(arg_name, wstokens[i].token->s, sep+1);
								if (ac->log_level > 1)
									printf("\taggregate arg_name '%s'\n", arg_name);

								uint64_t semisep = strcspn(wstokens[i].token->s+sep+1, ":") + sep;
								if (!strcmp(arg_name, "instance"))
									semisep = wstokens[i].token->l;

								json_t *arg_value = NULL;
								if (semisep+1 < wstokens[i].token->l)
								{
									char kv_key[255];
									strlcpy(kv_key, wstokens[i].token->s+sep+1, semisep-sep+1);
									if (ac->log_level > 1)
										printf("\taggregate kv_key '%s'\n", kv_key);

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
									json_t *kv_value = json_string(wstokens[i].token->s+semisep+2);
									json_array_object_insert(arg_value, kv_key, kv_value);
								}
								else
								{
									if (sisdigit(wstokens[i].token->s+sep+1))
									{
										int64_t num = strtoll(wstokens[i].token->s+sep+1, NULL, 10);
										arg_value = json_integer(num);
									}
									else
										arg_value = json_string(wstokens[i].token->s+sep+1);

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
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(allow_entrypoint, operator_name, arg_json);
					}
					else if (!strcmp(context_name, "entrypoint") && (!strcmp(operator_name, "env") || !strcmp(operator_name, "header")))
					{
						json_t *value_json = json_string(wstokens[++i].token->s);
						json_array_object_insert(env_entrypoint, wstokens[i-1].token->s, value_json);
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
					else
					{
						json_t *arg_json = json_string(wstokens[i].token->s);
						json_array_object_insert(operator_json, operator_name, arg_json);
					}
				}
				else if (wstokens[i].context)
				{
					if (ac->log_level > 0)
						printf("\t\t\tcontext_name: %s, argument: '%s'\n", context_name, wstokens[i].token->s);

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
										if (ac->log_level > 1)
											printf("aggregate %s\n", mapping_name);
										arg_json = json_array();

										for (; i < token_count; i++)
										{
											if (ac->log_level > 1)
												printf("quantile/bucket %s\n", wstokens[i].token->s);
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
	uint64_t token_count = plain_count_get(context);
	config_parser_stat *wstokens = string_tokenizer(context, &token_count);
	char *ret = build_json_from_tokens(wstokens, token_count);
	config_parser_stat_free(wstokens, token_count);
	return ret;
}
