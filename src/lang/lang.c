#include "lang/lang.h"
#include "config/get.h"
#include "main.h"
extern aconf *ac;

int lang_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((lang_options*)obj)->key;
	return strcmp(s1, s2);
}

void lang_options_free(lang_options* lo)
{
	if (lo->lang)
		free(lo->lang);
	if (lo->key)
		free(lo->key);
	if (lo->classpath)
		free(lo->classpath);
	if (lo->classname)
		free(lo->classname);
	if (lo->method)
		free(lo->method);
	if (lo->module)
		free(lo->module);
	if (lo->arg)
		free(lo->arg);
	if (lo->path)
		free(lo->path);
	if (lo->query)
		free(lo->query);

	free(lo);
}

void lang_options_del(lang_options* lo)
{
	if (!lo)
		return;

	alligator_ht_remove_existing(ac->lang_aggregator, &(lo->node));

	lang_options_free(lo);
}

void lang_delete(char *key)
{
	lang_options* lo = alligator_ht_search(ac->lang_aggregator, lang_compare, key, tommy_strhash_u32(0, key));

	lang_options_del(lo);
}

void lang_crawl(void* arg, string *data, string *parser_data, string *response)
{
	lang_options *lo = arg;
	char *ret = NULL;

	string *body = NULL;
	string *conf = NULL;
	if (lo->query)
	{
		metric_query_context *mqc = promql_parser(NULL, lo->query, lo->query_len);
		body = metric_query_deserialize(1024, mqc, lo->serializer, 0, NULL, NULL, NULL, NULL, NULL);
		query_context_free(mqc);
	}

	if (lo->conf)
	{
		conf = config_get_string();
	}

	if (!strcmp(lo->lang, "lua"))
		ret = lua_run(lo, lo->script, lo->file, lo->arg, body, conf, parser_data, response);
	else if (!strcmp(lo->lang, "mruby"))
		ret = mruby_run(lo, lo->script, lo->file, lo->arg, body, conf, parser_data, response);
	else if (!strcmp(lo->lang, "duktape"))
		ret = duktape_run(lo, lo->script, lo->file, lo->arg, lo->path, body, conf, parser_data, response);
	else if (!strcmp(lo->lang, "so"))
		ret = so_run(lo, lo->script, lo->file, "data_example", lo->arg, body, conf, parser_data, response);

	if (ret) {
		if (conf)
		{
			http_api_v1(NULL, NULL, ret);
		}
		else
		{
			alligator_multiparser(ret, strlen(ret), NULL, NULL, lo->carg);
		}
		free(ret);
	}

	if (body)
		string_free(body);

	if (conf)
		string_free(conf);
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
		if (ac->log_level > 0)
			printf("lang run key %s\n", lo->key);
		lang_crawl(lo, body, parser_data, response);
	}
	else
		if (ac->log_level > 0)
			printf("lang run key %s failed: not found\n", key);
}

void lang_foreach_del(void *funcarg, void* arg)
{
	lang_options *lo = arg;

	lang_options_free(lo);
}

void lang_stop()
{
	alligator_ht_foreach_arg(ac->lang_aggregator, lang_foreach_del, NULL);
	alligator_ht_done(ac->lang_aggregator);
	free(ac->lang_aggregator);
}
