#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/entrypoint.h"
#include "resolver/resolver.h"
#include "dstructures/uv_cache.h"
#include "events/metrics.h"
#include "events/client.h"
#include "common/logs.h"
#include "main.h"
#include "common/stop.h"
#include "metric/percentile_heap.h"

static void resolver_closed_tcp(uv_handle_t *handle);

void resolver_tcp_halt(context_arg *carg)
{
	if (!carg)
		return;

	if (carg->tt_timer) {
		uv_timer_stop(carg->tt_timer);
		carg->tt_timer->data = NULL;
		alligator_cache_push(ac->uv_cache_timer, carg->tt_timer);
		carg->tt_timer = NULL;
	}

	if (!uv_is_closing((uv_handle_t*)&carg->client)) {
		carg->client.data = carg;
		uv_close((uv_handle_t*)&carg->client, resolver_closed_tcp);
	} else {
		carg->lock = 0;
	}
}

static void resolver_closed_tcp(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	carglog(carg, L_INFO, "%"u64": tcp-resolver client closed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, TTL: %"d64"\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->context_ttl);
	(carg->close_counter)++;
	carg->close_time_finish = setrtime();

	aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);
	metric_add_labels5("alligator_parser_status", &carg->parsed, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);

	carg->lock = 0;
	string_null(carg->full_body);
}

