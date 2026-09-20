#include "main.h"
#include "events/context_arg.h"
#include "common/rtime.h"
#include "metric/percentile_heap.h"
#include "metric/labels.h"
#include "probe/probe.h"
#include <string.h>

static uint32_t read_metric_interval_effective(const context_arg *src)
{
	uint32_t v = src->read_metric_interval_sec;
	return v ? v : 10U;
}

void entrypoint_read_metrics_throttled_push(context_arg *src, context_arg *carg, const char *proto, uint8_t labels_entrypoint, const char *host_label)
{
	static const char type_entrypoint[] = "entrypoint";

	if (!src || !carg || carg->no_metric)
		return;
	if (!labels_entrypoint)
		return;

	uint32_t iv = read_metric_interval_effective(src);
	r_time now = setrtime();
	uint64_t sec = (uint64_t)now.sec;
	if (!src->entrypoint_read_metric_last_push_sec || sec - src->entrypoint_read_metric_last_push_sec >= (uint64_t)iv)
	{
		const char *mkey = carg->key ? carg->key : "";
		const char *host = host_label ? host_label : mkey;
		metric_add_labels4("alligator_session_reads_total", &src->read_counter, DATATYPE_UINT, carg, "key", (char *)mkey, "proto", (char *)proto, "type", (char *)type_entrypoint, "host", (char *)host);
		metric_add_labels4("alligator_session_read_bytes_total", &src->read_bytes_counter, DATATYPE_UINT, carg, "key", (char *)mkey, "proto", (char *)proto, "type", (char *)type_entrypoint, "host", (char *)host);
		src->entrypoint_read_metric_last_push_sec = sec;
	}
}

static void session_duration_set(context_arg *carg, char *key, char *proto, char *type, char *host, const char *stage, double seconds)
{
	metric_add_labels5("alligator_session_duration_seconds", &seconds, DATATYPE_DOUBLE, carg,
		"key", key, "proto", proto, "type", type, "host", host, "stage", (char *)stage);
}

static void session_duration_percentiles(context_arg *carg, percentile_buffer *pb, alligator_ht *base, const char *stage)
{
	alligator_ht *hash = labels_dup(base);
	labels_hash_insert(hash, "stage", (char *)stage);
	calc_percentiles(carg, pb, NULL, "alligator_session_duration_seconds", hash);
	labels_hash_free(hash);
}

void aggregator_events_metric_add(context_arg *srv_carg, context_arg *carg, char *key, char *proto, char *type, char *host)
{
	if (!key)
		key = carg->key;

	if (carg->no_metric)
		return;

	if (carg->parser_name)
		metric_add_labels5("alligator_parser_ok", &carg->parser_status, DATATYPE_UINT, carg, "proto", proto, "type", type, "host", host, "key", key, "parser", carg->parser_name);

	metric_add_labels4("alligator_session_connects_total", &srv_carg->conn_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_session_reads_total", &srv_carg->read_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_session_writes_total", &srv_carg->write_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_session_timeouts_total", &srv_carg->timeout_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_session_closes_total", &srv_carg->close_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_session_shutdowns_total", &srv_carg->shutdown_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_session_written_bytes_total", &srv_carg->write_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_session_read_bytes_total", &srv_carg->read_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	if (carg->tls)
	{
		metric_add_labels4("alligator_session_tls_writes_total", &srv_carg->tls_write_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_session_tls_reads_total", &srv_carg->tls_read_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_session_tls_handshakes_total", &srv_carg->tls_init_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_session_tls_written_bytes_total", &srv_carg->tls_write_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_session_tls_read_bytes_total", &srv_carg->tls_read_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	}

	if (!strcmp(type, "entrypoint"))
		srv_carg->entrypoint_read_metric_last_push_sec = (uint64_t)setrtime().sec;

	double connect_s = getrtime_mcs_seconds(carg->connect_time, carg->connect_time_finish);
	double tls_connect_s = getrtime_mcs_seconds(carg->tls_connect_time, carg->tls_connect_time_finish);
	double write_s = getrtime_mcs_seconds(carg->write_time, carg->write_time_finish);
	double read_s = getrtime_mcs_seconds(carg->read_time, carg->read_time_finish);
	double tls_read_s = getrtime_mcs_seconds(carg->tls_read_time, carg->tls_read_time_finish);
	double tls_write_s = getrtime_mcs_seconds(carg->tls_write_time, carg->tls_write_time_finish);
	double shutdown_s = getrtime_mcs_seconds(carg->shutdown_time, carg->shutdown_time_finish);
	double exec_s = getrtime_mcs_seconds(carg->exec_time, carg->exec_time_finish);
	double total_s = getrtime_mcs_seconds(carg->connect_time, carg->close_time);

	session_duration_set(carg, key, proto, type, host, "connect", connect_s);
	session_duration_set(carg, key, proto, type, host, "write", write_s);
	session_duration_set(carg, key, proto, type, host, "read", read_s);
	if (carg->tls == 1)
	{
		session_duration_set(carg, key, proto, type, host, "tls_handshake", tls_connect_s);
		session_duration_set(carg, key, proto, type, host, "tls_write", tls_write_s);
		session_duration_set(carg, key, proto, type, host, "tls_read", tls_read_s);
	}
	session_duration_set(carg, key, proto, type, host, "shutdown", shutdown_s);
	session_duration_set(carg, key, proto, type, host, "total", total_s);
	metric_add_labels4("alligator_parser_duration_seconds", &exec_s, DATATYPE_DOUBLE, carg, "key", key, "proto", proto, "type", type, "host", host);

	if (carg->q_request_time)
	{
		heap_insert(carg->q_request_time, total_s);
		heap_insert(carg->q_read_time, read_s);
		heap_insert(carg->q_connect_time, connect_s);

		alligator_ht *hash = alligator_ht_init(NULL);
		labels_merge(hash, carg->labels);
		labels_hash_insert(hash, "key", key);
		labels_hash_insert(hash, "proto", proto);
		labels_hash_insert(hash, "type", type);
		labels_hash_insert(hash, "host", host);
		session_duration_percentiles(carg, carg->q_read_time, hash, "read");
		session_duration_percentiles(carg, carg->q_request_time, hash, "total");
		session_duration_percentiles(carg, carg->q_connect_time, hash, "connect");
		labels_hash_free(hash);
	}

	//probe_node *pn = carg->data;
	//if (pn)
	//{
	//	if (pn->prober == APROTO_TCP)
	//	{
	//		printf("TCP\n");
	//	}
	//	else if (pn->prober == APROTO_TLS)
	//	{
	//		printf("TLS\n");
	//	}
	//}
	//int64_t a = 10;
	//heap_insert(pb, a);
}
