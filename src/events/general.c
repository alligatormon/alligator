#include "main.h"
#include "common/selector.h"
#include "alligator_version.h"
#include "metric/metric_dump.h"
#include "metric/metric_types.h"
#include "metric/namespace.h"
#include "events/filetailer.h"

extern aconf *ac;

static void register_alligator_metric_families(void)
{
	/* Aggregator protocol/parser metrics. */
	namespace_metric_family_set(NULL, NULL, "aggregator_http_body_size_bytes", METRIC_TYPE_GAUGE, "HTTP response body size in bytes observed by aggregator parser.");
	namespace_metric_family_set(NULL, NULL, "aggregator_http_code", METRIC_TYPE_GAUGE, "Last HTTP response code observed by aggregator parser.");
	namespace_metric_family_set(NULL, NULL, "aggregator_http_headers_size_bytes", METRIC_TYPE_GAUGE, "HTTP response headers size in bytes observed by aggregator parser.");
	namespace_metric_family_set(NULL, NULL, "aggregator_http_requests_total", METRIC_TYPE_COUNTER, "Total HTTP requests observed by aggregator parser by response code.");
	namespace_metric_family_set(NULL, NULL, "probe_success", METRIC_TYPE_GAUGE, "Blackbox probe success where 1 means the probe passed configured checks.");
	namespace_metric_family_set(NULL, NULL, "aggregator_resolve_address", METRIC_TYPE_GAUGE, "DNS record presence marker for resolved names and resource record data.");
	namespace_metric_family_set(NULL, NULL, "aggregator_resolve_address_rr_count", METRIC_TYPE_GAUGE, "Number of DNS resource records in a resolve response by query and type.");

	/* Core alligator runtime/process metrics. */
	namespace_metric_family_set(NULL, NULL, "alligator_close_total", METRIC_TYPE_COUNTER, "Total closed connections by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_open_total", METRIC_TYPE_COUNTER, "Total opened filetailer sources by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_connect_duration_microseconds", METRIC_TYPE_GAUGE, "Connection duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_connect_ok_total", METRIC_TYPE_COUNTER, "Total successful backend connection attempts.");
	namespace_metric_family_set(NULL, NULL, "alligator_connect_total", METRIC_TYPE_COUNTER, "Total connection attempts by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_cpu_seconds_total", METRIC_TYPE_COUNTER, "Alligator process CPU time in seconds by mode.");
	namespace_metric_family_set(NULL, NULL, "alligator_execute_duration_microseconds", METRIC_TYPE_GAUGE, "Execute stage duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_tls_connect_duration_microseconds", METRIC_TYPE_GAUGE, "TLS connect duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_tls_init_total", METRIC_TYPE_COUNTER, "Total TLS initialization attempts by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_tls_read_total", METRIC_TYPE_COUNTER, "Total TLS read operations by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_tls_read_bytes_total", METRIC_TYPE_COUNTER, "Total bytes read over TLS by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_tls_read_duration_microseconds", METRIC_TYPE_GAUGE, "TLS read duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_tls_write_total", METRIC_TYPE_COUNTER, "Total TLS write operations by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_tls_write_bytes_total", METRIC_TYPE_COUNTER, "Total bytes written over TLS by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_tls_write_duration_microseconds", METRIC_TYPE_GAUGE, "TLS write duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_gc_duration_microseconds", METRIC_TYPE_GAUGE, "Garbage-collection/expiration loop duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_memory_usage_bytes", METRIC_TYPE_GAUGE, "Alligator process memory usage in bytes by memory type.");
	namespace_metric_family_set(NULL, NULL, "alligator_metric_allocates_total", METRIC_TYPE_COUNTER, "Total metric allocation operations.");
	namespace_metric_family_set(NULL, NULL, "alligator_metric_cache_hits_total", METRIC_TYPE_COUNTER, "Total metric cache hits.");
	namespace_metric_family_set(NULL, NULL, "alligator_metric_free_total", METRIC_TYPE_COUNTER, "Total metric free operations.");
	namespace_metric_family_set(NULL, NULL, "alligator_parser_status", METRIC_TYPE_GAUGE, "Alligator parser status flag.");
	namespace_metric_family_set(NULL, NULL, "alligator_read_bytes_total", METRIC_TYPE_COUNTER, "Total bytes read by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_read_duration_microseconds", METRIC_TYPE_GAUGE, "Read stage duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_read_total", METRIC_TYPE_COUNTER, "Total read operations by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_request_duration_microseconds", METRIC_TYPE_GAUGE, "End-to-end request duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_shutdown_duration_microseconds", METRIC_TYPE_GAUGE, "Shutdown stage duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_shutdown_total", METRIC_TYPE_COUNTER, "Total shutdown operations by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_timeout_total", METRIC_TYPE_COUNTER, "Total timeouts by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_version", METRIC_TYPE_GAUGE, "Alligator version marker with version label.");
	namespace_metric_family_set(NULL, NULL, "alligator_write_bytes_total", METRIC_TYPE_COUNTER, "Total bytes written by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_write_duration_microseconds", METRIC_TYPE_GAUGE, "Write stage duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_write_total", METRIC_TYPE_COUNTER, "Total write operations by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_push_accepted_lines_count", METRIC_TYPE_GAUGE, "Number of accepted lines in the most recent push payload.");
	namespace_metric_family_set(NULL, NULL, "alligator_push_metrictree_duration_nanoseconds", METRIC_TYPE_GAUGE, "Time spent updating metric tree during push parsing in nanoseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_push_parsed_lines_count", METRIC_TYPE_GAUGE, "Number of parsed lines in the most recent push payload.");
	namespace_metric_family_set(NULL, NULL, "alligator_push_parsing_duration_nanoseconds", METRIC_TYPE_GAUGE, "Time spent parsing push payload in nanoseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_push_split_duration_nanoseconds", METRIC_TYPE_GAUGE, "Time spent splitting push payload into lines in nanoseconds.");
}

void general_loop_cb(uv_timer_t* handle)
{
	uint64_t val = 1;
	metric_add_labels("alligator_version", &val, DATATYPE_UINT, NULL, "version", ALLIGATOR_VERSION);
	metric_add_auto("alligator_metric_cache_hits_total", &ac->metric_cache_hits, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_allocates_total", &ac->metric_allocates, DATATYPE_UINT, NULL);
	metric_add_auto("alligator_metric_free_total", &ac->metric_freed, DATATYPE_UINT, NULL);
}

void namespaces_expire_foreach(void *funcarg, void* arg)
{
	namespace_struct *ns = arg;
	r_time time = setrtime();
	expire_purge(time.sec, NULL, ns);
}


void expire_loop(uv_timer_t* handle)
{
	alligator_ht_foreach_arg(ac->_namespace, namespaces_expire_foreach, NULL);
}

void dump_loop()
{
	metric_dump(-1);
	filetailer_write_state(ac->file_stat);
}

void general_loop()
{
	uv_loop_t *loop = ac->loop;
	register_alligator_metric_families();

	uv_timer_init(loop, &ac->general_timer);
	uv_timer_start(&ac->general_timer, general_loop_cb, 1000, 1000);

	uv_timer_init(loop, &ac->expire_timer);
	uv_timer_start(&ac->expire_timer, expire_loop, 10000, 10000);

	uv_timer_init(loop, &ac->dump_timer);
	uv_timer_start(&ac->dump_timer, dump_loop, 11000, ac->persistence_period);
}
