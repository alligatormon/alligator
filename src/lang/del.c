#include "lang/type.h"
#include "common/logs.h"
#include "dstructures/ht.h"

void lang_options_free(lang_options* lo)
{
	if (lo->lang)
		free(lo->lang);
	if (lo->key)
		free(lo->key);
	//if (lo->classpath)
	//	free(lo->classpath);
	//if (lo->classname)
	//	free(lo->classname);
	if (lo->method)
		free(lo->method);
	if (lo->module)
		free(lo->module);
	if (lo->arg)
		free(lo->arg);
	//if (lo->path)
	//	free(lo->path);
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

void lang_delete(json_t *lang)
{
	json_t *lang_key = json_object_get(lang, "key");
	if (!lang_key)
	{
		glog(L_ERROR, "no key for lang \n");
		return;
	}
	char *key = (char*)json_string_value(lang_key);

	lang_options* lo = alligator_ht_search(ac->lang_aggregator, lang_compare, key, tommy_strhash_u32(0, key));

	lang_options_del(lo);
}

void lang_delete_key(char *key)
{
	lang_options* lo = alligator_ht_search(ac->lang_aggregator, lang_compare, key, tommy_strhash_u32(0, key));

	lang_options_del(lo);
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
