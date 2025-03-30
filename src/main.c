#include "main.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "resolver/resolver.h"
#include "alligator_version.h"
#include "events/a_signal.h"
#include "x509/type.h"
#include "query/type.h"
#include "events/system_scrape.h"
#include "events/icmp.h"
#include "events/udp.h"
#include "common/aggregator.h"
#include "metric/metric_dump.h"
#include "events/filetailer.h"
#include "config/parser.h"
#include "config/env.h"
#include "events/client.h"
#include "parsers/postgresql.h"
#include "parsers/alligator_mysql.h"
#include "events/filetailer.h"
#include "puppeteer/puppeteer.h"
#include "cluster/type.h"
#include "common/logs.h"
#include "dstructures/ht.h"
#include "system/common.h"

aconf *ac;


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
	ac->tcp_server_handler = calloc(1, sizeof(alligator_ht));
	alligator_ht_init(ac->tcp_server_handler);
}

//void tls_initialize()
//{
//	SSL_library_init();
//	SSL_load_error_strings();
//}

aconf* configuration()
{
	ac = calloc(1, sizeof(*ac));
	ac->aggregator = calloc(1, sizeof(alligator_ht));
	ac->pg_aggregator = calloc(1, sizeof(alligator_ht));
	ac->file_aggregator = calloc(1, sizeof(alligator_ht));
	ac->zk_aggregator = calloc(1, sizeof(alligator_ht));
	ac->my_aggregator = calloc(1, sizeof(alligator_ht));
	ac->uggregator = calloc(1, sizeof(alligator_ht));

	ac->uv_cache_fs = calloc(1, sizeof(tommy_list));
	tommy_list_init(ac->uv_cache_fs);

	ac->uv_cache_timer = calloc(1, sizeof(tommy_list));
	tommy_list_init(ac->uv_cache_timer);

	alligator_ht_init(ac->uggregator);
	ac->aggregator_startup = 1500;
	ac->aggregator_repeat = 10000;
	ac->file_aggregator_repeat = 10000;

	ac->scheduler_startup = 13000;

	ac->udpaggregator = calloc(1, sizeof(alligator_ht));
	alligator_ht_init(ac->udpaggregator);
	ac->scheduler = alligator_ht_init(NULL);

	ac->iggregator = calloc(1, sizeof(alligator_ht));
	ac->iggregator_startup = 1000;
	ac->iggregator_repeat = 10000;
	ac->ping_hash = calloc(1, sizeof(alligator_ht));
	ac->ping_id = 0;

	ac->unixgram_aggregator = calloc(1, sizeof(alligator_ht));
	alligator_ht_init(ac->unixgram_aggregator);
	ac->unixgram_aggregator_startup = 1000;
	ac->unixgram_aggregator_repeat = 1000;

	ac->system_aggregator = alligator_ht_init(NULL);
	ac->system_aggregator_startup = 1000;
	ac->system_aggregator_repeat = 10000;

	ac->process_spawner = calloc(1, sizeof(alligator_ht));
	ac->process_script_dir = "/var/lib/alligator/spawner";

	ac->lang_aggregator = calloc(1, sizeof(alligator_ht));

	ac->fs_x509 = calloc(1, sizeof(alligator_ht));
	ac->tls_fs_startup = 4500;
	ac->tls_fs_repeat = 60000;

	//ac->puppeteer = calloc(1, sizeof(alligator_ht));
	ac->puppeteer = alligator_ht_init(NULL);
	ac->action = calloc(1, sizeof(alligator_ht));
	ac->probe = calloc(1, sizeof(alligator_ht));
	ac->cluster = calloc(1, sizeof(alligator_ht));
	ac->query = alligator_ht_init(NULL);
	ac->query_startup = 5000;
	ac->query_repeat = 10000;

	ac->cluster_startup = 1500;
	ac->cluster_reload = 10000;
	ac->cluster_repeat = 10000;

	ac->modules = alligator_ht_init(NULL);

	ac->file_stat = calloc(1, sizeof(alligator_ht));

	alligator_ht_init(ac->aggregator);
	alligator_ht_init(ac->pg_aggregator);
	alligator_ht_init(ac->file_aggregator);
	alligator_ht_init(ac->zk_aggregator);
	alligator_ht_init(ac->my_aggregator);
	alligator_ht_init(ac->iggregator);
	alligator_ht_init(ac->process_spawner);
	alligator_ht_init(ac->lang_aggregator);
	alligator_ht_init(ac->fs_x509);
	alligator_ht_init(ac->action);
	alligator_ht_init(ac->probe);
	alligator_ht_init(ac->cluster);
	alligator_ht_init(ac->file_stat);
	alligator_ht_init(ac->ping_hash);

	ac->entrypoints = alligator_ht_init(NULL);

	ac->aggregators = alligator_ht_init(NULL);

	ac->resolver = alligator_ht_init(NULL);

	ac->threads = alligator_ht_init(NULL);

	ac->request_cnt = 0;
	ts_initialize();
	aggregate_ctx_init();
	system_initialize();
	system_metric_initialize();
	//tls_initialize();
	tcp_server_initialize();

	ac->log_level = 0;
	ac->ttl = 300;
	ac->persistence_period = 10000;

	ac->metrictree_hashfunc = alligator_ht_strhash;

	setenv("UV_THREADPOOL_SIZE", "4", 1);

	return ac;
}

