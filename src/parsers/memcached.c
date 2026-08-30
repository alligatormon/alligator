#include <stdio.h>
#include <string.h>
#include <fnmatch.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "parsers/multiparser.h"
#include "metric/metric_types.h"
#include "query/type.h"
#include "common/logs.h"
#include "main.h"
#define MC_NAME_SIZE 255

void memcached_query(char *metrics, size_t size, context_arg *carg)
{
	carglog(carg, L_TRACE, "memcached query result (%zu):\n'%s'\n", size, metrics);

	char metricname[MC_NAME_SIZE];
	char metricvalue[MC_NAME_SIZE];
	for (uint64_t i = 0; i < size; i++)
	{
		uint64_t endline = strcspn(metrics + i, "\r\n");
		uint64_t cur = i;
		cur += strcspn(metrics + cur, " \n");
		cur += strspn(metrics + cur, " \n");
		uint64_t copysize = strcspn(metrics + cur, " \n");
		if (!copysize)
		{
			carglog(carg, L_ERROR, "memcached metric name size is 0, error\n");
			break;
		}
		//printf("cur = %d, copysize = %d\n", cur, copysize);

		size_t metricname_copy = copysize < (sizeof(metricname) - 1) ? copysize : (sizeof(metricname) - 1);
		strlcpy(metricname, metrics + cur, metricname_copy + 1);
		carglog(carg, L_DEBUG, "metric name is %s\n", metricname);

		metric_name_normalizer(metricname, copysize);

		i += endline;
		i += strspn(metrics + i, "\r\n");
		endline = strcspn(metrics + i, "\r\n");
		cur = i;

		copysize = strcspn(metrics + cur, "\r");
		if (!copysize)
		{
			carglog(carg, L_WARN, "metric value size is 0, error\n");
			break;
		}
		//printf("cur = %d, copysize = %d\n", cur, copysize);
		size_t metricvalue_copy = copysize < (sizeof(metricvalue) - 1) ? copysize : (sizeof(metricvalue) - 1);
		strlcpy(metricvalue, metrics + cur, metricvalue_copy + 1);
		carglog(carg, L_DEBUG, "metric value is %s\n", metricvalue);

		if (metric_value_validator(metricvalue, copysize-1))
		{
			double mval = strtod(metricvalue, NULL);
			namespace_metric_family_set(NULL, carg, metricname, METRIC_TYPE_GAUGE, "Memcached query-derived numeric value.");
			metric_add_auto(metricname, &mval, DATATYPE_DOUBLE, carg);
		}
		else
		{
			multicollector(NULL, metricvalue, copysize, carg);
		}

		i += endline;
		i += strspn(metrics + i, "\r\n");
	}
	carg->parser_status = 1;
}

