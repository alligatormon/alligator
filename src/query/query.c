#include "stdlib.h"
#include "query/query.h"
#include "query/promql.h"
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

void query_push(char *datasource, char *expr, char *make, char *action, char *ns, json_t *jfield)
{
	query_node *qn = calloc(1, sizeof(*qn));

	tommy_hashdyn *qf_hash = query_get_field(jfield);

	if (expr)
		qn->expr = strdup(expr);
	if (action)
		qn->action = strdup(action);
	if (ns)
		qn->ns = strdup(ns);
	//if (field)
	//	qn->field = strdup(field);
	qn->qf_hash = qf_hash;

	qn->make = strdup(make);
	qn->datasource = strdup(datasource); // part of query ds

	query_ds *qds = tommy_hashdyn_search(ac->query, queryds_compare, qn->datasource, tommy_strhash_u32(0, qn->datasource));
	if (!qds)
	{
		qds = calloc(1, sizeof(*qds));
		qds->hash = calloc(1, sizeof(tommy_hashdyn));
		tommy_hashdyn_init(qds->hash);
		qds->datasource = qn->datasource;
		tommy_hashdyn_insert(ac->query, &(qds->node), qds, tommy_strhash_u32(0, qds->datasource));
	}
	if (ac->log_level > 0)
		printf("create query node make '%s', expr '%s'\n", qn->make, qn->expr);
	tommy_hashdyn_insert(qds->hash, &(qn->node), qn, tommy_strhash_u32(0, qn->make));
}

void query_field_clean_foreach(void *funcarg, void* arg)
{
	query_field *qf = arg;
	free(qf->field);
}

void query_del(char *datasource, char *make)
{
	query_ds *qds = tommy_hashdyn_search(ac->query, queryds_compare, datasource, tommy_strhash_u32(0, datasource));
	if (qds)
	{
		query_node *qn = tommy_hashdyn_search(qds->hash, query_compare, make, tommy_strhash_u32(0, make));
		if (qn)
		{
			tommy_hashdyn_remove_existing(qds->hash, &(qn->node));

			if (qn->expr)
				free(qn->expr);
			if (qn->make)
				free(qn->make);
			if (qn->action)
				free(qn->action);
			if (qn->ns)
				free(qn->ns);
			if (qn->qf_hash)
			{
				tommy_hashdyn_foreach_arg(qn->qf_hash, query_field_clean_foreach, NULL);
				tommy_hashdyn_done(qn->qf_hash);
				free(qn->qf_hash);
			}
			free(qn);
		}

		uint64_t count = tommy_hashdyn_count(qds->hash);
		if (!count)
		{
			tommy_hashdyn_done(qds->hash);
			free(qds->hash);
			free(qds->datasource);
			free(qds);
		}
	}

}

query_ds* query_get(char *datasource)
{
	query_ds *qds = tommy_hashdyn_search(ac->query, queryds_compare, datasource, tommy_strhash_u32(0, datasource));
	if (qds)
		return qds;
	else
		return NULL;
}

query_node *query_get_node(query_ds *qds, char *make)
{
	if (!qds)
		return NULL;

	query_node *qn = tommy_hashdyn_search(qds->hash, query_compare, make, tommy_strhash_u32(0, make));
	if (qn)
		return qn;
	else
		return NULL;
}

tommy_hashdyn* query_get_field(json_t *jfield)
{
	uint64_t fields_count = json_array_size(jfield);
	tommy_hashdyn *fields_hash = calloc(1, sizeof(*fields_hash));
	tommy_hashdyn_init(fields_hash);
	
	for (uint64_t i = 0; i < fields_count; i++)
	{
		query_field *qf = calloc(1, sizeof(*qf));
		json_t *field_json = json_array_get(jfield, i);
		char *field = (char*)json_string_value(field_json);
		qf->field = strdup(field);
		tommy_hashdyn_insert(fields_hash, &(qf->node), qf, tommy_strhash_u32(0, qf->field));
	}

	return fields_hash;
}

query_field* query_field_get(tommy_hashdyn *qf_hash, char *key)
{
	return tommy_hashdyn_search(qf_hash, query_field_compare, key, tommy_strhash_u32(0, key));
}

void query_field_set_foreach(void *funcarg, void* arg)
{
	query_node *qn = (query_node*)funcarg;
	query_field *qf = arg;
	if (!qf->type)
		return;

	tommy_hashdyn *duplabels = labels_dup(qn->labels);
	//printf("2 qf %p\n", qf);
	//printf("2 qf->value %p\n", &qf->i);
	//printf("2 qf->field %p\n", qf->field);
	//printf("2 qf->value '%s': %"d64"\n", qf->field, (uint64_t)qf->i);
	if (qf->type == DATATYPE_INT)
		metric_add(qf->field, duplabels, &qf->i, qf->type, qn->carg);
	if (qf->type == DATATYPE_UINT)
		metric_add(qf->field, duplabels, &qf->u, qf->type, qn->carg);
	if (qf->type == DATATYPE_DOUBLE)
		metric_add(qf->field, duplabels, &qf->d, qf->type, qn->carg);
	qf->i = 0;
	qf->u = 0;
	qf->d = 0;
	qf->type = DATATYPE_NONE;
}

void query_set_values(query_node *qn)
{
	tommy_hashdyn_foreach_arg(qn->qf_hash, query_field_set_foreach, qn);
}

void internal_query_process(query_node *qn)
{
	//puts("===============");
	char *name = malloc(255);
	string *groupkey = string_new();
	int func;
	tommy_hashdyn *hash = promql_parser(NULL, qn->expr, strlen(qn->expr), name, &func, groupkey);

	//printf("query is %s:%s:%s:%d\n", name, qn->make, groupkey->s, func);
	metric_query_gen(0, name, hash, "", qn->make, func, groupkey);
	string_free(groupkey);
	free(name);
}

void query_processing(query_node *qn)
{
	internal_query_process(qn);
}

void query_recurse(void *arg)
{
	if (!arg)
		return;

	query_node *qn = arg;

	query_processing(qn);
}

static void query_crawl(uv_timer_t* handle) {
	(void)handle;
	query_ds *qds = query_get("internal");
	if (qds)
		tommy_hashdyn_foreach(qds->hash, query_recurse);
}

void query_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, query_crawl, ac->query_startup, ac->query_repeat);
}
