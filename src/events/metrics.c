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
		metric_add_labels4("alligator_read_total", &src->read_counter, DATATYPE_UINT, carg, "key", (char *)mkey, "proto", (char *)proto, "type", (char *)type_entrypoint, "host", (char *)host);
		metric_add_labels4("alligator_read_bytes_total", &src->read_bytes_counter, DATATYPE_UINT, carg, "key", (char *)mkey, "proto", (char *)proto, "type", (char *)type_entrypoint, "host", (char *)host);
		src->entrypoint_read_metric_last_push_sec = sec;
	}
}

void aggregator_events_metric_add(context_arg *srv_carg, context_arg *carg, char *key, char *proto, char *type, char *host)
{
	if (!key)
		key = carg->key;

	if (carg->no_metric)
		return;

	if (carg->parser_name)
		metric_add_labels5("alligator_parser_status", &carg->parser_status, DATATYPE_UINT, carg, "proto", proto, "type", type, "host", host, "key", key, "parser", carg->parser_name);

	metric_add_labels4("alligator_connect_total", &srv_carg->conn_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_read_total", &srv_carg->read_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_write_total", &srv_carg->write_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_timeout_total", &srv_carg->timeout_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_close_total", &srv_carg->close_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_shutdown_total", &srv_carg->shutdown_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_write_bytes_total", &srv_carg->write_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_read_bytes_total", &srv_carg->read_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	if (carg->tls)
	{
		metric_add_labels4("alligator_tls_write_total", &srv_carg->tls_write_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_tls_read_total", &srv_carg->tls_read_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_tls_init_total", &srv_carg->tls_init_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_tls_write_bytes_total", &srv_carg->tls_write_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_tls_read_bytes_total", &srv_carg->tls_read_bytes_counter, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	}

	if (!strcmp(type, "entrypoint"))
		srv_carg->entrypoint_read_metric_last_push_sec = (uint64_t)setrtime().sec;

	uint64_t connect_time = getrtime_mcs(carg->connect_time, carg->connect_time_finish, 0);
	uint64_t tls_connect_time = getrtime_mcs(carg->tls_connect_time, carg->tls_connect_time_finish, 0);
	uint64_t write_time = getrtime_mcs(carg->write_time, carg->write_time_finish, 0);
	uint64_t read_time = getrtime_mcs(carg->read_time, carg->read_time_finish, 0);
	uint64_t tls_read_time = getrtime_mcs(carg->tls_read_time, carg->tls_read_time_finish, 0);
	uint64_t tls_write_time = getrtime_mcs(carg->tls_write_time, carg->tls_write_time_finish, 0);
	uint64_t shutdown_time = getrtime_mcs(carg->shutdown_time, carg->shutdown_time_finish, 0);
	uint64_t exec_time = getrtime_mcs(carg->exec_time, carg->exec_time_finish, 0);
	uint64_t request_time = getrtime_mcs(carg->connect_time, carg->close_time, 0);

	metric_add_labels4("alligator_connect_duration_microseconds", &connect_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_write_duration_microseconds", &write_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_read_duration_microseconds", &read_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	if (carg->tls == 1)
	{
		metric_add_labels4("alligator_tls_connect_duration_microseconds", &tls_connect_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_tls_write_duration_microseconds", &tls_write_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
		metric_add_labels4("alligator_tls_read_duration_microseconds", &tls_read_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	}
	metric_add_labels4("alligator_shutdown_duration_microseconds", &shutdown_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_execute_duration_microseconds", &exec_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);
	metric_add_labels4("alligator_request_duration_microseconds", &request_time, DATATYPE_UINT, carg, "key", key, "proto", proto, "type", type, "host", host);

	if (carg->q_request_time)
	{
		heap_insert(carg->q_request_time, request_time);
		heap_insert(carg->q_read_time, read_time);
		heap_insert(carg->q_connect_time, connect_time);

		alligator_ht *hash = alligator_ht_init(NULL);
		labels_merge(hash, carg->labels);
		labels_hash_insert(hash, "key", key);
		labels_hash_insert(hash, "proto", proto);
		labels_hash_insert(hash, "type", type);
		labels_hash_insert(hash, "host", host);
		calc_percentiles(carg, carg->q_read_time, NULL, "alligator_read_duration_microseconds", hash);
		calc_percentiles(carg, carg->q_request_time, NULL, "alligator_request_duration_microseconds", hash);
		calc_percentiles(carg, carg->q_connect_time, NULL, "alligator_connect_duration_microseconds", hash);
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
