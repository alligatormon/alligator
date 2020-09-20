#include "main.h"
#include <unistd.h>
#include <string.h>
#include "alligator_version.h"
aconf *ac;
//pq_library *pqlib;

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

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("ttl");
	ctx->handler = context_ttl_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("persistence");
	ctx->handler = context_persistence_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("query");
	ctx->handler = context_query_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("lang");
	ctx->handler = context_lang_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("modules");
	ctx->handler = context_modules_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("configuration");
	ctx->handler = context_configuration_parser;
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
	ac->system_carg = calloc(1, sizeof(*ac->system_carg));
	ac->system_carg->ttl = 300;
	ac->system_carg->curr_ttl = -1;
	ac->scs = calloc(1, sizeof(system_cpu_stats));
	ac->system_sysfs = strdup("/sys/");
	ac->system_procfs = strdup("/proc/");
	ac->system_rundir = strdup("/run/");

	ac->process_match = calloc(1, sizeof(match_rules));
	ac->process_match->hash = malloc(sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->process_match->hash);

	ac->packages_match = calloc(1, sizeof(match_rules));
	ac->packages_match->hash = malloc(sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->packages_match->hash);
}

void system_metric_initialize()
{
	extern aconf *ac;
	ac->metric_cache_hits = 0;
	ac->metric_allocates = 0;
	ac->metric_freed = 0;
}

void tcp_server_initialize()
{
	extern aconf *ac;
	ac->tcp_server_handler = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->tcp_server_handler);
}

//void tls_initialize()
//{
//	SSL_library_init();
//	SSL_load_error_strings();
//}

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

	ac->lang_aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->lang_aggregator_startup = 4000;
	ac->lang_aggregator_repeat = 10000;

	ac->modules = calloc(1, sizeof(tommy_hashdyn));

	tommy_hashdyn_init(ac->aggregator);
	tommy_hashdyn_init(ac->tls_aggregator);
	tommy_hashdyn_init(ac->iggregator);
	tommy_hashdyn_init(ac->process_spawner);
	tommy_hashdyn_init(ac->lang_aggregator);
	tommy_hashdyn_init(ac->modules);

	ac->entrypoints = malloc(sizeof(*ac->entrypoints));
	tommy_hashdyn_init(ac->entrypoints);

	ac->request_cnt = 0;
	ts_initialize();
	https_ssl_domains_initialize();
	config_context_initialize();
	aggregate_ctx_init();
	system_initialize();
	system_metric_initialize();
	//tls_initialize();
	tcp_server_initialize();

	ac->log_level = 0;
	ac->ttl = 300;
	ac->persistence_period = 10000;

	return ac;
}

void restore_settings()
{
	if (ac->persistence_dir)
		metric_restore();
}

void print_help()
{
	printf("Alligator is aggregator for system and software metrics. Version: \"%s\".\nUsage: alligator [-h] [/path/to/config].\nDefault config path \"%s\"\nOptions:\n\t-h, --help\tHelp message\n", ALLIGATOR_VERSION, DEFAULT_CONF_DIR);
}

void parse_args(int argc, char **argv)
{
	if (argc < 2)
		return;

	uint64_t i;
	for (i = 1; i < argc; i++)
	{
		if (!strncmp(argv[i], "-h", 2))
		{
			print_help();
			exit(0);
		}
		else if (!strncmp(argv[i], "--help", 6))
		{
			print_help();
			exit(0);
		}
		else
		{
			split_config(argv[1]);
			parse_configs(argv[1]);
		}
	}
}


int main(int argc, char **argv)
{
	ac = configuration();

	uv_loop_t *loop = ac->loop = uv_default_loop();
	signal(SIGPIPE, SIG_IGN);
	general_loop();
	if (argc < 2)
	{
		split_config(DEFAULT_CONF_DIR);
		parse_configs(DEFAULT_CONF_PATH);
	}
	else
		parse_args(argc, argv);

	restore_settings();

	signal_listen();

	//filetailer_handler("/var/log/", dummy_handler);

	tcp_client_handler();
#ifdef __linux__
	icmp_client_handler();
#endif
	do_system_scrape(get_system_metrics, "systemmetrics");
	system_scrape_handler();
	process_handler();
	//unix_client_handler();
	unixgram_client_handler();
	lang_handler();

	//context_arg* carg = calloc(1, sizeof(*carg));
	//carg->loop = loop;

	return uv_run(loop, UV_RUN_DEFAULT);
}
