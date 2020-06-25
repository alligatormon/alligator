#include "lang/lang.h"
#include "main.h"
extern aconf *ac;

void lang_push(lang_options *lo)
{
	size_t keysize = 0;
	if (lo->classpath)
		keysize += strlen(lo->classpath);
	if (lo->classname)
		keysize += strlen(lo->classname);
	if (lo->method)
		keysize += strlen(lo->method);

	lo->key = malloc(keysize);
	snprintf(lo->key, keysize, "%s%s%s", lo->classpath, lo->classname, lo->method);

	tommy_hashdyn_insert(ac->lang_aggregator, &(lo->node), lo, tommy_strhash_u32(0, lo->key));
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
