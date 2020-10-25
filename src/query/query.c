#include "stdlib.h"
#include "query/query.h"
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

void query_push(char *datasource, char *expr, char *make, char *action, char *field)
{
	query_node *qn = calloc(1, sizeof(*qn));

	if (expr)
		qn->expr = strdup(expr);
	if (action)
		qn->action = strdup(action);
	if (field)
		qn->field = strdup(field);

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
			if (qn->field)
				free(qn->field);
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

void internal_query_process()
{
	//string *body = string_init(10000000);

	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);

	//labels_hash_insert(hash, "src_port", "80");
	labels_hash_insert(hash, "proto", "tcp");

	//metric_query(0, body, "socket_stat", hash, "");
	metric_query_gen(0, "socket_stat", hash, "", "socket_match");
	//printf("body '%s'\n", body->s);
	//string_free(body);
}

void query_processing(query_node *qn)
{
	internal_query_process();
}

void query_recurse(void *arg)
{
	if (!arg)
		return;

	query_node *qn = arg;

	puts("query_processing");
	query_processing(qn);
}

static void query_crawl(uv_timer_t* handle) {
	(void)handle;
	tommy_hashdyn_foreach(ac->query, query_recurse);
}

void query_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, query_crawl, ac->query_startup, ac->query_repeat);
}
