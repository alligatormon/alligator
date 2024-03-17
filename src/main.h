#pragma once
#define TCPUDP_NET_LENREAD 10000000
#include "dstructures/tommy.h"
#include "dstructures/ht.h"
#include <uv.h>
#include "common/jni.h"
#include "metric/labels.h"
#include "events/fs_write.h"
#include "events/uv_alloc.h"
#include "common/selector.h"
#include "events/general.h"
#include "events/udp.h"
#include "events/server.h"
#include "events/unixgram.h"
#include "events/client.h"
#include "events/process.h"
#include "events/filetailer.h"
#include "metric/namespace.h"
#include "parsers/postgresql.h"
#include "parsers/alligator_mysql.h"
#include "common/aggregator.h"
#include "dstructures/uv_cache.h"
#include "resolver/resolver.h"
#define d8 PRId8
#define u8 PRIu8
#define u16 PRIu16
#define u32 PRIu32
#define d64 PRId64
#define u64 PRIu64
#define METRIC_SIZE 1000
#ifdef __FreeBSD__
#define DEFAULT_CONF_DIR "/usr/local/etc/alligator.conf"
#define DEFAULT_CONF_PATH "/usr/local/etc/alligator"
#else
#define DEFAULT_CONF_DIR "/etc/alligator.conf"
#define DEFAULT_CONF_PATH "/etc/alligator"
#endif

#define	ASTDIN_FILENO	0
#define	ASTDOUT_FILENO	1
#define	ASTDERR_FILENO	2

typedef struct system_cpu_cores_stats
{
	uint64_t usage;
	uint64_t user;
	uint64_t nice;
	uint64_t system;
	uint64_t idle;
	uint64_t iowait;
	uint64_t total;
} system_cpu_cores_stats;

typedef struct system_cpu_stats
{
	system_cpu_cores_stats *cores;
	system_cpu_cores_stats cgroup;
	system_cpu_cores_stats hw;
} system_cpu_stats;

typedef struct pidfile_node {
	char *pidfile;
	int type;
	struct pidfile_node *next;
} pidfile_node;

typedef struct pidfile_list {
	pidfile_node *head;
	pidfile_node *tail;
} pidfile_list;


typedef struct userprocess_node {
	uid_t uid;
	char *name;

	tommy_node node;
} userprocess_node;

typedef struct aconf
{
	namespace_struct *nsdefault;
	alligator_ht *_namespace;

	alligator_ht* aggregator;
	alligator_ht* file_aggregator;
	alligator_ht* pg_aggregator;
	alligator_ht* zk_aggregator;
	alligator_ht* my_aggregator;
	alligator_ht* tls_aggregator;
	alligator_ht* scheduler;
	int64_t aggregator_startup;
	int64_t aggregator_repeat;
	int64_t file_aggregator_repeat;
	int64_t tls_aggregator_startup;
	int64_t tls_aggregator_repeat;
	int64_t scheduler_startup;
	uv_timer_t tcp_client_timer;
	uv_timer_t pg_timer;
	uv_timer_t my_timer;
	uv_timer_t zk_timer;

	alligator_ht* uggregator;
	alligator_ht* udpaggregator;

	uv_timer_t udp_client_timer;
	uv_timer_t udgregator_timer;

	alligator_ht* iggregator;
	int64_t iggregator_startup;
	int64_t iggregator_repeat;
	uv_timer_t icmp_client_timer;

	alligator_ht* unixgram_aggregator;
	int64_t unixgram_aggregator_startup;
	int64_t unixgram_aggregator_repeat;

	alligator_ht* resolver;
	resolver_data *srv_resolver[255];
	uint8_t resolver_size;
	int64_t resolver_max_ttl;
	int64_t resolver_replace_ttl;

	// SYSTEM METRICS SCRAPE
	alligator_ht* system_aggregator;
	int64_t system_aggregator_startup;
	int64_t system_aggregator_repeat;
	uv_timer_t system_scrape_timer_general;
	uv_timer_t system_scrape_timer_fast;
	uv_timer_t system_scrape_timer_slow;

	// PROCESS SPAWNER
	alligator_ht* process_spawner; // hashtable with commands
	char *process_script_dir; // dir where store commands into scripts
	uv_timer_t process_timer;

	// LANG SPAWNER
	alligator_ht* lang_aggregator;

	// modules paths
	alligator_ht* modules;

	uv_loop_t *loop;
	uint64_t tcp_client_count;
	uint64_t icmp_client_count;
	uint64_t ping_id;
	uint64_t tls_tcp_client_count;

	uint64_t request_cnt;

	// servers hash tables
	alligator_ht* tcp_server_handler;

	// config parser handlers
	alligator_ht* config_ctx;
	alligator_ht* aggregate_ctx;

	// filetailer file list
	alligator_ht* file_stat;
	uv_timer_t filetailer_timer;

	// local fs x509 cert scraper
	alligator_ht* fs_x509;
	int64_t tls_fs_startup;
	int64_t tls_fs_repeat;
	uv_timer_t tls_fs_timer;

	alligator_ht* puppeteer;
	uv_timer_t puppeteer_timer;

	// local query processing
	alligator_ht* action;
	alligator_ht* query;
	alligator_ht* probe;
	alligator_ht* cluster;
	int64_t query_startup;
	int64_t query_repeat;
	uv_timer_t query_timer;
	int64_t cluster_startup;
	int64_t cluster_repeat;
	uv_timer_t cluster_timer;
	tommy_list *uv_cache_timer;
	tommy_list *uv_cache_fs;

	int system_base;
	int system_disk;
	int system_network;
	int system_process;
	int system_smart;
	int system_packages;
	int system_firewall;
	int system_ipset;
	int system_ipset_entries;
	int system_cadvisor;
	int system_services;
	char *system_procfs;
	char *system_sysfs;
	char *system_rundir;
	char *system_usrdir;
	char *system_etcdir;
	char *cadvisor_tcpudpbuf;
	alligator_ht* system_userprocess;
	alligator_ht* system_groupprocess;
	alligator_ht* system_sysctl;
	uint64_t system_cpuavg_period;
	double system_cpuavg_sum;
	uint64_t system_cpuavg_ptr;
	int system_cpuavg;
	double *system_avg_metrics;
	r_time last_time_cpu;
	pidfile_list *system_pidfile;
	context_arg *system_carg;
	system_cpu_stats *scs;
	match_rules *process_match;
	match_rules *packages_match;
	match_rules *services_match;
	alligator_ht* fdesc;
	alligator_ht* ping_hash;

	alligator_ht* entrypoints;
	alligator_ht* aggregators;

	uv_lib_t* libjvm_handle;
	jint (*create_jvm)(JavaVM**, void**, void*);
	JNIEnv *env;

	pq_library *pqlib;
	my_library *mylib;
	void *pylib;

	uv_timer_t general_timer;
	uv_timer_t expire_timer;
	uv_timer_t dump_timer;

	int log_level; // 0 - no logs, 1 - err only, 2 - all queries logging, 3 - verbosity
	int64_t ttl; // global TTL for metrics

	// persistence settings
	char* persistence_dir;
	uint64_t persistence_period;

	// metrics
	uint64_t metric_cache_hits;
	uint64_t metric_allocates;
	uint64_t metric_freed;
} aconf;

extern aconf *ac;

void get_system_metrics();
void system_fast_scrape();
void system_slow_scrape();