void main_free()
{
	free(ac->system_carg);
	if (ac->cadvisor_carg)
		free(ac->cadvisor_carg);
	free(ac->log_host);
	free(ac->log_dest);

	alligator_ht_done(ac->tcp_server_handler);
	free(ac->tcp_server_handler);

	alligator_ht_done(ac->aggregator);
	free(ac->aggregator);

	alligator_ht_done(ac->pg_aggregator);
	free(ac->pg_aggregator);

	alligator_ht_done(ac->file_aggregator);
	free(ac->file_aggregator);

	alligator_ht_done(ac->zk_aggregator);
	free(ac->zk_aggregator);

	alligator_ht_done(ac->my_aggregator);
	free(ac->my_aggregator);

	alligator_ht_done(ac->uggregator);
	free(ac->uggregator);

	alligator_ht_done(ac->modules);
	free(ac->modules);

	free(ac->uv_cache_timer);
	free(ac->uv_cache_fs);

	alligator_ht_done(ac->udpaggregator);
	free(ac->udpaggregator);

	alligator_ht_done(ac->iggregator);
	free(ac->iggregator);

	alligator_ht_done(ac->ping_hash);
	free(ac->ping_hash);

	alligator_ht_done(ac->unixgram_aggregator);
	free(ac->unixgram_aggregator);

	alligator_ht_done(ac->system_aggregator);
	free(ac->system_aggregator);

	alligator_ht_done(ac->process_spawner);
	free(ac->process_spawner);

	alligator_ht_done(ac->fs_x509);
	free(ac->fs_x509);

	alligator_ht_done(ac->action);
	free(ac->action);

	alligator_ht_done(ac->probe);
	free(ac->probe);

	alligator_ht_done(ac->file_stat);
	free(ac->file_stat);

	alligator_ht_done(ac->aggregators);
	free(ac->aggregators);

	alligator_ht_done(ac->entrypoints);
	free(ac->entrypoints);

	thread_loop_free();
	alligator_ht_done(ac->threads);
	free(ac->threads);

	free(ac);
}

void restore_settings()
{
	if (ac->persistence_dir)
		metric_restore();

	filestat_restore();
}

void print_help()
{
	printf("Alligator is aggregator for system and software metrics. Version: \"%s\".\nUsage: alligator [-h] [/path/to/config].\nDefault config path \"%s\"\nOptions:\n\t-h, --help\tHelp message\n\t-l <num>\tLog level\n\t-v, --version\tVersion message\n", ALLIGATOR_VERSION, DEFAULT_CONF_DIR);
}

void print_version()
{
	printf("Alligator is aggregator for system and software metrics. Version: \"%s\".\n", ALLIGATOR_VERSION);
}

int parse_args(int argc, char **argv)
{
	int ret = 0;
	if (argc < 2)
		return 0;

	uint64_t i;
	for (i = 1; i < argc; i++)
	{
		if (!strncmp(argv[i], "-h", 2))
		{
			print_help();
			exit(0);
		}
		else if (!strncmp(argv[i], "-v", 2))
		{
			print_version();
			exit(0);
		}
		else if (!strncmp(argv[i], "-l", 2))
		{
			++i;
			if (sisdigit(argv[i]))
				ac->log_level = strtoll(argv[i], NULL, 10);
			else
				ac->log_level = get_log_level_by_name(argv[i], strlen(argv[i]));
		}
		else if (!strncmp(argv[i], "--help", 6))
		{
			print_help();
			exit(0);
		}
		else if (!strncmp(argv[i], "--version", 9))
		{
			print_version();
			exit(0);
		}
		else
		{
			parse_configs(argv[i]);
			ret = 1;
		}
	}

	return ret;
}

int main(int argc, char **argv, char **envp)
{
	ac = configuration();
	log_default();

	uv_loop_t *loop = ac->loop = uv_default_loop();
	signal(SIGPIPE, SIG_IGN);
	signal(SIGABRT, SIG_IGN);
	general_loop();

	if (!parse_args(argc, argv))
		parse_configs(DEFAULT_CONF_PATH);

	parse_env(envp);

	restore_settings();

	log_init();
	glog(L_OFF, "logger started\n");

	signal_listen();

	tcp_client_handler();
	udp_client_handler();
#ifdef __linux__
	icmp_client_handler();
#endif
	do_system_scrape(get_system_metrics, "systemmetrics");
	system_scrape_handler();
	process_handler();
	//unix_client_handler();
	unixgram_client_handler();
	tls_fs_handler();
	query_handler();
	postgresql_client_handler();
	mysql_client_handler();
	filetailer_crawl_handler();
	puppeteer_generator();
	cluster_handler();

	//openssl_r("../trash/pfx/domain.name.pfx", "123456");

	return uv_run(loop, UV_RUN_DEFAULT);
}
