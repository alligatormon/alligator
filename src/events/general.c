#include "main.h"

void general_loop_cb(uv_timer_t* handle)
{
	extern aconf* ac;

	//check_https_cert("google.com");

	metric_add_auto("alligator_metric_cache_hit", &ac->metric_cache_hits, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_allocates", &ac->metric_allocates, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_free", &ac->metric_freed, DATATYPE_UINT, NULL);
}

void expire_loop(uv_timer_t* handle)
{
	r_time time = setrtime();
	expire_purge(time.sec, 0);
}

void general_loop()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *general_timer = calloc(1, sizeof(*general_timer));
	uv_timer_init(loop, general_timer);
	uv_timer_start(general_timer, general_loop_cb, 1000, 1000);

	uv_timer_t *expire_timer = calloc(1, sizeof(*expire_timer));
	uv_timer_init(loop, expire_timer);
	uv_timer_start(expire_timer, expire_loop, 10000, 10000);
}
