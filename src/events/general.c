#include "main.h"

void general_loop_cb(uv_timer_t* handle)
{
	int64_t host_up = 1;
	metric_labels_add_auto("up", &host_up, ALLIGATOR_DATATYPE_INT, 0);
}

void general_loop()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, general_loop_cb, 1000, 1000);
}
