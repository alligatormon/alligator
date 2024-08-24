#include "stdlib.h"
#include "query/type.h"
#include "query/promql.h"
#include "metric/query.h"
#include "metric/labels.h"
#include "main.h"

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

	metric_query_gen(qn->ns, mqc, qn->make, qn->action);
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

