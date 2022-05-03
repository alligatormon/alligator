#include <stdio.h>
#include "main.h"
#include "metric/expiretree.h"

void alligator_stop(char *initiator, int code)
{
	printf("Stop signal received: %d from '%s'\n", code, initiator);
	metric_dump(1);
	puppeteer_done();
	aggregators_free();
	aggregate_ctx_free();
	entrypoints_free();
	namespace_free(0, NULL);
	cluster_del_all();
	cluster_handler_stop();
	system_free();
	alligator_timer_full_free(ac->altimer);

	uv_loop_close(uv_default_loop());

	exit(0);
}
