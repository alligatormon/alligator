#include "grok/type.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

void grok_node_del(grok_node *gn)
{
	if (gn->name)
		free(gn->name);
	if (gn->value)
		free(gn->value);
	if (gn->match)
		string_free(gn->match);
	if (gn->reg)
		onig_free(gn->reg);
	if (gn->expanded_match)
		string_free(gn->expanded_match);
	if (gn->labels)
		labels_hash_free(gn->labels);

	free(gn);
}

void grok_node_foreach_done(void *funcarg, void* arg)
{
	grok_node *gn = arg;
	grok_node_del(gn);
}

int grok_del(json_t *grok)
{
	json_t *jmake = json_object_get(grok, "make");
	if (!jmake) {
		glog(L_INFO, "grok_del: not specified 'make' option\n");
		return 0;
	}
	char *make = (char*)json_string_value(jmake);

	json_t *jkey = json_object_get(grok, "key");
	if (!jkey) {
		glog(L_INFO, "grok_del: not specified 'key' option in '%s'\n", make);
		return 0;
	}
	char *key = (char*)json_string_value(jkey);

	grok_ds *gps = alligator_ht_search(ac->grok, grokds_compare, key, tommy_strhash_u32(0, key));
	if (gps)
	{
		grok_node *gn = alligator_ht_search(gps->hash, grok_compare, make, tommy_strhash_u32(0, make));
		if (gn)
		{
			alligator_ht_remove_existing(gps->hash, &(gn->node));

			grok_node_del(gn);
		}

		if (gps->separator)
			string_free(gps->separator);

		uint64_t count = alligator_ht_count(gps->hash);
		if (!count)
		{
			alligator_ht_done(gps->hash);
			free(gps->hash);
			free(gps->key);
			free(gps);
		}
	}

	return 1;
}

void grok_foreach_done(void *funcarg, void* arg)
{
	grok_ds *gps = arg;

	alligator_ht_foreach_arg(gps->hash, grok_node_foreach_done, NULL);
	alligator_ht_done(gps->hash);
	free(gps->key);
	free(gps->hash);

	free(gps);
}

void grok_stop()
{
	string_tokens_free(ac->grok_patterns_path);
	free(ac->grok_patterns->nodes);
	free(ac->grok_patterns);
	alligator_ht_foreach_arg(ac->grok, grok_foreach_done, NULL);
	alligator_ht_done(ac->grok);
	free(ac->grok);
}
