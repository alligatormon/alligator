#include "main.h"
#include "common/selector.h"
#include "alligator_version.h"
#include "metric/metric_dump.h"
#include "events/filetailer.h"

extern aconf *ac;

void general_loop_cb(uv_timer_t* handle)
{
	uint64_t val = 1;
	metric_add_labels("alligator_version", &val, DATATYPE_UINT, NULL, "version", ALLIGATOR_VERSION);
	metric_add_auto("alligator_metric_cache_hit", &ac->metric_cache_hits, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_allocates", &ac->metric_allocates, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_free", &ac->metric_freed, DATATYPE_UINT, NULL);
}

void namespaces_expire_foreach(void *funcarg, void* arg)
{
	namespace_struct *ns = arg;
	r_time time = setrtime();
	expire_purge(time.sec, NULL, ns);
}


void expire_loop(uv_timer_t* handle)
{
	alligator_ht_foreach_arg(ac->_namespace, namespaces_expire_foreach, NULL);
}

void dump_loop()
{
	metric_dump(-1);
	filetailer_write_state(ac->file_stat);
}

void general_loop()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->general_timer);
	uv_timer_start(&ac->general_timer, general_loop_cb, 1000, 1000);

	uv_timer_init(loop, &ac->expire_timer);
	uv_timer_start(&ac->expire_timer, expire_loop, 10000, 10000);

	uv_timer_init(loop, &ac->dump_timer);
	uv_timer_start(&ac->dump_timer, dump_loop, 11000, ac->persistence_period);
}