void resolver_close_tcp(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	carglog(carg, L_INFO, "%"u64": tcp-resolver client call close %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	if (carg->tt_timer)
		carg->tt_timer->data = NULL;

	carg->close_time = setrtime();
	if (!uv_is_closing((uv_handle_t*)&carg->client))
	{
		uv_close((uv_handle_t*)&carg->client, resolver_closed_tcp);
	}
}

void resolver_shutdown_tcp(uv_shutdown_t* req, int status)
{
	context_arg* carg = req->data;
	carglog(carg, L_INFO, "%"u64": tcp-resolver client shutdowned %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	(carg->shutdown_counter)++;
	carg->shutdown_time_finish = setrtime();

	resolver_close_tcp((uv_handle_t *)&carg->client);
	free(req);
}

void resolver_timeout_tcp(uv_timer_t *timer)
{
	uv_timer_stop(timer);
	alligator_cache_push(ac->uv_cache_timer, timer);

	context_arg *carg = timer->data;
	if (!carg)
	{
		return;
	}

	carglog(carg, L_INFO, "%"u64": timeout tcp-resolver client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);
	(carg->timeout_counter)++;

	tcp_client_close((uv_handle_t *)&carg->client);
}

void resolver_read_tcp(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)stream->data;

	carglog(carg, L_INFO, "%"u64": tcp-resolver client readed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread);
	
	(carg->read_counter)++;
	carg->read_time_finish = setrtime();

	if (nread > 0)
	{
		if (!carg->full_body->l)
		{
			dns_handler(buf->base+2, nread-2, carg);

			resolver_data *rd = carg->rd;
			if (rd)
			{
				uint64_t read_time = getrtime_mcs(carg->read_time, carg->read_time_finish, 0);
				uint64_t write_time = getrtime_mcs(carg->write_time, carg->write_time_finish, 0);
				uint64_t response_time = getrtime_mcs(carg->write_time, carg->read_time_finish, 0);
				heap_insert(rd->read_time, read_time);
				heap_insert(rd->write_time, write_time);
				heap_insert(rd->response_time, response_time);
				calc_percentiles(carg, rd->read_time, NULL, "resolver_read_time_mcs_quantile", rd->labels);
				calc_percentiles(carg, rd->write_time, NULL, "resolver_write_time_mcs_quantile", rd->labels);
				calc_percentiles(carg, rd->response_time, NULL, "resolver_response_time_mcs_quantile", rd->labels);
			}

			if (carg->lock)
				carg->lock = 0;
		}
		else
			string_cat(carg->full_body, buf->base, nread);

	}
	else if (nread == 0)
		return;

	else if(nread == UV_EOF)
	{
		uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
		req->data = carg;
		carg->shutdown_time = setrtime();
		uv_shutdown(req, (uv_stream_t*)&carg->client, resolver_shutdown_tcp);
		resolver_close_tcp((uv_handle_t *)&carg->client);
	}
	else if (nread == UV_ECONNRESET || nread == UV_ECONNABORTED)
		resolver_close_tcp((uv_handle_t *)&carg->client);
	else if (nread == UV_ENOBUFS)
		resolver_close_tcp((uv_handle_t *)&carg->client);
}

void resolver_writed_tcp(uv_write_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;

	if (status != 0) {
		carglog(carg, L_ERROR, "resolver length write error: %s\n", uv_strerror(status));
		return;
	}

	int write_status = uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->request_buffer, 1, NULL);
	if (write_status) {
		carglog(carg, L_ERROR, "uv_write error: %s\n", uv_strerror(write_status));
		return;
	}

	carg->write_time_finish = setrtime();
	carg->read_time = setrtime();
}

void resolver_connected_tcp(uv_connect_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;
	carglog(carg, L_INFO, "%"u64": tcp-resolver connected %p(%p:%p) with key %s, parser name %s, hostname %s, port: %s tls: %d, status: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->parser_name, carg->host, carg->port, carg->tls, status);
	(carg->conn_counter)++;

	uint64_t ok = 1;
	if (status < 0)
	{
		ok = 0;
		metric_add_labels5("alligator_connect_ok_total", &ok, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);
		carg->lock = 0;
		if (carg->tt_timer) {
			uv_timer_stop(carg->tt_timer);
			carg->tt_timer->data = NULL;
			alligator_cache_push(ac->uv_cache_timer, carg->tt_timer);
			carg->tt_timer = NULL;
		}
		return;
	}

	metric_add_labels5("alligator_connect_ok_total", &ok, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);

	carg->write_time = setrtime();

	memset(&carg->write_req, 0, sizeof(carg->write_req));

	carg->write_req.data = carg;

	int read_status = uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, resolver_read_tcp);
	if (read_status)
		carglog(carg, L_ERROR, "resolver_read_tcp error: %s\n", uv_strerror(read_status));

	char *write_size_data = malloc(2);
	write_size_data[0] = 0;
	write_size_data[1] = carg->request_buffer.len;
	carg->write_buffer = uv_buf_init(write_size_data, 2);
	int write_size_status = uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->write_buffer, 1, resolver_writed_tcp);
	if (write_size_status)
		carglog(carg, L_ERROR, "uv_write error: %s\n", uv_strerror(write_size_status));

	carg->write_bytes_counter += carg->request_buffer.len;
	(carg->write_counter)++;
}

void resolver_connect_tcp(void *arg)
{
	context_arg *carg = arg;

	if (!carg || alligator_stop_requested())
		return;

	carg->count = 0;
	carglog(carg, L_INFO, "%"u64": tcp-resolver connect %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->port, carg->tls, carg->lock, carg->timeout);

	if (carg->lock) {
		if (carg->tt_timer)
			return;
		carg->lock = 0;
	}

	carg->lock = 1;
	carg->parsed = 0;

	resolver_init_client_timer(carg, resolver_timeout_tcp);

	memset(&carg->connect, 0, sizeof(carg->connect));
	carg->connect.data = carg;

	memset(&carg->client, 0, sizeof(carg->client));
	carg->client.data = carg;
	uv_tcp_init(carg->loop, &carg->client);

	char *addr = resolver_carg_get_addr(carg);
	if (!addr) {
		carg->lock = 0;
		return;
	}

	uv_ip4_addr(addr, carg->numport, &carg->remote_addr);

	carg->connect_time = setrtime();
	int status = uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)&carg->remote_addr, resolver_connected_tcp);
	if (status) {
		carglog(carg, L_ERROR, "resolver_connect_tcp error: %s\n", uv_strerror(status));
		carg->lock = 0;
		if (carg->tt_timer) {
			uv_timer_stop(carg->tt_timer);
			carg->tt_timer->data = NULL;
			alligator_cache_push(ac->uv_cache_timer, carg->tt_timer);
			carg->tt_timer = NULL;
		}
	}
}
