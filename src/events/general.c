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
	namespace_metric_family_set(NULL, NULL, "alligator_http_response_body_bytes", METRIC_TYPE_GAUGE, "Last HTTP response body size in bytes.");
	namespace_metric_family_set(NULL, NULL, "alligator_http_response_status_code", METRIC_TYPE_GAUGE, "Last HTTP response status code.");
	namespace_metric_family_set(NULL, NULL, "alligator_http_response_header_bytes", METRIC_TYPE_GAUGE, "Last HTTP response headers size in bytes.");
	namespace_metric_family_set(NULL, NULL, "alligator_http_requests_total", METRIC_TYPE_COUNTER, "Total HTTP requests observed by response code.");
	namespace_metric_family_set(NULL, NULL, "probe_success", METRIC_TYPE_GAUGE, "Blackbox probe success where 1 means the probe passed configured checks.");
	namespace_metric_family_set(NULL, NULL, "probe_failed_due_to_regex", METRIC_TYPE_GAUGE, "Set when a TCP/HTTP regex expect or body matcher failed.");
	namespace_metric_family_set(NULL, NULL, "probe_expect_info", METRIC_TYPE_GAUGE, "TCP query_response expect step matched.");
	namespace_metric_family_set(NULL, NULL, "alligator_probe_timeout_seconds", METRIC_TYPE_GAUGE, "Configured probe timeout in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_probe_ip_protocol", METRIC_TYPE_GAUGE, "Resolved IP protocol (4 or 6) from socket or getaddrinfo family.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_rtt_seconds", METRIC_TYPE_GAUGE, "Last ICMP echo round-trip time in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_reply_hop_limit", METRIC_TYPE_GAUGE, "TTL/hop-limit of the last ICMP echo reply.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_response_duration_seconds", METRIC_TYPE_HISTOGRAM, "ICMP echo RTT histogram in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_resolve_duration_seconds", METRIC_TYPE_GAUGE, "DNS resolve duration in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_reply_ratio", METRIC_TYPE_GAUGE, "ICMP echo reply ratio in the last burst (0–1).");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_loss_ratio", METRIC_TYPE_GAUGE, "ICMP echo loss ratio in the last burst (0–1).");
	namespace_metric_family_set(NULL, NULL, "alligator_http_request_duration_seconds", METRIC_TYPE_HISTOGRAM, "HTTP probe request duration histogram in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_duplicates", METRIC_TYPE_GAUGE, "Duplicate ICMP echo replies in the last burst.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_rr_info", METRIC_TYPE_GAUGE, "DNS record presence marker for resolved names and resource record data.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_rr_count", METRIC_TYPE_GAUGE, "Number of DNS resource records in a resolve response by query and type.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_soa_serial", METRIC_TYPE_GAUGE, "SOA serial from a DNS resolve response.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_soa_refresh_seconds", METRIC_TYPE_GAUGE, "SOA refresh interval in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_soa_retry_seconds", METRIC_TYPE_GAUGE, "SOA retry interval in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_soa_expire_seconds", METRIC_TYPE_GAUGE, "SOA expire interval in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_soa_minimum_seconds", METRIC_TYPE_GAUGE, "SOA minimum/TTL in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_replies", METRIC_TYPE_GAUGE, "ICMP echo replies in the last burst.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_losses", METRIC_TYPE_GAUGE, "ICMP echoes lost in the last burst.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_replies_total", METRIC_TYPE_COUNTER, "ICMP echo replies received across bursts.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_losses_total", METRIC_TYPE_COUNTER, "ICMP echoes lost across bursts.");
	namespace_metric_family_set(NULL, NULL, "alligator_icmp_echoes_total", METRIC_TYPE_COUNTER, "ICMP echoes attempted across bursts.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_read_duration_seconds", METRIC_TYPE_GAUGE, "DNS resolver read duration quantiles in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_write_duration_seconds", METRIC_TYPE_GAUGE, "DNS resolver write duration quantiles in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_dns_response_duration_seconds", METRIC_TYPE_GAUGE, "DNS resolver response duration quantiles in seconds.");

	/* Core alligator runtime/process metrics. */
	namespace_metric_family_set(NULL, NULL, "alligator_session_closes_total", METRIC_TYPE_COUNTER, "Total closed sessions by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_filetailer_opens_total", METRIC_TYPE_COUNTER, "Total opened filetailer sources by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_duration_seconds", METRIC_TYPE_GAUGE, "Last session stage duration in seconds (stage=connect|write|read|tls_handshake|tls_write|tls_read|shutdown|total).");
	namespace_metric_family_set(NULL, NULL, "alligator_session_connect_ok", METRIC_TYPE_GAUGE, "1 if the last backend connection attempt succeeded, 0 otherwise.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_connects_total", METRIC_TYPE_COUNTER, "Total connection attempts by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_cpu_seconds_total", METRIC_TYPE_COUNTER, "Alligator process CPU time in seconds by mode.");
	namespace_metric_family_set(NULL, NULL, "alligator_parser_duration_seconds", METRIC_TYPE_GAUGE, "Last parser/handler duration in seconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_tls_handshakes_total", METRIC_TYPE_COUNTER, "Total TLS handshake attempts by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_tls_reads_total", METRIC_TYPE_COUNTER, "Total TLS read operations by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_tls_read_bytes_total", METRIC_TYPE_COUNTER, "Total bytes read over TLS by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_tls_writes_total", METRIC_TYPE_COUNTER, "Total TLS write operations by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_tls_written_bytes_total", METRIC_TYPE_COUNTER, "Total bytes written over TLS by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_gc_duration_microseconds", METRIC_TYPE_GAUGE, "Garbage-collection/expiration loop duration in microseconds.");
	namespace_metric_family_set(NULL, NULL, "alligator_memory_usage_bytes", METRIC_TYPE_GAUGE, "Alligator process memory usage in bytes by memory type.");
	namespace_metric_family_set(NULL, NULL, "alligator_metric_allocates_total", METRIC_TYPE_COUNTER, "Total metric allocation operations.");
	namespace_metric_family_set(NULL, NULL, "alligator_metric_cache_hits_total", METRIC_TYPE_COUNTER, "Total metric cache hits.");
	namespace_metric_family_set(NULL, NULL, "alligator_metric_free_total", METRIC_TYPE_COUNTER, "Total metric free operations.");
	namespace_metric_family_set(NULL, NULL, "alligator_parser_ok", METRIC_TYPE_GAUGE, "1 if the last parser run succeeded, 0 otherwise.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_read_bytes_total", METRIC_TYPE_COUNTER, "Total bytes read by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_reads_total", METRIC_TYPE_COUNTER, "Total read operations by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_shutdowns_total", METRIC_TYPE_COUNTER, "Total shutdown operations by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_timeouts_total", METRIC_TYPE_COUNTER, "Total timeouts by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_version", METRIC_TYPE_GAUGE, "Alligator version marker with version label.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_written_bytes_total", METRIC_TYPE_COUNTER, "Total bytes written by key/proto/type/host.");
	namespace_metric_family_set(NULL, NULL, "alligator_session_writes_total", METRIC_TYPE_COUNTER, "Total write operations by key/proto/type/host.");
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
