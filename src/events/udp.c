#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/entrypoint.h"
#include "events/metrics.h"
#include "resolver/resolver.h"
#include "events/access.h"
#include "cluster/later.h"
#include "parsers/multiparser.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "main.h"
#include "common/rtime.h"

extern aconf *ac;

#define carglog_elapsed_ms(carg, when) getrtime_elapsed_ms((carg)->connect_time, (when))
#define carglog_elapsed_sec(carg, when) getrtime_sec_float((when), (carg)->connect_time)

void udp_close_client(context_arg *carg, const uv_buf_t *buf)
{
	uv_udp_recv_stop(&carg->udp_client);

	if (carg->tt_timer) {
		uv_timer_stop(carg->tt_timer);
		carg->tt_timer->data = NULL;
		alligator_cache_push(ac->uv_cache_timer, carg->tt_timer);
		carg->tt_timer = NULL;
	}

	carg->lock = 0;

	if (carg->context_ttl)
	{
		r_time time = setrtime();
		if (time.sec >= carg->context_ttl)
		{
			carg->remove_from_hash = 1;
			if (carg->key)
				alligator_ht_remove(ac->udpaggregator, aggregator_compare, carg->key, tommy_strhash_u32(0, carg->key));
			smart_aggregator_del(carg);
			if (buf && buf->base)
				free(buf->base);
			return;
		}
	}
	else {
		if (carg->period)
			uv_timer_set_repeat(carg->period_timer, carg->period);
	}

	if (buf && buf->base)
		free(buf->base);

	if (carg->key)
		alligator_ht_remove(ac->udpaggregator, aggregator_compare, carg->key, tommy_strhash_u32(0, carg->key));
}

void udp_on_read(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
	context_arg *carg = req->data;
	carg->read_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": udp read %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	if (nread < 0)
	{
		carglog(carg, L_ERROR, "Read error %s\n", uv_err_name(nread));
		udp_close_client(carg, buf);
		return;
	}
	if (nread == 0 && buf && buf->base)
	{
		udp_close_client(carg, buf);
		return;
	}

	if (!check_udp_ip_port(addr, carg))
	{
		carglog(carg, L_ERROR, "no access!\n");
		udp_close_client(carg, buf);
		return;
	}

	if (nread > 0)
	{
		(carg->conn_counter)++;
		(carg->read_counter)++;
		carg->read_bytes_counter += (uint64_t)nread;
	}

	alligator_multiparser(buf->base, nread, carg->parser_handler, NULL, carg);

	if (nread > 0 && !carg->no_metric && !carg->lock)
		entrypoint_read_metrics_throttled_push(carg, carg, "udp", 1, carg->key);
	if (carg->lock)
	{
		uv_udp_recv_stop(req);
		uv_close((uv_handle_t*) req, NULL);
		aggregator_events_metric_add(carg, carg, NULL, "udp", "entrypoint", carg->key);
	}

	udp_close_client(carg, buf);
}

void udp_timeout_timer(uv_timer_t *timer)
{
	context_arg *carg = timer->data;

	uv_timer_stop(timer);
	if (carg)
		carg->tt_timer = NULL;
	alligator_cache_push(ac->uv_cache_timer, timer);

	if (!carg)
		return;

	r_time timeout_now = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] timeout udp client %p(%p:%p) with key %s, hostname %s,  tls: %d, timeout: %"u64"\n", carg->count++, carglog_elapsed_ms(carg, timeout_now), carglog_elapsed_sec(carg, timeout_now), carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->timeout);
	(carg->timeout_counter)++;

	udp_close_client(carg, NULL);
}

void udp_on_send(uv_udp_send_t* req, int status) {
	context_arg *carg = req->data;
	if (status != 0) {
		carglog(carg, L_ERROR, "send_cb error: %s\n", uv_strerror(status));
	}
	carg->write_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": udp sent %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	req->handle->data = req->data;

	uv_udp_recv_start(req->handle, alloc_buffer, udp_on_read);
	carg->read_time = setrtime();
	//free(req);
}

static void udp_entrypoint_server_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	if (carg && carg->threads && carg->loop)
		uv_stop(carg->loop);
}

static void udp_entrypoint_server_closed_default(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	carg_free(carg);
}