void memcached_cachedump(char *metrics, size_t size, context_arg *carg)
{
	string *get_query = string_new();
	string_cat(get_query, "get", 3);
	query_node *qn = carg->data;
	uint64_t copysize;
	char field[MC_NAME_SIZE];
	char metric[MC_NAME_SIZE];

	size_t expr_size = strlen(qn->expr);
	size_t pattern_size = selector_count_field(qn->expr, " \t", expr_size);
	char pattern[pattern_size][MC_NAME_SIZE];
	uint64_t j = 0;
	for (uint64_t i = 5; i < expr_size; j++)
	{
		i += strspn(qn->expr + i, " \t");
		uint64_t endfield = strcspn(qn->expr + i, " \t");
		size_t pattern_copy = endfield < (sizeof(pattern[j]) - 1) ? endfield : (sizeof(pattern[j]) - 1);
		strlcpy(pattern[j], qn->expr + i, pattern_copy + 1);

		i += endfield;
		i += strspn(qn->expr + i, " \t");
	}

	pattern_size = j;

	for (uint64_t i = 0; i < size;)
	{
		uint64_t endline = strcspn(metrics + i, "\r\n");
		uint64_t cur = i;

		size_t field_copy = endline < (sizeof(field) - 1) ? endline : (sizeof(field) - 1);
		strlcpy(field, metrics + cur, field_copy + 1);
		// ITEM third_metric
		if (!strncmp(field, "ITEM", 4))
		{
			carglog(carg, L_DEBUG, "memcached cachedump: ITEM line\n");

			copysize = strcspn(field + 5, " \t\n");
			size_t metric_copy = copysize < (sizeof(metric) - 1) ? copysize : (sizeof(metric) - 1);
			strlcpy(metric, field + 5, metric_copy + 1);
			for (uint64_t j = 0; j < pattern_size; j++)
			{
				if (!fnmatch(pattern[j], metric, 0))
				{
					carglog(carg, L_DEBUG, "matching key '%s' and pattern '%s': OK\n", metric, pattern[j]);
					string_cat(get_query, " ", 1);
					string_cat(get_query, field + 5, copysize);
					break;
				}
				else
					carglog(carg, L_DEBUG, "matching key '%s' and pattern '%s': no match\n", metric, pattern[j]);
			}
		}

		i += endline;
		i += strspn(metrics + i, "\r\n");
	}

	string_cat(get_query, "\r\n", 2);

	char *key = malloc(512);
	snprintf(key, 511, "memcached_query(tcp://%s:%u)/%s", carg->host, htons(carg->remote_addr.sin_port), qn->expr);

	carglog(carg, L_DEBUG, "memcached glob get query is\n'%s'\nkey '%s'\n", get_query->s, key);
	try_again(carg, get_query->s, get_query->l, memcached_query, "memcached_query", NULL, key, carg->data);
	free(get_query);
	carg->parser_status = 1;
}

void memcached_stats_items(char *metrics, size_t size, context_arg *carg)
{
	string *slab_query = string_new();
	char field[MC_NAME_SIZE];
	query_node *qn = carg->data;

	for (uint64_t i = 0; i < size;)
	{
		uint64_t endline = strcspn(metrics + i, "\r\n");
		uint64_t cur = i;

		size_t field_copy = endline < (sizeof(field) - 1) ? endline : (sizeof(field) - 1);
		strlcpy(field, metrics + cur, field_copy + 1);
		char *number_str = NULL;
		if ((number_str = strstr(field, ":number")))
		{
			uint64_t slab_id = strtoull(field + 11, NULL, 10);
			uint64_t number = strtoull(number_str + 8, NULL, 10);
			string_cat(slab_query, "stats cachedump ", 16);
			string_uint(slab_query, slab_id);
			string_cat(slab_query, " ", 1);
			string_uint(slab_query, number);
			string_cat(slab_query, "\r\n", 2);
		}

		i += endline;
		i += strspn(metrics + i, "\r\n");
	}

	char *key = malloc(512);
	snprintf(key, 511, "memcached_cachedump(tcp://%s:%u)/%s", carg->host, htons(carg->remote_addr.sin_port), qn->expr);

	carglog(carg, L_DEBUG, "query is\n'%s'\nkey '%s'\n", slab_query->s, key);
	try_again(carg, slab_query->s, slab_query->l, memcached_cachedump, "memcached_cachedump", NULL, key, carg->data);
	free(slab_query);
	carg->parser_status = 1;
}

void memcached_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	query_node *qn = arg;

	carglog(carg, L_DEBUG, "run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);

	uint64_t writelen = 0;
	char *write_comm = NULL;
	void *func = NULL;
	char *funcname = NULL;
	void *data;
	if (!strncmp(qn->expr, "glob", 4))
	{
		writelen = strlen("stats items\r\n");
		write_comm = malloc(writelen + 1);
		strlcpy(write_comm, "stats items\r\n", writelen + 1);
		func = memcached_stats_items;
		funcname = "memcached_stats_items";
		data = qn;
	}
	else
	{
		writelen = strlen(qn->expr) + 2; // "get KEY1 KEY2 KEY3\r\n"
		write_comm = malloc(writelen + 1);
		strlcpy(write_comm, qn->expr, writelen - 1);
		strcat(write_comm, "\r\n");
		func = memcached_query;
		funcname = "memcached_query";
		data = carg->data;
	}

	char *key = malloc(255);
	snprintf(key, 255, "(tcp://%s:%u)/%s", carg->host, htons(carg->remote_addr.sin_port), qn->expr);
	size_t key_len = strlen(key);
	if (key_len)
		key[key_len - 1] = 0;

	try_again(carg, write_comm, writelen, func, funcname, NULL, key, data);
}

