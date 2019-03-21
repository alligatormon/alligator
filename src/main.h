#pragma once
#include "platform/platform.h"
#include "dstructures/tommy.h"
#include <uv.h>
#include "dstructures/metric.h"
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
#define d64 PRId64
#define u64 PRIu64
#define METRIC_SIZE 1000
typedef struct aconf
{
	namespace_struct *nsdefault;
	tommy_hashdyn *_namespace;

	tommy_hashdyn* aggregator;
	int64_t aggregator_startup;
	int64_t aggregator_repeat;

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

	uint64_t request_cnt;

	// HTTPS TLS CHECK OBJECTS
	tommy_hashdyn *https_ssl_domains;

	// config parser handlers
	tommy_hashdyn* config_ctx;

	int system_base;
	int system_disk;
	int system_network;
	int system_process;

	int log_level; // 0 - no logs, 1 - err only, 2 - all queries logging, 3 - verbosity
} aconf;

void get_system_metrics();
