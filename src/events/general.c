#include "main.h"
#include "common/selector.h"
#include "alligator_version.h"

void general_loop_cb(uv_timer_t* handle)
{
	extern aconf* ac;

	uint64_t val = 1;
	metric_add_labels("alligator_version", &val, DATATYPE_UINT, NULL, "version", ALLIGATOR_VERSION);
	metric_add_auto("alligator_metric_cache_hit", &ac->metric_cache_hits, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_allocates", &ac->metric_allocates, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_free", &ac->metric_freed, DATATYPE_UINT, NULL);
}

void expire_loop(uv_timer_t* handle)
{
	extern aconf* ac;

	r_time time = setrtime();
	expire_purge(time.sec, 0);
}

void dump_loop()
{
	metric_dump(-1);
	filetailer_write_state(ac->file_stat);
}

void general_loop()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->general_timer);
	uv_timer_start(&ac->general_timer, general_loop_cb, 1000, 1000);

	uv_timer_init(loop, &ac->expire_timer);
	uv_timer_start(&ac->expire_timer, expire_loop, 10000, 10000);

	uv_timer_init(loop, &ac->dump_timer);
	uv_timer_start(&ac->dump_timer, dump_loop, 11000, ac->persistence_period);
}
