#include "common/logs.h"
#include "grok/type.h"
#include "main.h"

int grok_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((grok_node*)obj)->name;
	return strcmp(s1, s2);
}

int grokds_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((grok_ds*)obj)->key;
	return strcmp(s1, s2);
}

grok_ds* grok_get(char *key)
{
	if (!key) {
		glog(L_ERROR, "Not specified grok key\n");
		return NULL;
	}

	grok_ds *gps = alligator_ht_search(ac->grok, grokds_compare, key, tommy_strhash_u32(0, key));
	if (gps)
		return gps;
	else
		return NULL;
}

grok_node *grok_get_node(grok_ds *gps, char *name)
{
	if (!gps)
		return NULL;

	grok_node *gn = alligator_ht_search(gps->hash, grok_compare, name, tommy_strhash_u32(0, name));
	if (gn)
		return gn;
	else
		return NULL;
}
