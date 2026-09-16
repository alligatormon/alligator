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
	alligator_ht_foreach(ac->system_aggregator, on_scrape_run);
}

void on_scrape_free(void* arg)
{
	system_scrape_info *ssinfo = arg;
	free(ssinfo);
}

void system_scrape_free() {
	if (!ac)
		return;

	if (ac->system_scrape_timer_general.loop &&
	    !uv_is_closing((uv_handle_t *)&ac->system_scrape_timer_general))
		uv_timer_stop(&ac->system_scrape_timer_general);
	if (ac->system_scrape_timer_fast.loop &&
	    !uv_is_closing((uv_handle_t *)&ac->system_scrape_timer_fast))
		uv_timer_stop(&ac->system_scrape_timer_fast);
	if (ac->system_scrape_timer_slow.loop &&
	    !uv_is_closing((uv_handle_t *)&ac->system_scrape_timer_slow))
		uv_timer_stop(&ac->system_scrape_timer_slow);

	if (!ac->system_aggregator)
		return;

	alligator_ht_foreach(ac->system_aggregator, on_scrape_free);
}

void do_system_scrape(void *handler, char *name)
{
	extern aconf* ac;
	system_scrape_info *ssinfo = calloc(1, sizeof(*ssinfo));
	ssinfo->parser_handler = handler;
	ssinfo->key = name;

	alligator_ht_insert(ac->system_aggregator, &(ssinfo->node), ssinfo, tommy_strhash_u32(0, ssinfo->key));
}

void system_scrape_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->system_scrape_timer_general);
	uv_timer_start(&ac->system_scrape_timer_general, system_scrape, ac->system_aggregator_startup, ac->system_aggregator_repeat);

	uv_timer_init(loop, &ac->system_scrape_timer_fast);
	uv_timer_start(&ac->system_scrape_timer_fast, system_fast_scrape, 500, 1000);

	uv_timer_init(loop, &ac->system_scrape_timer_slow);
	uv_timer_start(&ac->system_scrape_timer_slow, system_slow_scrape, 4000, 250000);
}
