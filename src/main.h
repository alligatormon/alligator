#pragma once
#define TCPUDP_NET_LENREAD 10000000
#include "platform/platform.h"
#include "dstructures/tommy.h"
#include <uv.h>
//#include <jni.h>
#include "common/jni.h"
#include "metric/labels.h"
#include "events/fs_write.h"
#include "events/uv_alloc.h"
#include "common/selector.h"
#include "events/general.h"
#include "events/a_signal.h"
#include "events/udp.h"
#include "events/server.h"
#include "events/unixgram.h"
#include "events/client.h"
#include "events/process.h"
#include "events/filetailer.h"
#include "parsers/multiparser.h"
#include "metric/namespace.h"
//#include "common/rpm.h"
#include "parsers/mongodb.h"
#include "parsers/postgresql.h"
#include "parsers/mysql.h"
#include "common/aggregator.h"
#define d8 PRId8
#define u16 PRIu16
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
	tommy_hashdyn *_namespace;

	tommy_hashdyn* aggregator;
	tommy_hashdyn* file_aggregator;
	tommy_hashdyn* pg_aggregator;
	tommy_hashdyn* zk_aggregator;
	tommy_hashdyn* mongodb_aggregator;
	tommy_hashdyn* my_aggregator;
	tommy_hashdyn* tls_aggregator;
	int64_t aggregator_startup;
	int64_t aggregator_repeat;
	int64_t file_aggregator_repeat;
	int64_t tls_aggregator_startup;
	int64_t tls_aggregator_repeat;

	tommy_hashdyn* uggregator;
	tommy_hashdyn* udpaggregator;

	tommy_hashdyn* iggregator;
	int64_t iggregator_startup;
	int64_t iggregator_repeat;

	tommy_hashdyn* unixgram_aggregator;
	int64_t unixgram_aggregator_startup;
	int64_t unixgram_aggregator_repeat;

	// SYSTEM METRICS SCRAPE
	tommy_hashdyn* system_aggregator;
	int64_t system_aggregator_startup;
	int64_t system_aggregator_repeat;

	// PROCESS SPAWNER
	tommy_hashdyn* process_spawner; // hashtable with commands
	char *process_script_dir; // dir where store commands into scripts
	int64_t process_cnt; // count process pushed to hashtable

	// LANG SPAWNER
	tommy_hashdyn* lang_aggregator;
	int64_t lang_aggregator_startup;
	int64_t lang_aggregator_repeat;

	// modules paths
	tommy_hashdyn* modules;

	uv_loop_t *loop;
	uint64_t tcp_client_count;
	uint64_t icmp_client_count;
	uint64_t tls_tcp_client_count;

	uint64_t request_cnt;

	// servers hash tables
	tommy_hashdyn* tcp_server_handler;

	// config parser handlers
	tommy_hashdyn* config_ctx;
	tommy_hashdyn* aggregate_ctx;

	// filetailer file list
	tommy_hashdyn* file_stat;

	// local fs x509 cert scraper
	tommy_hashdyn* fs_x509;
	int64_t tls_fs_startup;
	int64_t tls_fs_repeat;

	// local query processing
	tommy_hashdyn* query;
	int64_t query_startup;
	int64_t query_repeat;

	int system_base;
	int system_disk;
	int system_network;
	int system_process;
	int system_vm;
	int system_smart;
	int system_packages;
	int system_firewall;
	int system_cadvisor;
	int system_services;
	char *system_procfs;
	char *system_sysfs;
	char *system_rundir;
	char *system_usrdir;
	char *system_etcdir;
	char *cadvisor_tcpudpbuf;
	tommy_hashdyn* system_userprocess;
	tommy_hashdyn* system_groupprocess;
	uint64_t system_cpuavg_period;
	double system_cpuavg_sum;
	uint64_t system_cpuavg_ptr;
	int system_cpuavg;
	double *system_avg_metrics;
	r_time last_time_cpu;
	pidfile_list *system_pidfile;
#ifdef __linux__
	//rpm_library *rpmlib;
	void *rpmlib;
#endif
	int8_t rpm_readconf;
	context_arg *system_carg;
	system_cpu_stats *scs;
	match_rules *process_match;
	match_rules *packages_match;
	match_rules *services_match;
	tommy_hashdyn* fdesc;

	tommy_hashdyn* entrypoints;
	tommy_hashdyn* aggregators;

	uv_lib_t* libjvm_handle;
	jint (*create_jvm)(JavaVM**, void**, void*);
	JNIEnv *env;

	libmongo *libmongo;
	pq_library *pqlib;
	my_library *mylib;

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
