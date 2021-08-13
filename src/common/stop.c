#include <stdio.h>
#include "main.h"

void alligator_stop_timeout(uv_timer_t *timer)
{
	free(timer);
	uv_loop_close(uv_default_loop());
	exit(0);
}

void alligator_stop(char *initiator, int code)
{
	printf("Stop signal received: %d from '%s'\n", code, initiator);
	metric_dump(1);
	puppeteer_done();

	uv_timer_t* timer1 = calloc(1, sizeof(*timer1));;
	uv_timer_init(ac->loop, timer1);
	uv_timer_start(timer1, alligator_stop_timeout, 500, 1000);
}