static void udp_entrypoint_stop_async_cb(uv_async_t *handle)
{
	context_arg *carg = handle->data;
	if (!carg || uv_is_closing((uv_handle_t *)&carg->udp_server))
		return;
	uv_udp_recv_stop(&carg->udp_server);
	uv_close((uv_handle_t *)&carg->udp_server, udp_entrypoint_server_closed);
}

void udp_server_run(void *passarg) {
	context_arg *carg = passarg;

	if (carg->threads) {
		carg->loop = malloc(sizeof *carg->loop);
		carg->loop_allocated = 1;
		uv_loop_init(carg->loop); // TODO: need to be freed in carg_free
		uv_async_init(carg->loop, &carg->entrypoint_stop_async, udp_entrypoint_stop_async_cb);
		carg->entrypoint_stop_async.data = carg;
		carg->entrypoint_stop_async_ready = 1;
	} else {
		carg->loop = uv_default_loop();
	}

	// after update libuv to versions 1.49.2+ remove this block to the UV_TCP_REUSEPORT flag in uv_tcp_bind
	uv_udp_init_ex(carg->loop, &carg->udp_server, AF_INET);
	uv_os_fd_t fd;
	int on = 1;
	uv_fileno((const uv_handle_t *)&carg->udp_server, &fd);
	setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
	//

	struct sockaddr_in *recv_addr = carg->local_addr = calloc(1, sizeof(*recv_addr));
	uv_ip4_addr(carg->host, carg->numport, recv_addr);
	int ret = uv_udp_bind(&carg->udp_server, (const struct sockaddr *)carg->local_addr, 0);
    if (ret) {
		carglog(carg, L_FATAL, "Listen udp socket '%s:%d' error %s\n", carg->host, carg->numport, uv_strerror(ret));
	}
	carg->udp_server.data = carg;
	uv_udp_recv_start(&carg->udp_server, alloc_buffer, udp_on_read);

	carg->running = 1;
	if (carg->threads) {
		uv_run(carg->loop, UV_RUN_DEFAULT);
		if (carg->entrypoint_stop_async_ready && !uv_is_closing((uv_handle_t *)&carg->entrypoint_stop_async))
			uv_close((uv_handle_t *)&carg->entrypoint_stop_async, NULL);
		uv_run(carg->loop, UV_RUN_NOWAIT);
		carg_free(carg);
	}
}

void udp_server_init(uv_loop_t *loop, const char* addr, uint16_t port, uint8_t tls, context_arg *import_carg)
{
	for (uint64_t i = 0; i < import_carg->threads; ++i) {
		context_arg* carg = NULL;
		if (import_carg) {
			carg = carg_copy(import_carg);
		} else {
			carg = calloc(1, sizeof(context_arg));
		}

		carg->numport = port;
		carg->curr_ttl = carg->ttl;
		strlcpy(carg->host, addr, HOSTHEADER_SIZE);

		carg->tls = tls;
		carg->udp_server.data = carg;

		entrypoint_carg_replace_key(carg, "udp:%" PRIu64 ":%s:%u", i, carg->host, port);
		carglog(carg, L_INFO, "init udp server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", loop, tls, carg, carg->host, port);

		uv_thread_create(&carg->thread, udp_server_run, carg);
		alligator_ht_insert(ac->entrypoints, &(carg->context_node), carg, tommy_strhash_u32(0, carg->key));
	}
	if (!import_carg->threads) {
		context_arg* carg = NULL;
		if (import_carg) {
			carg = carg_copy(import_carg);
		} else {
			carg = calloc(1, sizeof(context_arg));
		}

		carg->numport = port;
		carg->curr_ttl = carg->ttl;
		strlcpy(carg->host, addr, HOSTHEADER_SIZE);

		carg->tls = tls;
		carg->udp_server.data = carg;

		entrypoint_carg_replace_key(carg, "udp:0:%s:%u", carg->host, port);
		carglog(carg, L_INFO, "init udp server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", loop, tls, carg, carg->host, port);

		udp_server_run(carg);
		alligator_ht_insert(ac->entrypoints, &(carg->context_node), carg, tommy_strhash_u32(0, carg->key));
	}
}

