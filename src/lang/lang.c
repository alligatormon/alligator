#include "lang/lang.h"
#include "main.h"
extern aconf *ac;

void lang_push(lang_options *lo)
{
	if (ac->log_level > 0)
		printf("lang push key %s\n", lo->key);

	tommy_hashdyn_insert(ac->lang_aggregator, &(lo->node), lo, tommy_strhash_u32(0, lo->key));
}

int lang_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((lang_options*)obj)->key;
	return strcmp(s1, s2);
}

void lang_delete(char *key)
{
	lang_options* lo = tommy_hashdyn_search(ac->lang_aggregator, lang_compare, key, tommy_strhash_u32(0, key));
	if (lo)
	{
		tommy_hashdyn_remove_existing(ac->lang_aggregator, &(lo->node));

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
		if (lo->arg)
			free(lo->arg);

		free(lo);
	}
}

void lang_crawl(void* arg)
{
	lang_options *lo = arg;
	char *ret = NULL;
	if (!strcmp(lo->lang, "java"))
		ret = java_run("-Djava.class.path=/var/lib/alligator/", lo->classname, lo->method, lo->arg);

	if (ret) {
		multicollector(NULL, ret, strlen(ret), NULL);
		alligator_multiparser(ret, strlen(ret), NULL, NULL, lo->carg);
		free(ret);
	}
}

static void lang_cb(void *arg)
{
	usleep(ac->lang_aggregator_startup*1000);
	while ( 1 )
	{

		tommy_hashdyn_foreach(ac->lang_aggregator, lang_crawl);
		//java_run("-Djava.class.path=/var/lib/alligator/", "alligatorJmx", "getJmx");
		usleep(ac->lang_aggregator_repeat*1000);
	}
}

void lang_handler()
{
	uv_thread_t th;
	uv_thread_create(&th, lang_cb, NULL);
}
