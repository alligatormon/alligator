#include "main.h"
#include "metric/expiretree.h"
#include "metric/metric_dump.h"
#include "common/file_stat.h"
#include "x509/type.h"
#include "puppeteer/puppeteer.h"
#include "common/aggregator.h"
#include "metric/namespace.h"
#include "cluster/type.h"
#include "scheduler/type.h"
#include "query/type.h"
#include "resolver/resolver.h"
#include "lang/type.h"
#include "dstructures/uv_cache.h"
#include "system/common.h"
#include "events/system_scrape.h"
#include "events/a_signal.h"

void alligator_stop(char *sig, int code)
{
	printf("Stop signal received: %d: '%s'\n", code, sig);
	printf("Don't forget to start me again, otherwise the alligator will bite you :)\n");
	fflush(stdout);
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
	system_scrape_free();
	resolver_stop();
	lang_stop();
	alligator_cache_full_free(ac->uv_cache_timer);
	alligator_cache_full_free(ac->uv_cache_fs);

	free_namespaces();
	main_free();
	signal_stop();

	//uv_loop_close(uv_default_loop());

	exit(0);
}
