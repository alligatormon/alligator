#include "query/type.h"
#include "main.h"

int query_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((query_node*)obj)->make;
	return strcmp(s1, s2);
}

int queryds_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((query_ds*)obj)->datasource;
	return strcmp(s1, s2);
}

int query_field_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((query_field*)obj)->field;
	return strcmp(s1, s2);
}

query_ds* query_get(char *datasource)
{
	query_ds *qds = alligator_ht_search(ac->query, queryds_compare, datasource, tommy_strhash_u32(0, datasource));
	if (qds)
		return qds;
	else
		return NULL;
}

query_node *query_get_node(query_ds *qds, char *make)
{
	if (!qds)
		return NULL;

	query_node *qn = alligator_ht_search(qds->hash, query_compare, make, tommy_strhash_u32(0, make));
	if (qn)
		return qn;
	else
		return NULL;
}

alligator_ht* query_get_field(json_t *jfield)
{
	uint64_t fields_count = json_array_size(jfield);
	alligator_ht *fields_hash = calloc(1, sizeof(*fields_hash));
	alligator_ht_init(fields_hash);
	
	for (uint64_t i = 0; i < fields_count; i++)
	{
		query_field *qf = calloc(1, sizeof(*qf));
		json_t *field_json = json_array_get(jfield, i);
		char *field = (char*)json_string_value(field_json);
		qf->field = strdup(field);
		alligator_ht_insert(fields_hash, &(qf->node), qf, tommy_strhash_u32(0, qf->field));
	}

	return fields_hash;
}

query_field* query_field_get(alligator_ht *qf_hash, char *key)
{
	return alligator_ht_search(qf_hash, query_field_compare, key, tommy_strhash_u32(0, key));
}
