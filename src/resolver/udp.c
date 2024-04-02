#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/entrypoint.h"
#include "resolver/resolver.h"
#include "events/metrics.h"
#include "main.h"

void resolver_read_udp(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
	context_arg *carg = req->data;
	if (carg->log_level > 1)
		printf("%"u64": udp resolver read %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	carg->read_time_finish = setrtime();
	uint64_t read_time = getrtime_mcs(carg->read_time, carg->read_time_finish, 0);
	uint64_t write_time = getrtime_mcs(carg->write_time, carg->write_time_finish, 0);
	uint64_t response_time = getrtime_mcs(carg->write_time, carg->read_time_finish, 0);
	resolver_data *rd = carg->rd;

	if (rd)
	{
		heap_insert(rd->read_time, read_time);
		heap_insert(rd->write_time, write_time);
		heap_insert(rd->response_time, response_time);
		calc_percentiles(carg, rd->read_time, NULL, "resolver_read_time_mcs_quantile", rd->labels);
		calc_percentiles(carg, rd->write_time, NULL, "resolver_write_time_mcs_quantile", rd->labels);
		calc_percentiles(carg, rd->response_time, NULL, "resolver_response_time_mcs_quantile", rd->labels);
	}

	if (nread < 0)
	{
		if (carg->log_level > 1)
			fprintf(stderr, "Read error %s\n", uv_err_name(nread));
		free(buf->base);
		return;
	}
	if (nread == 0)
	{
		free(buf->base);
		return;
	}

	(carg->conn_counter)++;
	(carg->read_counter)++;

	metric_add_labels("udp_entrypoint_read", &carg->read_counter, DATATYPE_UINT, carg, "entrypoint", carg->key);

	uint64_t ttl = dns_handler(buf->base, nread, carg);
	if (carg->lock)
	{
		uv_udp_recv_stop(req);
		uv_close((uv_handle_t*) req, NULL);
		carg->lock = 0;
	}
	free(buf->base);

	aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);
	metric_add_labels5("alligator_parser_status", &carg->parsed, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);

	uv_timer_set_repeat(&carg->resolver_timer, ++ttl * 1000);
}

void resolver_send_udp(uv_udp_send_t* req, int status) {
	context_arg *carg = req->data;
	if (carg->log_level > 1)
		printf("%"u64": udp resolver send %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);
	carg->write_time_finish = setrtime();

	if (status != 0) {
		if (carg->log_level > 1)
			fprintf(stderr, "send_cb error: %s\n", uv_strerror(status));
	}

	req->handle->data = req->data;
	uv_udp_recv_start(req->handle, alloc_buffer, resolver_read_udp);
	carg->read_time = setrtime();
}

void resolver_connect_udp(void *arg)
{
	context_arg *carg = arg;
	if (carg->log_level > 1)
		printf("%"u64": udp resolver connect %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	if (carg->lock)
		return;

	carg->count = 0;
	carg->lock = 1;
	carg->parsed = 0;
	carg->curr_ttl = carg->ttl;

	char *addr = resolver_carg_get_addr(carg);
	if (!addr)
		return;

	struct sockaddr_in res;
	uv_ip4_addr(addr, carg->numport, &res);
	carg->dest = &res;

	carg->udp_send.data = carg;
	uv_udp_init(uv_default_loop(), &carg->udp_client);

	int status = uv_udp_send(&carg->udp_send, &carg->udp_client, &carg->request_buffer, 1, (struct sockaddr *)carg->dest, resolver_send_udp);
	if (status)
		if (carg->log_level > 1)
			fprintf(stderr, "uv_udp_send error: %s\n", uv_strerror(status));

	carg->write_time = setrtime();
}
