#include "main.h"
#include <unistd.h>
#include <string.h>
#include "alligator_version.h"
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
	ac->system_carg->curr_ttl = 0;
	ac->scs = calloc(1, sizeof(system_cpu_stats));
	ac->system_sysfs = strdup("/sys/");
	ac->system_procfs = strdup("/proc/");
	ac->system_rundir = strdup("/run/");
	ac->system_usrdir = strdup("/usr/");
	ac->system_etcdir = strdup("/etc/");
	ac->system_pidfile = calloc(1, sizeof(*ac->system_pidfile));

	ac->process_match = calloc(1, sizeof(match_rules));
	ac->process_match->hash = malloc(sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->process_match->hash);

	ac->packages_match = calloc(1, sizeof(match_rules));
	ac->packages_match->hash = malloc(sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->packages_match->hash);

	ac->services_match = calloc(1, sizeof(match_rules));
	ac->services_match->hash = malloc(sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->services_match->hash);

	ac->system_userprocess = malloc(sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->system_userprocess);

	ac->system_groupprocess = malloc(sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->system_groupprocess);
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
	ac->pg_aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->file_aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->zk_aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->mongodb_aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->my_aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->uggregator = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->uggregator);
	ac->aggregator_startup = 1500;
	ac->aggregator_repeat = 10000;
	ac->file_aggregator_repeat = 300000;

	ac->tls_aggregator = calloc(1, sizeof(tommy_hashdyn));
	//ac->uggregator = calloc(1, sizeof(tommy_hashdyn));
	//tommy_hashdyn_init(ac->uggregator);
	ac->udpaggregator = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->udpaggregator);
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
	ac->process_script_dir = "/var/lib/alligator/spawner";
	ac->process_cnt = 0;

	ac->lang_aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->lang_aggregator_startup = 4000;
	ac->lang_aggregator_repeat = 10000;

	ac->fs_x509 = calloc(1, sizeof(tommy_hashdyn));
	ac->tls_fs_startup = 4500;
	ac->tls_fs_repeat = 60000;

	ac->query = calloc(1, sizeof(tommy_hashdyn));
	ac->query_startup = 5000;
	ac->query_repeat = 10000;

	ac->modules = calloc(1, sizeof(tommy_hashdyn));

	ac->file_stat = calloc(1, sizeof(tommy_hashdyn));

	tommy_hashdyn_init(ac->aggregator);
	tommy_hashdyn_init(ac->pg_aggregator);
	tommy_hashdyn_init(ac->file_aggregator);
	tommy_hashdyn_init(ac->zk_aggregator);
	tommy_hashdyn_init(ac->mongodb_aggregator);
	tommy_hashdyn_init(ac->my_aggregator);
	tommy_hashdyn_init(ac->tls_aggregator);
	tommy_hashdyn_init(ac->iggregator);
	tommy_hashdyn_init(ac->process_spawner);
	tommy_hashdyn_init(ac->lang_aggregator);
	tommy_hashdyn_init(ac->fs_x509);
	tommy_hashdyn_init(ac->query);
	tommy_hashdyn_init(ac->modules);
	tommy_hashdyn_init(ac->file_stat);

	ac->entrypoints = malloc(sizeof(*ac->entrypoints));
	tommy_hashdyn_init(ac->entrypoints);

	ac->aggregators = malloc(sizeof(*ac->aggregators));
	tommy_hashdyn_init(ac->aggregators);

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

	return ac;
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
			ac->log_level = strtoll(argv[i], NULL, 10);
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


int main(int argc, char **argv)
{
	ac = configuration();

	uv_loop_t *loop = ac->loop = uv_default_loop();
	signal(SIGPIPE, SIG_IGN);
	signal(SIGABRT, SIG_IGN);
	general_loop();

	if (!parse_args(argc, argv))
		parse_configs(DEFAULT_CONF_PATH);

	restore_settings();

	signal_listen();

	//filetailer_handler("/var/log/", dummy_handler);

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
	lang_handler();
	tls_fs_handler();
	query_handler();
	postgresql_client_handler();
	mongodb_client_handler();
	mysql_client_handler();
	zk_client_handler();
	filetailer_crawl_handler();
	//udp_send("\0\1sendfile.txt\0netascii\0", 24);
	//udp_send("\0\1sendfile.txt\0octet\0", 21);

	//context_arg* carg = calloc(1, sizeof(*carg));
	//carg->loop = loop;

	return uv_run(loop, UV_RUN_DEFAULT);
}
