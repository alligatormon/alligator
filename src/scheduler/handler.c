#include "main.h"
#include "scheduler/type.h"
#include "query/promql.h"
#include "dstructures/uv_cache.h"
#include "lang/type.h"
//void scheduler_run(void *arg)
void scheduler_run(uv_timer_t* handle)
{
	//scheduler_node *sn = arg;
	scheduler_node *sn = handle->data;

    printf("run %p\n", sn->lang);
	if (sn->datasource_int == SCHEDULER_DATASOURCE_INTERNAL)
	{
		metric_query_context *mqc = NULL;
		if (sn->expr && sn->expr->l)
			mqc = promql_parser(NULL, sn->expr->s, sn->expr->l);

		if (sn->action)
		{
			// TODO: fix sn->name
			action_run_process(sn->action, NULL, mqc);
		}
		if (sn->lang)
		{
			lang_run(sn->lang, NULL, NULL, NULL);
		}

		if (mqc)
			query_context_free(mqc);
	}
}

void scheduler_start(scheduler_node *sn)
{
	sn->timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
	sn->timer->data = sn;
	uv_timer_init(ac->loop, sn->timer);
	uv_timer_start(sn->timer, scheduler_run, ac->scheduler_startup, sn->period);
}

void scheduler_stop(scheduler_node *sn)
{
	uv_timer_stop(sn->timer);
	alligator_cache_push(ac->uv_cache_timer, sn->timer);
}
