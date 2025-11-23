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

//int grok_field_compare(const void* arg, const void* obj)
//{
//	char *s1 = (char*)arg;
//	char *s2 = ((grok_field*)obj)->field;
//	return strcmp(s1, s2);
//}

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

//alligator_ht* grok_get_field(json_t *jfield)
//{
//	uint64_t fields_count = json_array_size(jfield);
//	alligator_ht *fields_hash = calloc(1, sizeof(*fields_hash));
//	alligator_ht_init(fields_hash);
//	
//	for (uint64_t i = 0; i < fields_count; i++)
//	{
//		grok_field *qf = calloc(1, sizeof(*qf));
//		json_t *field_json = json_array_get(jfield, i);
//		char *field = (char*)json_string_value(field_json);
//		qf->field = strdup(field);
//		alligator_ht_insert(fields_hash, &(qf->node), qf, tommy_strhash_u32(0, qf->field));
//	}
//
//	return fields_hash;
//}
//
//grok_field* grok_field_get(alligator_ht *qf_hash, char *key)
//{
//	return alligator_ht_search(qf_hash, grok_field_compare, key, tommy_strhash_u32(0, key));
//}
