#include "main.h"
#include <unistd.h>
#include <string.h>
aconf *ac;

void ts_initialize()
{
	extern aconf *ac;
	ac->_namespace = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->_namespace);

	namespace_struct *ns = calloc(1, sizeof(*ns));
	ns->key = strdup("default");
	tommy_hashdyn_insert(ac->_namespace, &(ns->node), ns, tommy_strhash_u32(0, ns->key));
	ac->nsdefault = ns;

	ns->metric = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ns->metric);
}

void https_ssl_domains_initialize()
{
	extern aconf *ac;
	ac->https_ssl_domains = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->https_ssl_domains);
}

void config_context_initialize()
{
	extern aconf *ac;
	ac->config_ctx = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->config_ctx);
	config_context *ctx;

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("aggregate");
	ctx->handler = context_aggregate_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("entrypoint");
	ctx->handler = context_entrypoint_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("system");
	ctx->handler = context_system_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));
}

void system_initialize()
{
	extern aconf *ac;
	ac->system_base = 0;
	ac->system_network = 0;
	ac->system_disk = 0;
	ac->system_process = 0;
}

aconf* configuration()
{
	ac = calloc(1, sizeof(*ac));
	ac->aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->uggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->aggregator_startup = 1500;
	ac->aggregator_repeat = 10000;

	ac->iggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->iggregator_startup = 1000;
	ac->iggregator_repeat = 1000;

	ac->unixgram_aggregator = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->unixgram_aggregator);
	ac->unixgram_aggregator_startup = 1000;
	ac->unixgram_aggregator_repeat = 1000;

	ac->system_aggregator = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->system_aggregator);
	ac->system_aggregator_startup = 1000;
	ac->system_aggregator_repeat = 10000;

	ac->process_spawner = calloc(1, sizeof(tommy_hashdyn));
	ac->process_script_dir = "/var/alligator/spawner";
	ac->process_cnt = 0;
	tommy_hashdyn_init(ac->aggregator);
	tommy_hashdyn_init(ac->iggregator);
	tommy_hashdyn_init(ac->process_spawner);

	ac->request_cnt = 0;
	ts_initialize();
	https_ssl_domains_initialize();
	config_context_initialize();
	system_initialize();

	ac->log_level = 0;

	return ac;
}

int main(int argc, char **argv)
{
	ac = configuration();
	uv_loop_t *loop = ac->loop = uv_default_loop();
	signal(SIGPIPE, SIG_IGN);
	general_loop();
	if (argc < 2)
		split_config("/etc/alligator.conf");
	else
		split_config(argv[1]);

	signal_listen();

	tcp_client_handler();
#ifndef __APPLE__
	icmp_client_handler();
#endif
	do_system_scrape(get_system_metrics, "systemmetrics");
	system_scrape_handler();
	process_handler();
	unix_client_handler();
	unixgram_client_handler();
	//get_system_metrics();

	return uv_run(loop, UV_RUN_DEFAULT);
}
