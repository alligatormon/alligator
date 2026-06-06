#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/entrypoint.h"
#include "common/stop.h"
#include "resolver/resolver.h"
#include "dstructures/uv_cache.h"
#include "events/metrics.h"
#include "common/logs.h"
#include "main.h"
#include <arpa/inet.h>

static void resolver_udp_stop_timer(context_arg *carg)
{
	if (!carg || !carg->tt_timer)
		return;

	uv_timer_stop(carg->tt_timer);
	carg->tt_timer->data = NULL;
	alligator_cache_push(ac->uv_cache_timer, carg->tt_timer);
	carg->tt_timer = NULL;
}

static void resolver_udp_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;

	if (carg) {
		resolver_udp_stop_timer(carg);
		carg->lock = 0;
		memset(&carg->udp_client, 0, sizeof(carg->udp_client));
	}
}

static void resolver_udp_abort(context_arg *carg, uv_udp_t *udp)
{
	resolver_udp_stop_timer(carg);

	if (udp && !uv_is_closing((uv_handle_t*)udp)) {
		uv_udp_recv_stop(udp);
		udp->data = carg;
		uv_close((uv_handle_t*)udp, resolver_udp_closed);
	} else if (carg) {
		carg->lock = 0;
	}
}

void resolver_udp_halt(context_arg *carg)
{
	resolver_udp_abort(carg, &carg->udp_client);
}

void resolver_timeout_udp(uv_timer_t *timer)
{
	uv_timer_stop(timer);
	alligator_cache_push(ac->uv_cache_timer, timer);

	context_arg *carg = timer->data;
	if (!carg)
		return;

	carg->tt_timer = NULL;
	carglog(carg, L_INFO, "%"u64": timeout udp-resolver %p(%p:%p) with key %s, hostname %s, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->timeout);
	(carg->timeout_counter)++;
	resolver_udp_abort(carg, &carg->udp_client);
}

void resolver_read_udp(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
	context_arg *carg = req->data;
	carglog(carg, L_INFO, "%"u64": udp resolver read %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	carg->read_time_finish = setrtime();
	uint64_t read_time = getrtime_mcs(carg->read_time, carg->read_time_finish, 0);
	uint64_t write_time = getrtime_mcs(carg->write_time, carg->write_time_finish, 0);
	uint64_t response_time = getrtime_mcs(carg->write_time, carg->read_time_finish, 0);
	carg->close_time = setrtime();
	carg->close_time_finish = setrtime();
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
		carglog(carg, L_ERROR, "Read error %s\n", uv_err_name(nread));
		free(buf->base);
		resolver_udp_abort(carg, req);
		return;
	}
	if (nread == 0)
	{
		free(buf->base);
		resolver_udp_abort(carg, req);
		return;
	}

	(carg->conn_counter)++;
	(carg->read_counter)++;
	carg->read_bytes_counter += nread;

	metric_add_labels("udp_entrypoint_read_total", &carg->read_counter, DATATYPE_UINT, carg, "entrypoint", carg->key);

	dns_handler(buf->base, nread, carg);
	if (carg->lock)
		resolver_udp_abort(carg, req);

	free(buf->base);
	free(carg->local_addr);
	carg->local_addr = NULL;

	aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);
	metric_add_labels5("alligator_parser_status", &carg->parsed, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);
}

void resolver_send_udp(uv_udp_send_t* req, int status) {
	context_arg *carg = req->data;
	carglog(carg, L_INFO, "%"u64": udp resolver send %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);
	carg->write_time_finish = setrtime();

	if (status != 0) {
		carglog(carg, L_ERROR, "send_cb error: %s\n", uv_strerror(status));
		resolver_udp_abort(carg, req->handle);
		return;
	}
	req->handle->data = req->data;
	uv_udp_recv_start(req->handle, alloc_buffer, resolver_read_udp);
	carg->read_time = setrtime();
}

void resolver_connect_udp(void *arg)
{
	context_arg *carg = arg;

	if (!carg || !carg->loop || alligator_stop_requested())
		return;

	carg->count = 0;
	carglog(carg, L_INFO, "%"u64": udp resolver connect %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	if (carg->lock) {
		if (carg->tt_timer)
			return;
		carg->lock = 0;
	}

	carg->lock = 1;
	carg->parsed = 0;

	char *addr = resolver_carg_get_addr(carg);
	if (!addr) {
		carg->lock = 0;
		return;
	}

	resolver_udp_stop_timer(carg);
	resolver_init_client_timer(carg, resolver_timeout_udp);

	uv_ip4_addr(addr, carg->numport, &carg->remote_addr);

	carg->udp_send.data = carg;
	memset(&carg->udp_client, 0, sizeof(carg->udp_client));
	carg->udp_client.data = carg;
	uv_udp_init(carg->loop, &carg->udp_client);

	int addr_ret = carg_set_socket_addr(&carg->local_addr, carg->bind_address, carg->bind_port);
	if (addr_ret) {
		int bind_ret = uv_udp_bind(&carg->udp_client, (const struct sockaddr *)carg->local_addr, 0);
		if (bind_ret) {
			carglog(carg, L_FATAL, "Bind udp socket '%s:%d' error %s\n", carg->bind_address ? carg->bind_address : "0.0.0.0", carg->bind_port, uv_strerror(bind_ret));
			resolver_udp_abort(carg, &carg->udp_client);
			return;
		}
	}

	int status = uv_udp_send(&carg->udp_send, &carg->udp_client, &carg->request_buffer, 1, (struct sockaddr *)&carg->remote_addr, resolver_send_udp);
	if (status) {
		carglog(carg, L_ERROR, "uv_udp_send error: %s\n", uv_strerror(status));
		resolver_udp_abort(carg, &carg->udp_client);
		return;
	}

	carg->write_bytes_counter += carg->request_buffer.len;
	(carg->write_counter)++;

	carg->write_time = setrtime();
	carg->connect_time = setrtime();
	carg->connect_time_finish = setrtime();
}
