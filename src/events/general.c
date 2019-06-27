#include "main.h"

void general_loop_cb(uv_timer_t* handle)
{
	//int64_t host_up = 1;
	//metric_add_auto("up", &host_up, DATATYPE_INT, 0);
	extern aconf* ac;

	//r_time time = setrtime();
	//expire_purge(time.sec, 0);

	metric_add_auto("alligator_metric_cache_hit", &ac->metric_cache_hits, DATATYPE_UINT, 0);
	metric_add_auto("alligator_metric_allocates", &ac->metric_allocates, DATATYPE_UINT, 0);
	metric_add_auto("alligator_metric_free", &ac->metric_freed, DATATYPE_UINT, 0);

	//expire_build(0);
}

void general_loop()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, general_loop_cb, 1000, 1000);
}
