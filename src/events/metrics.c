#include "main.h"
#include "events/context_arg.h"
#include "events/metrics.h"
#include "common/rtime.h"
#include "metric/percentile_heap.h"
#include "metric/labels.h"
#include "metric/namespace.h"
#include "probe/probe.h"
#include "resolver/resolver.h"
#include <string.h>

static uint32_t read_metric_interval_effective(const context_arg *src)
{
	uint32_t v = src->read_metric_interval_sec;
	return v ? v : 10U;
}

static void event_label_put(alligator_ht *hash, const char *name, const char *val)
{
	if (!hash || !name || !name[0] || !val || !val[0])
		return;
	labels_hash_insert(hash, (char *)name, (char *)val);
}

static const char *event_dns_class(uint32_t rrclass)
{
	if (rrclass == DNS_CLASS_CHAOS)
		return "CH";
	if (rrclass == DNS_CLASS_HESIOD)
		return "HS";
	return "IN";
}

static const char *event_path_label(context_arg *carg)
{
	const char *q = carg->query_url;
	const char *p;

	if (q && q[0] && strcmp(q, "/"))
		return q;
	if (!carg->key)
		return NULL;
	p = strstr(carg->key, ")/");
	if (p && p[2])
		return p + 1;
	return NULL;
}

alligator_ht *alligator_event_labels(context_arg *carg, const char *proto, const char *type, const char *host)
{
	alligator_ht *hash = alligator_ht_init(NULL);
	probe_node *pn;

	if (!carg)
		return hash;

	event_label_put(hash, "proto", proto);
	event_label_put(hash, "type", type);
	event_label_put(hash, "host", (host && host[0]) ? host : carg->host);
	event_label_put(hash, "port", carg->port[0] ? carg->port : NULL);
	event_label_put(hash, "path", event_path_label(carg));
	event_label_put(hash, "match", carg->file_match);

	if (carg->resolver) {
		event_label_put(hash, "name", (char *)carg->data);
		event_label_put(hash, "qtype", carg->rrtype);
		event_label_put(hash, "class", event_dns_class(carg->rrclass));
	}

	pn = probe_from_carg(carg);
	if (pn)
		event_label_put(hash, "module", pn->name);

	event_label_put(hash, "parser", carg->parser_name);

	return hash;
}

void alligator_event_metric(const char *name, void *value, int8_t dtype, context_arg *carg, alligator_ht *base)
{
	metric_add((char *)name, labels_dup(base), value, dtype, carg);
}

void alligator_session_connect_ok_set(context_arg *carg, uint64_t ok)
{
	alligator_ht *lbl;

	if (!carg)
		return;
	lbl = alligator_event_labels(carg, "tcp", "aggregator", carg->host);
	alligator_event_metric("alligator_session_connect_ok", &ok, DATATYPE_UINT, carg, lbl);
	labels_hash_free(lbl);
}

void alligator_session_connects_inc(context_arg *carg)
{
	alligator_ht *lbl;

	if (!carg)
		return;
	(carg->conn_counter)++;
	lbl = alligator_event_labels(carg, "tcp", "aggregator", carg->host);
	alligator_event_metric("alligator_session_connects_total", &carg->conn_counter, DATATYPE_UINT, carg, lbl);
	labels_hash_free(lbl);
}

void alligator_parser_ok_set(context_arg *carg, uint64_t ok, const char *proto, const char *host)
{
	alligator_ht *lbl;

	if (!carg)
		return;
	lbl = alligator_event_labels(carg, proto, "aggregator", host);
	alligator_event_metric("alligator_parser_ok", &ok, DATATYPE_UINT, carg, lbl);
	labels_hash_free(lbl);
}

void entrypoint_read_metrics_throttled_push(context_arg *src, context_arg *carg, const char *proto, uint8_t labels_entrypoint, const char *host_label)
{
	static const char type_entrypoint[] = "entrypoint";
	alligator_ht *lbl;
	const char *host;

	if (!src || !carg || carg->no_metric)
		return;
	if (!labels_entrypoint)
		return;

	uint32_t iv = read_metric_interval_effective(src);
	r_time now = setrtime();
	uint64_t sec = (uint64_t)now.sec;
	if (!src->entrypoint_read_metric_last_push_sec || sec - src->entrypoint_read_metric_last_push_sec >= (uint64_t)iv)
	{
		host = host_label ? host_label : (carg->host[0] ? carg->host : NULL);
		lbl = alligator_event_labels(carg, proto, type_entrypoint, host);
		alligator_event_metric("alligator_session_reads_total", &src->read_counter, DATATYPE_UINT, carg, lbl);
		alligator_event_metric("alligator_session_read_bytes_total", &src->read_bytes_counter, DATATYPE_UINT, carg, lbl);
		labels_hash_free(lbl);
		src->entrypoint_read_metric_last_push_sec = sec;
	}
}

