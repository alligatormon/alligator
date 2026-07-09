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
#include "grok/type.h"
#include "events/system_scrape.h"
#include "events/a_signal.h"
#include "events/filetailer.h"
#include "system/linux/ipmi.h"
#include <signal.h>
#include <string.h>
#include <stdio.h>

static volatile sig_atomic_t g_stop_requested;
static int g_stop_code;
static char g_stop_sig[48];

void alligator_stop(char *sig, int code)
{
	/* Never run teardown (ipmi_wait_idle -> uv_run) from inside libuv's signal
	 * callback: nested uv_run on the same loop is unsafe and can hang SIGINT. */
	if (g_stop_requested)
		return;
	g_stop_requested = 1;
	g_stop_code = code;
	if (sig)
		snprintf(g_stop_sig, sizeof g_stop_sig, "%s", sig);
	else
		g_stop_sig[0] = '\0';
	resolver_probes_halt();
	if (ac && ac->loop)
		uv_stop(ac->loop);
}

int alligator_stop_requested(void)
{
	return g_stop_requested ? 1 : 0;
}

void alligator_shutdown_after_loop(void)
{
	if (!g_stop_requested)
		return;

	printf("Stop signal received: %d: '%s'\n", g_stop_code, g_stop_sig[0] ? g_stop_sig : "?");
	printf("Don't forget to start me again, otherwise the alligator will bite you :)\n");
	fflush(stdout);
	ipmi_wait_idle();
	metric_dump(1);
	tls_fs_free();
	puppeteer_done();
	filetailer_shutdown();
	aggregators_free();
	aggregate_ctx_free();
	file_stat_free(ac->file_stat);
	entrypoints_free();
	namespace_free(0, NULL);
	cluster_del_all();
	scheduler_del_all();
	cluster_handler_stop();
	query_stop();
	system_free();
	system_scrape_free();
	resolver_stop();
	lang_stop();
	grok_stop();
	alligator_cache_full_free(ac->uv_cache_timer);
	alligator_cache_full_free(ac->uv_cache_fs);

	free_namespaces();
	main_free();

	//uv_loop_close(uv_default_loop());

	exit(0);
}