enum {
	MC_SKIP = 0,
	MC_AUTO,
	MC_L1,
	MC_L2,
	MC_ACCUM_CMD_SET,
	MC_ACCUM_CAS
};

typedef struct memcached_stat_rule {
	const char *stat_key;
	const char *family;
	uint8_t mtype;
	uint8_t emit;
	const char *l1n;
	const char *l1v;
	const char *l2n;
	const char *l2v;
} memcached_stat_rule;

static const memcached_stat_rule memcached_stat_rules[] = {
	{ "pid", NULL, 0, MC_SKIP, NULL, NULL, NULL, NULL },
	{ "version", NULL, 0, MC_SKIP, NULL, NULL, NULL, NULL },
	{ "libevent", NULL, 0, MC_SKIP, NULL, NULL, NULL, NULL },
	{ "cmd_get", NULL, 0, MC_SKIP, NULL, NULL, NULL, NULL },
	{ "cmd_touch", NULL, 0, MC_SKIP, NULL, NULL, NULL, NULL },

	{ "get_hits", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "get", "status", "hit" },
	{ "get_misses", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "get", "status", "miss" },
	{ "get_expired", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "get", "status", "expired" },
	{ "get_flushed", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "get", "status", "flushed" },
	{ "delete_hits", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "delete", "status", "hit" },
	{ "delete_misses", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "delete", "status", "miss" },
	{ "incr_hits", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "incr", "status", "hit" },
	{ "incr_misses", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "incr", "status", "miss" },
	{ "decr_hits", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "decr", "status", "hit" },
	{ "decr_misses", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "decr", "status", "miss" },
	{ "touch_hits", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "touch", "status", "hit" },
	{ "touch_misses", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "touch", "status", "miss" },
	{ "cas_hits", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_ACCUM_CAS, "command", "cas", "status", "hit" },
	{ "cas_misses", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_ACCUM_CAS, "command", "cas", "status", "miss" },
	{ "cas_badval", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_ACCUM_CAS, "command", "cas", "status", "badval" },
	{ "cmd_flush", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "flush", "status", "hit" },
	{ "cmd_meta", "memcached_commands_total", METRIC_TYPE_COUNTER, MC_L2, "command", "meta", "status", "hit" },
	{ "cmd_set", NULL, METRIC_TYPE_COUNTER, MC_ACCUM_CMD_SET, NULL, NULL, NULL, NULL },

	{ "bytes_read", "memcached_read_bytes_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "bytes_written", "memcached_written_bytes_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "total_connections", "memcached_connections_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "conn_yields", "memcached_connections_yielded_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "listen_disabled_num", "memcached_connections_listener_disabled_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "rejected_connections", "memcached_connections_rejected_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "total_items", "memcached_items_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "evictions", "memcached_items_evicted_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "reclaimed", "memcached_items_reclaimed_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "evicted_unfetched", "memcached_items_evicted_unfetched_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "expired_unfetched", "memcached_items_expired_unfetched_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "evicted_active", "memcached_items_evicted_active_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "direct_reclaims", "memcached_direct_reclaims_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "crawler_items_checked", "memcached_lru_crawler_items_checked_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "crawler_reclaimed", "memcached_lru_crawler_reclaimed_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "lru_crawler_starts", "memcached_lru_crawler_starts_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "moves_to_cold", "memcached_lru_crawler_moves_to_cold_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "moves_to_warm", "memcached_lru_crawler_moves_to_warm_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "moves_within_lru", "memcached_lru_crawler_moves_within_lru_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "lru_maintainer_juggles", "memcached_lru_maintainer_juggles_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "lru_bumps_dropped", "memcached_lru_bumps_dropped_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "lrutail_reflocked", "memcached_lrutail_reflocked_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "malloc_fails", "memcached_malloc_fails_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "uptime", "memcached_uptime_seconds", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "rusage_user", "memcached_rusage_seconds_total", METRIC_TYPE_COUNTER, MC_L1, "type", "user", NULL, NULL },
	{ "rusage_system", "memcached_rusage_seconds_total", METRIC_TYPE_COUNTER, MC_L1, "type", "system", NULL, NULL },
	{ "time_in_listen_disabled_us", "memcached_listen_disabled_microseconds_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "auth_cmds", "memcached_auth_commands_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "auth_errors", "memcached_auth_errors_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "slab_reassign_busy_deletes", "memcached_slab_reassign_total", METRIC_TYPE_COUNTER, MC_L1, "event", "busy_deletes", NULL, NULL },
	{ "slab_reassign_busy_items", "memcached_slab_reassign_total", METRIC_TYPE_COUNTER, MC_L1, "event", "busy_items", NULL, NULL },
	{ "slab_reassign_chunk_rescues", "memcached_slab_reassign_total", METRIC_TYPE_COUNTER, MC_L1, "event", "chunk_rescues", NULL, NULL },
	{ "slab_reassign_evictions_nomem", "memcached_slab_reassign_total", METRIC_TYPE_COUNTER, MC_L1, "event", "evictions_nomem", NULL, NULL },
	{ "slab_reassign_inline_reclaim", "memcached_slab_reassign_total", METRIC_TYPE_COUNTER, MC_L1, "event", "inline_reclaim", NULL, NULL },
	{ "slab_reassign_rescues", "memcached_slab_reassign_total", METRIC_TYPE_COUNTER, MC_L1, "event", "rescues", NULL, NULL },
	{ "slabs_moved", "memcached_slabs_moved_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "log_watcher_sent", "memcached_log_events_total", METRIC_TYPE_COUNTER, MC_L2, "component", "watcher", "result", "sent" },
	{ "log_watcher_skipped", "memcached_log_events_total", METRIC_TYPE_COUNTER, MC_L2, "component", "watcher", "result", "skipped" },
	{ "log_worker_dropped", "memcached_log_events_total", METRIC_TYPE_COUNTER, MC_L2, "component", "worker", "result", "dropped" },
	{ "log_worker_written", "memcached_log_events_total", METRIC_TYPE_COUNTER, MC_L2, "component", "worker", "result", "written" },
	{ "ssl_handshake_errors", "memcached_ssl_handshake_errors_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "read_buf_oom", "memcached_read_buf_oom_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "response_obj_oom", "memcached_response_obj_oom_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "unexpected_napi_ids", "memcached_unexpected_napi_ids_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "round_robin_fallback", "memcached_round_robin_fallback_total", METRIC_TYPE_COUNTER, MC_AUTO, NULL, NULL, NULL, NULL },

	{ "curr_connections", "memcached_current_connections", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "max_connections", "memcached_max_connections", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "accepting_conns", "memcached_accepting_connections", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "connection_structures", "memcached_connection_structures", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "reserved_fds", "memcached_reserved_fds", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "bytes", "memcached_current_bytes", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "limit_maxbytes", "memcached_limit_bytes", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "curr_items", "memcached_current_items", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "threads", "memcached_threads", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "pointer_size", "memcached_pointer_size", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "hash_bytes", "memcached_hash_bytes", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "hash_power_level", "memcached_hash_power_level", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "hash_is_expanding", "memcached_hash_is_expanding", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "lru_crawler_running", "memcached_lru_crawler_running", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "slab_reassign_running", "memcached_slab_reassign_running", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "slab_global_page_pool", "memcached_slab_global_page_pool", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "time", "memcached_time_seconds", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
	{ "read_buf_bytes", "memcached_read_buf", METRIC_TYPE_GAUGE, MC_L1, "kind", "bytes", NULL, NULL },
	{ "read_buf_bytes_free", "memcached_read_buf", METRIC_TYPE_GAUGE, MC_L1, "kind", "bytes_free", NULL, NULL },
	{ "read_buf_count", "memcached_read_buf", METRIC_TYPE_GAUGE, MC_L1, "kind", "count", NULL, NULL },
	{ "response_obj_bytes", "memcached_response_obj", METRIC_TYPE_GAUGE, MC_L1, "kind", "bytes", NULL, NULL },
	{ "response_obj_count", "memcached_response_obj", METRIC_TYPE_GAUGE, MC_L1, "kind", "count", NULL, NULL },
	{ "time_since_server_cert_refresh", "memcached_time_since_server_cert_refresh_seconds", METRIC_TYPE_GAUGE, MC_AUTO, NULL, NULL, NULL, NULL },
};

static const memcached_stat_rule *memcached_find_rule(const char *stat_key)
{
	size_t n = sizeof(memcached_stat_rules) / sizeof(memcached_stat_rules[0]);
	for (size_t i = 0; i < n; i++)
	{
		if (!strcmp(memcached_stat_rules[i].stat_key, stat_key))
			return &memcached_stat_rules[i];
	}
	return NULL;
}

static void memcached_register_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "memcached_commands_total", METRIC_TYPE_COUNTER, "Memcached command counters by command and status.");
	namespace_metric_family_set(NULL, carg, "memcached_read_bytes_total", METRIC_TYPE_COUNTER, "Total bytes read by memcached from the network.");
	namespace_metric_family_set(NULL, carg, "memcached_written_bytes_total", METRIC_TYPE_COUNTER, "Total bytes written by memcached to the network.");
	namespace_metric_family_set(NULL, carg, "memcached_connections_total", METRIC_TYPE_COUNTER, "Total connections opened since memcached start.");
	namespace_metric_family_set(NULL, carg, "memcached_connections_yielded_total", METRIC_TYPE_COUNTER, "Connections yielded due to the -R limit.");
	namespace_metric_family_set(NULL, carg, "memcached_connections_listener_disabled_total", METRIC_TYPE_COUNTER, "Times the listener was disabled after hitting the connection limit.");
	namespace_metric_family_set(NULL, carg, "memcached_connections_rejected_total", METRIC_TYPE_COUNTER, "Rejected connections.");
	namespace_metric_family_set(NULL, carg, "memcached_items_total", METRIC_TYPE_COUNTER, "Total items stored during the life of this instance.");
	namespace_metric_family_set(NULL, carg, "memcached_items_evicted_total", METRIC_TYPE_COUNTER, "Items evicted to free memory for new items.");
	namespace_metric_family_set(NULL, carg, "memcached_items_reclaimed_total", METRIC_TYPE_COUNTER, "Entries stored using memory from an expired entry.");
	namespace_metric_family_set(NULL, carg, "memcached_items_evicted_unfetched_total", METRIC_TYPE_COUNTER, "Items evicted that were never fetched.");
	namespace_metric_family_set(NULL, carg, "memcached_items_expired_unfetched_total", METRIC_TYPE_COUNTER, "Expired items that were never fetched.");
	namespace_metric_family_set(NULL, carg, "memcached_items_evicted_active_total", METRIC_TYPE_COUNTER, "Active items that were evicted.");
	namespace_metric_family_set(NULL, carg, "memcached_direct_reclaims_total", METRIC_TYPE_COUNTER, "Direct reclaim operations.");
	namespace_metric_family_set(NULL, carg, "memcached_lru_crawler_items_checked_total", METRIC_TYPE_COUNTER, "Items examined by the LRU crawler.");
	namespace_metric_family_set(NULL, carg, "memcached_lru_crawler_reclaimed_total", METRIC_TYPE_COUNTER, "Items freed by the LRU crawler.");
	namespace_metric_family_set(NULL, carg, "memcached_lru_crawler_starts_total", METRIC_TYPE_COUNTER, "Times an LRU crawler was started.");
	namespace_metric_family_set(NULL, carg, "memcached_lru_crawler_moves_to_cold_total", METRIC_TYPE_COUNTER, "Items moved from HOT/WARM to COLD LRU.");
	namespace_metric_family_set(NULL, carg, "memcached_lru_crawler_moves_to_warm_total", METRIC_TYPE_COUNTER, "Items moved from COLD to WARM LRU.");
	namespace_metric_family_set(NULL, carg, "memcached_lru_crawler_moves_within_lru_total", METRIC_TYPE_COUNTER, "Items reshuffled within HOT or WARM LRU.");
	namespace_metric_family_set(NULL, carg, "memcached_lru_maintainer_juggles_total", METRIC_TYPE_COUNTER, "LRU maintainer juggle operations.");
	namespace_metric_family_set(NULL, carg, "memcached_lru_bumps_dropped_total", METRIC_TYPE_COUNTER, "Dropped LRU bumps.");
	namespace_metric_family_set(NULL, carg, "memcached_lrutail_reflocked_total", METRIC_TYPE_COUNTER, "Times the LRU tail was ref-locked.");
	namespace_metric_family_set(NULL, carg, "memcached_malloc_fails_total", METRIC_TYPE_COUNTER, "Malloc failures.");
	namespace_metric_family_set(NULL, carg, "memcached_uptime_seconds", METRIC_TYPE_COUNTER, "Seconds since the memcached server started.");
	namespace_metric_family_set(NULL, carg, "memcached_rusage_seconds_total", METRIC_TYPE_COUNTER, "CPU time consumed by memcached by type.");
	namespace_metric_family_set(NULL, carg, "memcached_listen_disabled_microseconds_total", METRIC_TYPE_COUNTER, "Microseconds spent with the listener disabled.");
	namespace_metric_family_set(NULL, carg, "memcached_auth_commands_total", METRIC_TYPE_COUNTER, "Authentication commands processed.");
	namespace_metric_family_set(NULL, carg, "memcached_auth_errors_total", METRIC_TYPE_COUNTER, "Authentication errors.");
	namespace_metric_family_set(NULL, carg, "memcached_slab_reassign_total", METRIC_TYPE_COUNTER, "Slab reassign events by type.");
	namespace_metric_family_set(NULL, carg, "memcached_slabs_moved_total", METRIC_TYPE_COUNTER, "Slab pages moved.");
	namespace_metric_family_set(NULL, carg, "memcached_log_events_total", METRIC_TYPE_COUNTER, "Logging subsystem events by component and result.");
	namespace_metric_family_set(NULL, carg, "memcached_ssl_handshake_errors_total", METRIC_TYPE_COUNTER, "TLS handshake errors.");
	namespace_metric_family_set(NULL, carg, "memcached_read_buf_oom_total", METRIC_TYPE_COUNTER, "Read buffer out-of-memory events.");
	namespace_metric_family_set(NULL, carg, "memcached_response_obj_oom_total", METRIC_TYPE_COUNTER, "Response object out-of-memory events.");
	namespace_metric_family_set(NULL, carg, "memcached_unexpected_napi_ids_total", METRIC_TYPE_COUNTER, "Unexpected NAPI id events.");
	namespace_metric_family_set(NULL, carg, "memcached_round_robin_fallback_total", METRIC_TYPE_COUNTER, "Round-robin fallback events.");

	namespace_metric_family_set(NULL, carg, "memcached_current_connections", METRIC_TYPE_GAUGE, "Current open connections.");
	namespace_metric_family_set(NULL, carg, "memcached_max_connections", METRIC_TYPE_GAUGE, "Configured maximum connections.");
	namespace_metric_family_set(NULL, carg, "memcached_accepting_connections", METRIC_TYPE_GAUGE, "Whether memcached is accepting new connections.");
	namespace_metric_family_set(NULL, carg, "memcached_connection_structures", METRIC_TYPE_GAUGE, "Number of connection structures allocated.");
	namespace_metric_family_set(NULL, carg, "memcached_reserved_fds", METRIC_TYPE_GAUGE, "File descriptors reserved for internal use.");
	namespace_metric_family_set(NULL, carg, "memcached_current_bytes", METRIC_TYPE_GAUGE, "Current bytes used to store items.");
	namespace_metric_family_set(NULL, carg, "memcached_limit_bytes", METRIC_TYPE_GAUGE, "Maximum bytes allowed for item storage.");
	namespace_metric_family_set(NULL, carg, "memcached_current_items", METRIC_TYPE_GAUGE, "Current number of items stored.");
	namespace_metric_family_set(NULL, carg, "memcached_threads", METRIC_TYPE_GAUGE, "Number of worker threads.");
	namespace_metric_family_set(NULL, carg, "memcached_pointer_size", METRIC_TYPE_GAUGE, "Host pointer size in bits.");
	namespace_metric_family_set(NULL, carg, "memcached_hash_bytes", METRIC_TYPE_GAUGE, "Bytes used by the hash table.");
	namespace_metric_family_set(NULL, carg, "memcached_hash_power_level", METRIC_TYPE_GAUGE, "Current hash table power level.");
	namespace_metric_family_set(NULL, carg, "memcached_hash_is_expanding", METRIC_TYPE_GAUGE, "Whether the hash table is expanding.");
	namespace_metric_family_set(NULL, carg, "memcached_lru_crawler_running", METRIC_TYPE_GAUGE, "Whether the LRU crawler is currently running.");
	namespace_metric_family_set(NULL, carg, "memcached_slab_reassign_running", METRIC_TYPE_GAUGE, "Whether slab reassign is currently running.");
	namespace_metric_family_set(NULL, carg, "memcached_slab_global_page_pool", METRIC_TYPE_GAUGE, "Pages in the global slab page pool.");
	namespace_metric_family_set(NULL, carg, "memcached_time_seconds", METRIC_TYPE_GAUGE, "Current unix time as reported by memcached.");
	namespace_metric_family_set(NULL, carg, "memcached_read_buf", METRIC_TYPE_GAUGE, "Read buffer usage by kind.");
	namespace_metric_family_set(NULL, carg, "memcached_response_obj", METRIC_TYPE_GAUGE, "Response object pool usage by kind.");
	namespace_metric_family_set(NULL, carg, "memcached_time_since_server_cert_refresh_seconds", METRIC_TYPE_GAUGE, "Seconds since the last server certificate refresh.");
}

static void memcached_emit_value(context_arg *carg, const memcached_stat_rule *rule, int rc, int64_t ival, uint64_t uval, double dval)
{
	if (rc == DATATYPE_DOUBLE)
	{
		if (rule->emit == MC_L2)
			metric_add_labels2((char *)rule->family, &dval, DATATYPE_DOUBLE, carg, (char *)rule->l1n, (char *)rule->l1v, (char *)rule->l2n, (char *)rule->l2v);
		else if (rule->emit == MC_L1)
			metric_add_labels((char *)rule->family, &dval, DATATYPE_DOUBLE, carg, (char *)rule->l1n, (char *)rule->l1v);
		else
			metric_add_auto((char *)rule->family, &dval, DATATYPE_DOUBLE, carg);
		return;
	}

	if (rc == DATATYPE_INT)
	{
		if (rule->emit == MC_L2)
			metric_add_labels2((char *)rule->family, &ival, DATATYPE_INT, carg, (char *)rule->l1n, (char *)rule->l1v, (char *)rule->l2n, (char *)rule->l2v);
		else if (rule->emit == MC_L1)
			metric_add_labels((char *)rule->family, &ival, DATATYPE_INT, carg, (char *)rule->l1n, (char *)rule->l1v);
		else
			metric_add_auto((char *)rule->family, &ival, DATATYPE_INT, carg);
		return;
	}

	if (rule->emit == MC_L2)
		metric_add_labels2((char *)rule->family, &uval, DATATYPE_UINT, carg, (char *)rule->l1n, (char *)rule->l1v, (char *)rule->l2n, (char *)rule->l2v);
	else if (rule->emit == MC_L1)
		metric_add_labels((char *)rule->family, &uval, DATATYPE_UINT, carg, (char *)rule->l1n, (char *)rule->l1v);
	else
		metric_add_auto((char *)rule->family, &uval, DATATYPE_UINT, carg);
}

void memcached_handler(char *metrics, size_t size, context_arg *carg)
{
	char *cur = metrics;
	char stat_key[MC_NAME_SIZE];
	char fallback_name[MC_NAME_SIZE + 16];
	uint64_t msize;
	uint64_t cmd_set = 0;
	uint64_t cas_total = 0;
	int have_cmd_set = 0;

	memcached_register_families(carg);

	while ((size_t)(cur - metrics) < size)
	{
		cur = strstr(cur, "STAT ");
		if (!cur)
			break;

		cur += 5;
		msize = strcspn(cur, " \t\r\n");
		size_t key_copy = msize < (sizeof(stat_key) - 1) ? msize : (sizeof(stat_key) - 1);
		strlcpy(stat_key, cur, key_copy + 1);
		metric_name_normalizer(stat_key, key_copy);

		cur += msize;
		msize = strspn(cur, " \t\r\n");
		cur += msize;
		msize = strcspn(cur, " \t\r\n");

		int rc = metric_value_validator(cur, msize);
		if (rc != DATATYPE_INT && rc != DATATYPE_UINT && rc != DATATYPE_DOUBLE)
		{
			cur += msize;
			continue;
		}

		int64_t ival = 0;
		uint64_t uval = 0;
		double dval = 0;
		char *after = cur;
		if (rc == DATATYPE_DOUBLE)
			dval = strtod(cur, &after);
		else if (rc == DATATYPE_INT)
			ival = strtoll(cur, &after, 10);
		else
			uval = strtoull(cur, &after, 10);
		cur = after;

		const memcached_stat_rule *rule = memcached_find_rule(stat_key);
		if (!rule)
		{
			snprintf(fallback_name, sizeof(fallback_name), "memcached_%s", stat_key);
			namespace_metric_family_set(NULL, carg, fallback_name, METRIC_TYPE_GAUGE, "Memcached STAT field without a dedicated mapping.");
			if (rc == DATATYPE_DOUBLE)
				metric_add_auto(fallback_name, &dval, DATATYPE_DOUBLE, carg);
			else if (rc == DATATYPE_INT)
				metric_add_auto(fallback_name, &ival, DATATYPE_INT, carg);
			else
				metric_add_auto(fallback_name, &uval, DATATYPE_UINT, carg);
			continue;
		}

		if (rule->emit == MC_SKIP)
			continue;

		if (rule->emit == MC_ACCUM_CMD_SET)
		{
			have_cmd_set = 1;
			cmd_set = (rc == DATATYPE_DOUBLE) ? (uint64_t)dval : (rc == DATATYPE_INT) ? (uint64_t)ival : uval;
			continue;
		}

		if (rule->emit == MC_ACCUM_CAS)
		{
			uint64_t cas_val = (rc == DATATYPE_DOUBLE) ? (uint64_t)dval : (rc == DATATYPE_INT) ? (uint64_t)ival : uval;
			cas_total += cas_val;
			memcached_emit_value(carg, rule, DATATYPE_UINT, 0, cas_val, 0);
			continue;
		}

		memcached_emit_value(carg, rule, rc, ival, uval, dval);
	}

	if (have_cmd_set)
	{
		uint64_t set_hits = (cmd_set > cas_total) ? (cmd_set - cas_total) : 0;
		metric_add_labels2("memcached_commands_total", &set_hits, DATATYPE_UINT, carg, "command", "set", "status", "hit");
	}

	if (carg->name)
	{
		query_ds *qds = query_get(carg->name);
		carglog(carg, L_DEBUG, "found queries for datasource: %s: %p\n", carg->name, qds);
		if (qds)
		{
			alligator_ht_foreach_arg(qds->hash, memcached_queries_foreach, carg);
		}
	}
	carg->parser_status = 1;
}

string* memcached_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(strdup("stats\r\n"));
}

void memcached_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("memcached");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = memcached_handler;
	//actx->handler[0].validator = memcached_validator;
	actx->handler[0].mesg_func = memcached_mesg;
	strlcpy(actx->handler[0].key,"memcached", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
