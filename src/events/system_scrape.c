#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "main.h"
#include "events/system_scrape.h"
void on_scrape_run(void* arg)
{
	system_scrape_info *ssinfo = arg;
	void (*handler)() = ssinfo->parser_handler;
	handler();
}

static void system_scrape(uv_timer_t* handle)
{
	(void)handle;
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->system_aggregator, on_scrape_run);
}

void do_system_scrape(void *handler, char *name)
{
	extern aconf* ac;
	system_scrape_info *ssinfo = calloc(1, sizeof(*ssinfo));
	ssinfo->parser_handler = handler;
	ssinfo->key = name;

	tommy_hashdyn_insert(ac->system_aggregator, &(ssinfo->node), ssinfo, tommy_strhash_u32(0, ssinfo->key));
}

void system_scrape_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, system_scrape, ac->system_aggregator_startup, ac->system_aggregator_repeat);

	uv_timer_t *timer2 = calloc(1, sizeof(*timer2));
	uv_timer_init(loop, timer2);
	uv_timer_start(timer2, system_fast_scrape, 500, 1000);

	uv_timer_t *timer3 = calloc(1, sizeof(*timer3));
	uv_timer_init(loop, timer3);
	uv_timer_start(timer3, system_slow_scrape, 10000, 250000);
}
