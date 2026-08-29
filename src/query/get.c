#include "query/type.h"
#include "main.h"
#include <string.h>

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

int query_except_match(query_node *qn, const char *name)
{
	if (!qn || !qn->except || !name)
		return 0;
	if (!qn->except->hash)
		return 0;
	if (!alligator_ht_count(qn->except->hash) && !qn->except->head)
		return 0;

	return match_mapper(qn->except, (char *)name, strlen(name), (char *)name) == 1;
}

typedef struct query_except_scan
{
	const char *name;
	int any_run;
} query_except_scan;

static void query_ds_except_scan(void *funcarg, void *arg)
{
	query_except_scan *s = funcarg;
	query_node *qn = arg;
	if (!query_except_match(qn, s->name))
		s->any_run = 1;
}

int query_ds_except_db(query_ds *qds, const char *dbname)
{
	if (!qds || !qds->hash || !dbname)
		return 0;
	if (!alligator_ht_count(qds->hash))
		return 0;

	query_except_scan s = { .name = dbname, .any_run = 0 };
	alligator_ht_foreach_arg(qds->hash, query_ds_except_scan, &s);
	return !s.any_run;
}
