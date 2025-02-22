#include "lang/type.h"
#include "config/get.h"
#include "api/api.h"
#include "parsers/multiparser.h"
#include "common/logs.h"
#include "query/type.h"
#include "config/get.h"
#include "common/json_query.h"
#include "main.h"
extern aconf *ac;

void langlog(lang_options *lo, int priority, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	wrlog(lo->log_level, priority, format, args);
	va_end(args);
}

int lang_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((lang_options*)obj)->key;
	return strcmp(s1, s2);
}


void lang_crawl(void* arg, string *data, string *parser_data, string *response)
{
	lang_options *lo = arg;
	char *ret = NULL;

	string *body = data;
	string *conf = NULL;
    int body_should_free = 0;
	if (!body && lo->query)
	{
		metric_query_context *mqc = promql_parser(NULL, lo->query, lo->query_len);
		body = metric_query_deserialize(1024, mqc, lo->serializer, 0, NULL, NULL, NULL, NULL);
		query_context_free(mqc);
        body_should_free = 1;
	}

	if (lo->conf)
	{
		conf = config_get_string();
	}

	char *queries = NULL;
	if (lo->carg && lo->carg->name)
	{
		query_ds *qds = query_get(lo->carg->name);
		json_t *dst = json_object();
		carglog(lo->carg, L_INFO, "found queries for datasource: %s: %p\n", lo->carg->name, qds);
		if (qds)
		{
			alligator_ht_foreach_arg(qds->hash, query_node_generate_conf, dst);
		}
        json_t *jqueries = json_object_get(dst, "query");
		queries = json_dumps(jqueries, JSON_INDENT(2));
	}

	if (!strcmp(lo->lang, "lua"))
		ret = lua_run(lo, lo->script, lo->file, lo->arg, body, conf, parser_data, response);
	else if (!strcmp(lo->lang, "mruby"))
		ret = mruby_run(lo, lo->script, lo->file, lo->arg, body, conf, parser_data, response);
	else if (!strcmp(lo->lang, "duktape"))
		ret = duktape_run(lo, lo->script, lo->file, lo->arg, body, conf, parser_data, response);
	else if (!strcmp(lo->lang, "so"))
		ret = so_run(lo, lo->script, lo->file, "data_example", lo->arg, body, conf, parser_data, response, queries);

	if (ret) {
		if (conf)
		{
			http_api_v1(NULL, NULL, ret);
		}
		else if (lo->carg->pquery_size)
		{
			json_query(ret, NULL, lo->module, lo->carg, lo->carg->pquery, lo->carg->pquery_size);
		}
		else
		{
			alligator_multiparser(ret, strlen(ret), NULL, NULL, lo->carg);
		}
		free(ret);
	}

	if (body_should_free && body)
		string_free(body);

	if (conf)
		string_free(conf);

	if (queries)
		free(queries);
}

void lang_load_script(char *script, size_t script_size, void *data, char *filename)
{
	lang_options *lo = data;
	lo->script = strdup(script);
	lo->script_size = script_size;
}

void lang_run(char *key, string *body, string *parser_data, string *response)
{
	lang_options* lo = alligator_ht_search(ac->lang_aggregator, lang_compare, key, tommy_strhash_u32(0, key));
	if (lo)
	{
		lang_crawl(lo, body, parser_data, response);
	}
	else
		glog(L_INFO, "lang run key %s failed: not found\n", key);
}

lang_options* lang_get(char *key)
{
	return alligator_ht_search(ac->lang_aggregator, lang_compare, key, tommy_strhash_u32(0, key));
}
