#include "stdlib.h"
#include "query/query.h"
#include "query/promql.h"
#include "metric/query.h"
#include "metric/labels.h"
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

	alligator_ht *qf_hash = query_get_field(jfield);

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

	query_ds *qds = alligator_ht_search(ac->query, queryds_compare, qn->datasource, tommy_strhash_u32(0, qn->datasource));
	if (!qds)
	{
		qds = calloc(1, sizeof(*qds));
		qds->hash = calloc(1, sizeof(alligator_ht));
		alligator_ht_init(qds->hash);
		qds->datasource = qn->datasource;
		alligator_ht_insert(ac->query, &(qds->node), qds, tommy_strhash_u32(0, qds->datasource));
	}
	if (ac->log_level > 0)
		printf("create query node make %p: '%s', expr '%s'\n", qn, qn->make, qn->expr);
	alligator_ht_insert(qds->hash, &(qn->node), qn, tommy_strhash_u32(0, qn->make));
}

void query_field_clean_foreach(void *funcarg, void* arg)
{
	query_field *qf = arg;
	free(qf->field);
}

void query_node_del(query_node *qn)
{
	if (qn->expr)
		free(qn->expr);
	if (qn->make)
		free(qn->make);
	if (qn->action)
		free(qn->action);
	if (qn->ns)
		free(qn->ns);
	if (qn->datasource)
		free(qn->datasource);
	if (qn->action)
		free(qn->action);
	if (qn->labels)
		labels_free(qn->labels);

	if (qn->qf_hash)
	{
		alligator_ht_foreach_arg(qn->qf_hash, query_field_clean_foreach, NULL);
		alligator_ht_done(qn->qf_hash);
		free(qn->qf_hash);
	}
	free(qn);
}

void query_del(char *datasource, char *make)
{
	query_ds *qds = alligator_ht_search(ac->query, queryds_compare, datasource, tommy_strhash_u32(0, datasource));
	if (qds)
	{
		query_node *qn = alligator_ht_search(qds->hash, query_compare, make, tommy_strhash_u32(0, make));
		if (qn)
		{
			alligator_ht_remove_existing(qds->hash, &(qn->node));

			query_node_del(qn);
		}

		uint64_t count = alligator_ht_count(qds->hash);
		if (!count)
		{
			alligator_ht_done(qds->hash);
			free(qds->hash);
			free(qds->datasource);
			free(qds);
		}
	}

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

void query_field_set_foreach(void *funcarg, void* arg)
{
	query_node *qn = (query_node*)funcarg;
	query_field *qf = arg;
	if (!qf->type)
		return;

	alligator_ht *duplabels = labels_dup(qn->labels);
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
	alligator_ht_foreach_arg(qn->qf_hash, query_field_set_foreach, qn);
}

void internal_query_process(query_node *qn)
{
	metric_query_context *mqc = promql_parser(NULL, qn->expr, strlen(qn->expr));

	metric_query_gen(0, mqc, qn->make, qn->action);
	query_context_free(mqc);
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
		alligator_ht_foreach(qds->hash, query_recurse);
}

void query_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->query_timer);
	uv_timer_start(&ac->query_timer, query_crawl, ac->query_startup, ac->query_repeat);
}

//void query_hash_foreach_done(void *funcarg, void* arg)
//{
//	query_field *qf = arg;
//	free(qf->field);
//	free(qf);
//}
//
void query_node_foreach_done(void *funcarg, void* arg)
{
	query_node *qn = arg;
	query_node_del(qn);
}

void query_foreach_done(void *funcarg, void* arg)
{
	query_ds *qds = arg;

	alligator_ht_foreach_arg(qds->hash, query_node_foreach_done, NULL);
	alligator_ht_done(qds->hash);
	free(qds->hash);

	//alligator_ht_foreach_arg(qn->qf_hash, query_hash_foreach_done, NULL);
	//alligator_ht_done(qn->qf_hash);
	//free(qn->qf_hash);

	//free(qn);
	free(qds);
}

void query_stop()
{
	alligator_ht_foreach_arg(ac->query, query_foreach_done, NULL);
	alligator_ht_done(ac->query);
	free(ac->query);
}
