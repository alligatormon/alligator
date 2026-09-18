#include "stdlib.h"
#include <string.h>
#include "query/type.h"
#include "query/promql.h"
#include "metric/query.h"
#include "metric/labels.h"
#include "common/logs.h"
#include "main.h"

static void query_merge_add_labels(void *funcarg, void *arg)
{
	labels_container *lc = arg;
	alligator_ht *dst = funcarg;
	if (lc && lc->name && lc->key)
		labels_hash_insert_nocache(dst, lc->name, lc->key);
}

void query_field_set_foreach(void *funcarg, void* arg)
{
	query_node *qn = (query_node*)funcarg;
	query_field *qf = arg;
	if (!qf->type)
		return;

	alligator_ht *duplabels = qn->labels ? labels_dup(qn->labels) : alligator_ht_init(NULL);
	if (qn->add_labels)
		alligator_ht_foreach_arg(qn->add_labels, query_merge_add_labels, duplabels);

	carg_or_glog(qn->carg, L_TRACE,
		"query make '%s' datasource '%s' field '%s' type=%d i=%"d64" u=%"u64" d=%g labels=%"u64" add_labels=%"u64"\n",
		qn->make ? qn->make : "",
		qn->datasource ? qn->datasource : "",
		qf->field ? qf->field : "",
		(int)qf->type, qf->i, qf->u, qf->d,
		qn->labels ? alligator_ht_count(qn->labels) : 0,
		qn->add_labels ? alligator_ht_count(qn->add_labels) : 0);

	context_arg *use = qn->carg;
	context_arg overlay;
	if (qn->metricstransform)
	{
		if (qn->carg)
			overlay = *qn->carg;
		else
			memset(&overlay, 0, sizeof(overlay));
		overlay.metricstransform = qn->metricstransform;
		use = &overlay;
	}

	if (qf->type == DATATYPE_INT)
		metric_add(qf->field, duplabels, &qf->i, qf->type, use);
	if (qf->type == DATATYPE_UINT)
		metric_add(qf->field, duplabels, &qf->u, qf->type, use);
	if (qf->type == DATATYPE_DOUBLE)
		metric_add(qf->field, duplabels, &qf->d, qf->type, use);
	qf->i = 0;
	qf->u = 0;
	qf->d = 0;
	qf->type = DATATYPE_NONE;
}

static void query_field_reset_foreach(void *funcarg, void* arg)
{
	query_field *qf = arg;
	(void)funcarg;
	qf->i = 0;
	qf->u = 0;
	qf->d = 0;
	qf->type = DATATYPE_NONE;
}

void query_set_values(query_node *qn)
{
	if (!qn || !qn->qf_hash)
		return;
	if (query_except_match_row(qn)) {
		if (qn->carg)
			carglog(qn->carg, L_DEBUG, "skip query make '%s': except matched result database label\n", qn->make ? qn->make : "");
		alligator_ht_foreach_arg(qn->qf_hash, query_field_reset_foreach, qn);
		return;
	}
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