static void session_duration_set(context_arg *carg, alligator_ht *base, const char *stage, double seconds)
{
	alligator_ht *hash = labels_dup(base);
	labels_hash_insert(hash, "stage", (char *)stage);
	metric_add("alligator_session_duration_seconds", hash, &seconds, DATATYPE_DOUBLE, carg);
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
	alligator_ht *lbl;

	(void)key;

	if (carg->no_metric)
		return;

	lbl = alligator_event_labels(carg, proto, type, host);

	if (carg->parser_name)
		alligator_event_metric("alligator_parser_ok", &carg->parser_status, DATATYPE_UINT, carg, lbl);

	alligator_event_metric("alligator_session_connects_total", &srv_carg->conn_counter, DATATYPE_UINT, carg, lbl);
	alligator_event_metric("alligator_session_reads_total", &srv_carg->read_counter, DATATYPE_UINT, carg, lbl);
	alligator_event_metric("alligator_session_writes_total", &srv_carg->write_counter, DATATYPE_UINT, carg, lbl);
	alligator_event_metric("alligator_session_timeouts_total", &srv_carg->timeout_counter, DATATYPE_UINT, carg, lbl);
	alligator_event_metric("alligator_session_closes_total", &srv_carg->close_counter, DATATYPE_UINT, carg, lbl);
	alligator_event_metric("alligator_session_shutdowns_total", &srv_carg->shutdown_counter, DATATYPE_UINT, carg, lbl);
	alligator_event_metric("alligator_session_written_bytes_total", &srv_carg->write_bytes_counter, DATATYPE_UINT, carg, lbl);
	alligator_event_metric("alligator_session_read_bytes_total", &srv_carg->read_bytes_counter, DATATYPE_UINT, carg, lbl);
	if (carg->tls)
	{
		alligator_event_metric("alligator_session_tls_writes_total", &srv_carg->tls_write_counter, DATATYPE_UINT, carg, lbl);
		alligator_event_metric("alligator_session_tls_reads_total", &srv_carg->tls_read_counter, DATATYPE_UINT, carg, lbl);
		alligator_event_metric("alligator_session_tls_handshakes_total", &srv_carg->tls_init_counter, DATATYPE_UINT, carg, lbl);
		alligator_event_metric("alligator_session_tls_written_bytes_total", &srv_carg->tls_write_bytes_counter, DATATYPE_UINT, carg, lbl);
		alligator_event_metric("alligator_session_tls_read_bytes_total", &srv_carg->tls_read_bytes_counter, DATATYPE_UINT, carg, lbl);
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

	session_duration_set(carg, lbl, "connect", connect_s);
	session_duration_set(carg, lbl, "write", write_s);
	session_duration_set(carg, lbl, "read", read_s);
	if (carg->tls == 1)
	{
		session_duration_set(carg, lbl, "tls_handshake", tls_connect_s);
		session_duration_set(carg, lbl, "tls_write", tls_write_s);
		session_duration_set(carg, lbl, "tls_read", tls_read_s);
	}
	session_duration_set(carg, lbl, "shutdown", shutdown_s);
	session_duration_set(carg, lbl, "total", total_s);
	alligator_event_metric("alligator_parser_duration_seconds", &exec_s, DATATYPE_DOUBLE, carg, lbl);

	if (carg->q_request_time)
	{
		heap_insert(carg->q_request_time, total_s);
		heap_insert(carg->q_read_time, read_s);
		heap_insert(carg->q_connect_time, connect_s);

		session_duration_percentiles(carg, carg->q_read_time, lbl, "read");
		session_duration_percentiles(carg, carg->q_request_time, lbl, "total");
		session_duration_percentiles(carg, carg->q_connect_time, lbl, "connect");
	}

	labels_hash_free(lbl);
}
