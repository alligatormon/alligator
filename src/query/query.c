#include "stdlib.h"
#include "query/query.h"
#include "main.h"

int query_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((query_node*)obj)->make;
	return strcmp(s1, s2);
}

void query_push(char *datasource, char *expr, char *make, char *action, char *field)
{
	query_node *qn = calloc(1, sizeof(*qn));

	qn->expr = expr;
	qn->make = make;
	qn->action = action;
	qn->field = field;
	qn->datasource = datasource;

	tommy_hashdyn_insert(ac->query, &(qn->node), qn, tommy_strhash_u32(0, qn->make));
}

void query_del(char *make)
{
	query_node *qn = tommy_hashdyn_search(ac->query, query_compare, make, tommy_strhash_u32(0, make));
	if (qn)
	{
		tommy_hashdyn_remove_existing(ac->query, &(qn->node));

		if (qn->expr)
			free(qn->expr);
		if (qn->make)
			free(qn->make);
		if (qn->action)
			free(qn->action);
		if (qn->field)
			free(qn->field);
		if (qn->datasource)
			free(qn->datasource);
		free(qn);
	}
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
