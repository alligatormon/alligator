#pragma once
#include "platform/platform.h"
#include "dstructures/tommy.h"
#include <uv.h>
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
#include "config/context.h"
#include "parsers/multiparser.h"
#include "metric/namespace.h"
#include "common/rpm.h"
#define d8 PRId8
#define u16 PRIu16
#define d64 PRId64
#define u64 PRIu64
#define METRIC_SIZE 1000

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

typedef struct aconf
{
	namespace_struct *nsdefault;
	tommy_hashdyn *_namespace;

	tommy_hashdyn* aggregator;
	tommy_hashdyn* tls_aggregator;
	int64_t aggregator_startup;
	int64_t aggregator_repeat;
	int64_t tls_aggregator_startup;
	int64_t tls_aggregator_repeat;

	tommy_hashdyn* uggregator;

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
	uv_loop_t *loop;
	uint64_t tcp_client_count;
	uint64_t icmp_client_count;
	uint64_t tls_tcp_client_count;

	uint64_t request_cnt;

	// servers hash tables
	tommy_hashdyn* tcp_server_handler;

	// HTTPS TLS CHECK OBJECTS
	tommy_hashdyn *https_ssl_domains;

	// config parser handlers
	tommy_hashdyn* config_ctx;

	int system_base;
	int system_disk;
	int system_network;
	int system_process;
	int system_vm;
	int system_smart;
	int system_packages;
	int system_firewall;
	rpm_library *rpmlib;
	int8_t rpm_readconf;
	context_arg *system_carg;
	system_cpu_stats *scs;
	match_rules *process_match;
	tommy_hashdyn* fdesc;

	int log_level; // 0 - no logs, 1 - err only, 2 - all queries logging, 3 - verbosity
	int64_t ttl; // TTL for metrics

	// persistence settings
	char* persistence_dir;
	uint64_t persistence_period;

	// metrics
	uint64_t metric_cache_hits;
	uint64_t metric_allocates;
	uint64_t metric_freed;
} aconf;

void get_system_metrics();
void system_fast_scrape();
void system_slow_scrape();
