#include "grok/type.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

int grok_push(json_t *grok) {
	json_t *jname = json_object_get(grok, "name");
	if (!jname) {
		glog(L_INFO, "grok_push: not specified 'name' option\n");
		return 0;
	}
	char *name = (char*)json_string_value(jname);

	json_t *jkey = json_object_get(grok, "key");
	if (!jkey) {
		glog(L_INFO, "grok_push: not specified 'key' option in '%s'\n", name);
		return 0;
	}
	char *key = (char*)json_string_value(jkey);

	json_t *jvalue = json_object_get(grok, "value");
	char *value = (char*)json_string_value(jvalue);

	json_t *jmatch = json_object_get(grok, "match");
	if (!jmatch) {
		glog(L_INFO, "grok_push: not specified 'match' option in '%s'\n", name);
		return 0;
	}
	char *match = (char*)json_string_value(jmatch);
	size_t match_len = json_string_length(jmatch);

	grok_node *gn = calloc(1, sizeof(*gn));

	gn->name = strdup(name);
	if (value)
		gn->value = strdup(value);
	gn->match = string_init_dupn(match, match_len);

	grok_ds *gps = alligator_ht_search(ac->grok, grokds_compare, gn->name, tommy_strhash_u32(0, gn->name));
	if (!gps)
	{
		gps = calloc(1, sizeof(*gps));
		gps->hash = calloc(1, sizeof(alligator_ht));
		alligator_ht_init(gps->hash);
		gps->key = strdup(key);
		alligator_ht_insert(ac->grok, &(gps->node), gps, tommy_strhash_u32(0, gps->key));
	}
	glog(L_INFO, "create grok node %p: '%s', match '%s'\n", gn, gn->name, gn->match->s);
	alligator_ht_insert(gps->hash, &(gn->node), gn, tommy_strhash_u32(0, gn->name));

	return 1;
}
