#include <stdio.h>
#include "main.h"
#include "metric/expiretree.h"

void alligator_stop(char *initiator, int code)
{
	printf("Stop signal received: %d from '%s'\n", code, initiator);
	metric_dump(1);
	file_stat_free(ac->file_stat);
	tls_fs_free();
	puppeteer_done();
	aggregators_free();
	aggregate_ctx_free();
	entrypoints_free();
	namespace_free(0, NULL);
	cluster_del_all();
	cluster_handler_stop();
	scheduler_del_all();
	query_stop();
	system_free();
	resolver_stop();
	lang_stop();
	alligator_cache_full_free(ac->uv_cache_timer);
	alligator_cache_full_free(ac->uv_cache_fs);

	free_namespaces();
	main_free();

	uv_loop_close(uv_default_loop());

	exit(0);
}