void udp_server_stop(const char* addr, uint16_t port)
{
	context_arg **matches = NULL;
	size_t n = entrypoint_collect_transport("udp", addr, port, &matches);
	size_t i;

	for (i = 0; i < n; ++i) {
		context_arg *carg = matches[i];
		if (!carg)
			continue;
		alligator_ht_remove_existing(ac->entrypoints, &(carg->context_node));
		if (carg->threads && carg->entrypoint_stop_async_ready) {
			uv_async_send(&carg->entrypoint_stop_async);
		} else if (!uv_is_closing((uv_handle_t *)&carg->udp_server)) {
			uv_udp_recv_stop(&carg->udp_server);
			uv_close((uv_handle_t *)&carg->udp_server, udp_entrypoint_server_closed_default);
		}
	}

	free(matches);
}

void udp_client_repeat_period(uv_timer_t *timer);
void udp_client_connect(void *arg)
{
	context_arg *carg = arg;
	carg->count = 0;
	carglog(carg, L_INFO, "%"u64": udp client connect %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	if (carg->lock)
		return;
	if (cluster_come_later(carg))
		return;

	carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);

	if (carg->period && !carg->conn_counter) {
		carg->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->period_timer->data = carg;
		uv_timer_init(carg->loop, carg->period_timer);
		uv_timer_start(carg->period_timer, udp_client_repeat_period, carg->period, 0);
	}

	carg->lock = 1;
	carg->parsed = 0;
	carg->parser_status = 0;
	carg->curr_ttl = carg->ttl;

	string *data = aggregator_get_addr(carg, carg->host, DNS_TYPE_A, DNS_CLASS_IN);
	if (!data)
	{
		carg->lock = 0;
		return;
	}

	uv_ip4_addr(data->s, carg->numport, &carg->remote_addr);

	carg->tt_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
	carg->tt_timer->data = carg;
	uv_timer_init(carg->loop, carg->tt_timer);
	uv_timer_start(carg->tt_timer, udp_timeout_timer, carg->timeout, 0);


	carg->udp_send.data = carg;
	uv_udp_init(carg->loop, &carg->udp_client);

	int addr_ret = carg_set_socket_addr(&carg->local_addr, carg->bind_address, carg->bind_port);
	if (addr_ret) {
		int bind_ret = uv_udp_bind(&carg->udp_client, (const struct sockaddr *)carg->local_addr, 0);
		if (bind_ret) {
			carglog(carg, L_FATAL, "Bind udp socket '%s:%d' error %s\n", carg->bind_address ? carg->bind_address : "0.0.0.0", carg->bind_port, uv_strerror(bind_ret));
			carg->lock = 0;
			return;
		}
	}


	uv_udp_send(&carg->udp_send, &carg->udp_client, &carg->request_buffer, 1, (struct sockaddr *)&carg->remote_addr, udp_on_send);
	carg->write_time = setrtime();
}

void udp_client_repeat_period(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (!carg->period)
		return;

	udp_client_connect((void*)carg);
}

void for_udp_client_connect(void *arg)
{
	context_arg *carg = arg;
	if (!carg || carg->context_ttl || carg->remove_from_hash)
		return;
	if (carg->period && carg->conn_counter)
		return;

	udp_client_connect(arg);
}

char* udp_client(void *arg)
{
	if (!arg)
		return NULL;

	context_arg *carg = arg;
	aggregator_get_addr(carg, carg->host, DNS_TYPE_A, DNS_CLASS_IN);

	alligator_ht_insert(ac->udpaggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "udp";
}

void udp_client_del(context_arg *carg)
{
	if (!carg)
		return;

	uv_udp_recv_stop(&carg->udp_client);
	if (carg->remove_from_hash)
		alligator_ht_remove_existing(ac->aggregators, &(carg->context_node));

	if (carg->lock)
	{
		r_time time = setrtime();
		carg->context_ttl = time.sec;
		alligator_ht_remove_existing(ac->udpaggregator, &(carg->node));
		uv_udp_recv_stop(&carg->udp_client);
	}
	else {
		carg->lock = 1;
		alligator_ht_remove_existing(ac->udpaggregator, &(carg->node));
		carg_free(carg);
	}
}

static void udp_client_crawl(uv_timer_t* handle) {
	(void)handle;
	alligator_ht_foreach(ac->udpaggregator, for_udp_client_connect);
}

void udp_client_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->udp_client_timer);
	uv_timer_start(&ac->udp_client_timer, udp_client_crawl, ac->aggregator_startup, ac->aggregator_repeat);
}
