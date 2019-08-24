#include "main.h"
#include <unistd.h>
#include <string.h>
aconf *ac;

void ts_initialize()
{
	// initialize multi NS
	extern aconf *ac;
	ac->_namespace = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->_namespace);

	// initialize default NS
	namespace_struct *ns = calloc(1, sizeof(*ns));
	ns->key = strdup("default");
	tommy_hashdyn_insert(ac->_namespace, &(ns->node), ns, tommy_strhash_u32(0, ns->key));
	ac->nsdefault = ns;

	metric_tree *metrictree = calloc(1, sizeof(*metrictree));
	expire_tree *expiretree = calloc(1, sizeof(*expiretree));
	
	tommy_hashdyn* labels_words_hash = malloc(sizeof(*labels_words_hash));
	tommy_hashdyn_init(labels_words_hash);

	sortplan *sort_plan = malloc(sizeof(*sort_plan));
	sort_plan->plan[0] = "__name__";
	sort_plan->size = 1;

	ns->metrictree = metrictree;
	ns->expiretree = expiretree;
	metrictree->labels_words_hash = labels_words_hash;
	metrictree->sort_plan = sort_plan;
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

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("log_level");
	ctx->handler = context_log_level_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));
}

void system_initialize()
{
	extern aconf *ac;
	ac->system_base = 0;
	ac->system_network = 0;
	ac->system_disk = 0;
	ac->system_process = 0;
	ac->system_vm = 0;
	ac->system_smart = 0;
	ac->process_match = calloc(1, sizeof(match_rules));
	ac->process_match->hash = malloc(sizeof(tommy_hashdyn));
	ac->scs = calloc(1, sizeof(system_cpu_stats));
	tommy_hashdyn_init(ac->process_match->hash);
}

void system_metric_initialize()
{
	extern aconf *ac;
	ac->metric_cache_hits = 0;
	ac->metric_allocates = 0;
	ac->metric_freed = 0;
}

void tls_initialize()
{
	SSL_library_init();
	SSL_load_error_strings();
}

aconf* configuration()
{
	ac = calloc(1, sizeof(*ac));
	ac->aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->uggregator = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->uggregator);
	ac->aggregator_startup = 1500;
	ac->aggregator_repeat = 10000;

	ac->tls_aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->uggregator = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->uggregator);
	ac->tls_aggregator_startup = 1500;
	ac->tls_aggregator_repeat = 10000;

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
	tommy_hashdyn_init(ac->tls_aggregator);
	tommy_hashdyn_init(ac->iggregator);
	tommy_hashdyn_init(ac->process_spawner);

	ac->request_cnt = 0;
	ts_initialize();
	https_ssl_domains_initialize();
	config_context_initialize();
	system_initialize();
	system_metric_initialize();
	tls_initialize();

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
	tls_tcp_client_handler();
#ifdef __linux__
	icmp_client_handler();
#endif
	do_system_scrape(get_system_metrics, "systemmetrics");
	system_scrape_handler();
	process_handler();
	unix_client_handler();
	unixgram_client_handler();

	//cert_check_file("cert.pem");

	return uv_run(loop, UV_RUN_DEFAULT);
}
