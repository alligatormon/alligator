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
#include "system/linux/nvml.h"
#include "system/linux/dcgm.h"
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include "common/logs.h"

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

static void alligator_timer_stop_if_active(uv_timer_t *timer)
{
	if (timer && timer->loop && !uv_is_closing((uv_handle_t *)timer))
		uv_timer_stop(timer);
}

/* Nested uv_run during teardown (wait_idle / filetailer close drain) must not
 * re-enter periodic crawlers after their hashtables are freed. */
static void alligator_stop_periodic_timers(void)
{
	if (!ac)
		return;

	alligator_timer_stop_if_active(&ac->puppeteer_timer);
	alligator_timer_stop_if_active(&ac->chromecdp_timer);
	alligator_timer_stop_if_active(&ac->system_scrape_timer_general);
	alligator_timer_stop_if_active(&ac->system_scrape_timer_fast);
	alligator_timer_stop_if_active(&ac->system_scrape_timer_slow);
	alligator_timer_stop_if_active(&ac->filetailer_timer);
	alligator_timer_stop_if_active(&ac->tls_fs_timer);
	alligator_timer_stop_if_active(&ac->query_timer);
	alligator_timer_stop_if_active(&ac->cluster_timer);
	alligator_timer_stop_if_active(&ac->process_timer);
	alligator_timer_stop_if_active(&ac->tcp_client_timer);
	alligator_timer_stop_if_active(&ac->udp_client_timer);
	alligator_timer_stop_if_active(&ac->udgregator_timer);
	alligator_timer_stop_if_active(&ac->pg_timer);
	alligator_timer_stop_if_active(&ac->cass_timer);
	alligator_timer_stop_if_active(&ac->my_timer);
	alligator_timer_stop_if_active(&ac->zk_timer);
	alligator_timer_stop_if_active(&ac->general_timer);
	alligator_timer_stop_if_active(&ac->expire_timer);
	alligator_timer_stop_if_active(&ac->dump_timer);
#ifdef __linux__
	alligator_timer_stop_if_active(&ac->icmp_client_timer);
#endif
}

void alligator_shutdown_after_loop(void)
{
	if (!g_stop_requested)
		return;

	glog(L_INFO, "Stop signal received: %d: '%s'\n", g_stop_code, g_stop_sig[0] ? g_stop_sig : "?");
	glog(L_INFO, "Don't forget to start me again, otherwise the alligator will bite you :)\n");
	alligator_stop_periodic_timers();
	ipmi_wait_idle();
	nvml_wait_idle();
	dcgm_wait_idle();
	metric_dump(1);
	tls_fs_free();
	puppeteer_done();
	filetailer_shutdown();
	/* Entrypoints first: their shutdown drains uv_run. Aggregators must not be
	 * freed before that drain, or libuv still touches handles inside freed
	 * context_arg (valgrind: Invalid write in uv_run / entrypoint_shutdown). */
	entrypoints_free();
	aggregators_free();
	aggregate_ctx_free();
	file_stat_free(ac->file_stat);
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
