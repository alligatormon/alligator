#include "lang/lang.h"
#include "config/get.h"
#include "main.h"
extern aconf *ac;

void lang_push(lang_options *lo)
{
	if (ac->log_level > 0)
		printf("lang push key %s\n", lo->key);

	alligator_ht_insert(ac->lang_aggregator, &(lo->node), lo, tommy_strhash_u32(0, lo->key));
}

int lang_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((lang_options*)obj)->key;
	return strcmp(s1, s2);
}

void lang_delete(char *key)
{
	lang_options* lo = alligator_ht_search(ac->lang_aggregator, lang_compare, key, tommy_strhash_u32(0, key));
	if (lo)
	{
		alligator_ht_remove_existing(ac->lang_aggregator, &(lo->node));

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
}

void lang_crawl(void* arg)
{
	lang_options *lo = arg;
	char *ret = NULL;

	string *body = NULL;
	string *conf = NULL;
	if (lo->query)
	{
		metric_query_context *mqc = promql_parser(NULL, lo->query, lo->query_len);
		body = metric_query_deserialize(1024, mqc, lo->serializer, 0, NULL);
	}

	if (lo->conf)
	{
		conf = config_get_string();
	}

	if (!strcmp(lo->lang, "java"))
		ret = java_run("-Djava.class.path=/var/lib/alligator/", lo->classname, lo->method, lo->arg, body, conf);
	else if (!strcmp(lo->lang, "lua"))
		ret = lua_run(lo, lo->script, lo->file, lo->arg, body, conf);
	else if (!strcmp(lo->lang, "mruby"))
		ret = mruby_run(lo, lo->script, lo->file, lo->arg, body, conf);
	//else if (!strcmp(lo->lang, "python"))
	//	ret = python_run(lo, lo->script, lo->file, lo->arg, lo->path, body, conf);
	else if (!strcmp(lo->lang, "duktape"))
		ret = duktape_run(lo, lo->script, lo->file, lo->arg, lo->path, body, conf);
	else if (!strcmp(lo->lang, "so"))
		ret = so_run(lo, lo->script, lo->file, "data_example", lo->arg, body, conf);

	if (ret) {
		if (conf)
		{
			http_api_v1(NULL, NULL, ret);
		}
		else
		{
			//multicollector(NULL, ret, strlen(ret), NULL);
			alligator_multiparser(ret, strlen(ret), NULL, NULL, lo->carg);
		}
		free(ret);
	}

	if (body)
		string_free(body);

	if (conf)
		string_free(conf);
}

static void lang_threads_cb(void *arg)
{
	//printf("pthread id %lu\n", pthread_self());
	lang_options *lo = arg;
	uint64_t period = ac->lang_aggregator_repeat*1000;
	if (lo->period)
		period = lo->period * 1000;

	usleep(ac->lang_aggregator_startup*1000);
	while ( 1 )
	{

		lang_crawl(lo);
		usleep(period);
	}
}

void lang_run_threads(void *arg)
{
	lang_options *lo = arg;
	if (!lo->th)
	{
		//lo->th = calloc(1, sizeof(*lo->th));
		//uv_thread_create(lo->th, lang_threads_cb, arg);
		lang_threads_cb(arg);
	}
}

static void lang_cb(void *arg)
{
	usleep(ac->lang_aggregator_startup*1000);
	while ( 1 )
	{
		alligator_ht_foreach(ac->lang_aggregator, lang_run_threads);
		usleep(ac->lang_aggregator_repeat*1000);
	}
}

void lang_handler()
{
	uv_thread_t th;
	uv_thread_create(&th, lang_cb, NULL);
}

void lang_load_script(char *script, size_t script_size, void *data, char *filename)
{
	lang_options *lo = data;
	lo->script = strdup(script);
	lo->script_size = script_size;
}
