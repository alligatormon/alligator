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
#include "events/proxy.h"
#include "events/uv_alloc.h"

extern aconf *ac;

#define carglog_elapsed_ms(carg, when) getrtime_elapsed_ms((carg)->connect_time, (when))
#define carglog_elapsed_sec(carg, when) getrtime_sec_float((when), (carg)->connect_time)

void udp_client_repeat_period(uv_timer_t *timer);
static void udp_socks_tcp_closed(uv_handle_t *handle);
static void udp_session_finish(context_arg *carg);

static void udp_session_finish(context_arg *carg)
{
	(carg->close_counter)++;
	carg->lock = 0;
	carg->proxy_udp_associate = 0;
	carg->proxy_udp_control = 0;

	aggregator_events_metric_add(carg, carg, NULL, "udp", "aggregator", carg->host);

	if (carg->context_ttl)
	{
		r_time time = setrtime();
		if (time.sec >= carg->context_ttl)
		{
			carg->remove_from_hash = 1;
			smart_aggregator_del(carg);
		}
	}
	else if (carg->period && carg->period_timer) {
		uv_timer_stop(carg->period_timer);
		uv_timer_start(carg->period_timer, udp_client_repeat_period, carg->period, 0);
	}
}

static void udp_client_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;

	if (carg->write_buffer.base && carg->write_buffer.base != carg->request_buffer.base) {
		free(carg->write_buffer.base);
		carg->write_buffer.base = NULL;
		carg->write_buffer.len = 0;
	}

	if (carg->proxy_udp_control && carg->client.type == UV_TCP && !uv_is_closing((uv_handle_t *)&carg->client)) {
		uv_read_stop((uv_stream_t *)&carg->client);
		uv_close((uv_handle_t *)&carg->client, udp_socks_tcp_closed);
		return;
	}

	udp_session_finish(carg);
}

static void udp_socks_tcp_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	carg->proxy_udp_control = 0;
	udp_session_finish(carg);
}

void udp_close_client(context_arg *carg, const uv_buf_t *buf)
{
	if (carg->tt_timer) {
		uv_timer_stop(carg->tt_timer);
		carg->tt_timer->data = NULL;
		alligator_cache_push(ac->uv_cache_timer, carg->tt_timer);
		carg->tt_timer = NULL;
	}

	if (buf && buf->base)
		free(buf->base);

	/* Client handle must have been uv_udp_init'd. Keep lock until closed
	 * callback so crawl cannot reconnect a oneshot mid-teardown. */
	if (carg->udp_client.type == UV_UDP && !uv_is_closing((uv_handle_t *)&carg->udp_client)) {
		uv_udp_recv_stop(&carg->udp_client);
		uv_close((uv_handle_t *)&carg->udp_client, udp_client_closed);
		return;
	}

	if (carg->proxy_udp_control && carg->client.type == UV_TCP && !uv_is_closing((uv_handle_t *)&carg->client)) {
		uv_read_stop((uv_stream_t *)&carg->client);
		uv_close((uv_handle_t *)&carg->client, udp_socks_tcp_closed);
		return;
	}

	udp_session_finish(carg);
}

void udp_on_read(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
	context_arg *carg = req->data;
	carg->read_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": udp read %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	/* Entrypoint server: parse datagram and keep listening. */
	if (req == &carg->udp_server) {
		if (nread > 0) {
			if (!check_udp_ip_port(addr, carg)) {
				carglog(carg, L_ERROR, "no access!\n");
			} else {
				(carg->conn_counter)++;
				(carg->read_counter)++;
				carg->read_bytes_counter += (uint64_t)nread;
				alligator_multiparser(buf->base, nread, carg->parser_handler, NULL, carg);
				if (!carg->no_metric)
					entrypoint_read_metrics_throttled_push(carg, carg, "udp", 1, carg->key);
			}
		}
		if (buf && buf->base)
			free(buf->base);
		return;
	}

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

	if (!(carg->proxy && carg->proxy->type == PROXY_TYPE_SOCKS5) && !check_udp_ip_port(addr, carg))
	{
		carglog(carg, L_ERROR, "no access!\n");
		udp_close_client(carg, buf);
		return;
	}

	const char *parse_base = buf ? buf->base : NULL;
	ssize_t parse_len = nread;
	if (nread > 0 && carg->proxy && carg->proxy->type == PROXY_TYPE_SOCKS5) {
		const unsigned char *payload = NULL;
		size_t payload_len = 0;
		if (proxy_udp_unwrap((const unsigned char *)buf->base, (size_t)nread, &payload, &payload_len) < 0) {
			carglog(carg, L_ERROR, "SOCKS5 UDP unwrap failed\n");
			udp_close_client(carg, buf);
			return;
		}
		parse_base = (const char *)payload;
		parse_len = (ssize_t)payload_len;
	}

	if (parse_len > 0)
	{
		(carg->conn_counter)++;
		(carg->read_counter)++;
		carg->read_bytes_counter += (uint64_t)parse_len;
	}

	alligator_multiparser((char *)parse_base, parse_len, carg->parser_handler, NULL, carg);

	if (nread > 0 && !carg->no_metric && !carg->lock)
		entrypoint_read_metrics_throttled_push(carg, carg, "udp", 1, carg->key);
	if (carg->lock)
		aggregator_events_metric_add(carg, carg, NULL, "udp", "entrypoint", carg->key);

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

static void udp_socks_tcp_connected(uv_connect_t *req, int status)
{
	context_arg *carg = req->data;

	if (status < 0) {
		carglog(carg, L_ERROR, "SOCKS5 UDP TCP connect failed key %s: %s\n",
			carg->key ? carg->key : "?", uv_strerror(status));
		udp_client_socks_fail(carg);
		return;
	}
	carg->connect_time_finish = setrtime();
	proxy_handshake_start(carg);
}

void udp_client_socks_fail(context_arg *carg)
{
	if (!carg)
		return;
	udp_close_client(carg, NULL);
}

void udp_client_socks_relay_start(context_arg *carg)
{
	unsigned char *wrapped;
	size_t cap;
	size_t n;
	int addr_ret;

	if (!carg)
		return;

	carg->udp_send.data = carg;
	uv_udp_init(carg->loop, &carg->udp_client);
	carg->udp_client.data = carg;

	addr_ret = carg_set_socket_addr(&carg->local_addr, carg->bind_address, carg->bind_port);
	if (addr_ret) {
		int bind_ret = uv_udp_bind(&carg->udp_client, (const struct sockaddr *)carg->local_addr, 0);
		if (bind_ret) {
			carglog(carg, L_FATAL, "Bind udp socket '%s:%d' error %s\n", carg->bind_address ? carg->bind_address : "0.0.0.0", carg->bind_port, uv_strerror(bind_ret));
			udp_client_socks_fail(carg);
			return;
		}
	}

	cap = carg->request_buffer.len + 512;
	wrapped = malloc(cap);
	if (!wrapped) {
		udp_client_socks_fail(carg);
		return;
	}
	n = proxy_udp_wrap(wrapped, cap, carg->host, carg->numport,
		(const unsigned char *)carg->request_buffer.base, carg->request_buffer.len);
	if (!n) {
		free(wrapped);
		udp_client_socks_fail(carg);
		return;
	}
	if (carg->write_buffer.base && carg->write_buffer.base != carg->request_buffer.base)
		free(carg->write_buffer.base);
	carg->write_buffer = uv_buf_init((char *)wrapped, n);
	uv_udp_send(&carg->udp_send, &carg->udp_client, &carg->write_buffer, 1,
		(struct sockaddr *)&carg->proxy_udp_relay, udp_on_send);
	carg->write_time = setrtime();
}

void udp_client_connect(void *arg)
{
	context_arg *carg = arg;
	carg->count = 0;
	carglog(carg, L_INFO, "%"u64": udp client connect %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	if (carg->lock)
		return;
	if (cluster_come_later(carg))
		return;

	if (carg->proxy && !proxy_ok_for_transport(carg->proxy, carg->transport)) {
		carglog(carg, L_ERROR, "proxy: HTTP/HTTPS proxy is not supported for UDP (use socks5://) key %s\n",
			carg->key ? carg->key : "?");
		return;
	}

	carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);
	proxy_handshake_reset(carg);

	if (carg->period && !carg->close_counter) {
		carg->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->period_timer->data = carg;
		uv_timer_init(carg->loop, carg->period_timer);
		uv_timer_start(carg->period_timer, udp_client_repeat_period, carg->period, 0);
	}

	if (carg->proxy && carg->proxy->type == PROXY_TYPE_SOCKS5) {
		string *pdata = aggregator_get_addr(carg, carg->proxy->host, DNS_TYPE_A, DNS_CLASS_IN);
		if (!pdata)
			return;

		carg->lock = 1;
		carg->parsed = 0;
		carg->parser_status = 0;
		carg->curr_ttl = carg->ttl;
		carg->proxy_udp_associate = 1;

		carg->tt_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->tt_timer->data = carg;
		uv_timer_init(carg->loop, carg->tt_timer);
		uv_timer_start(carg->tt_timer, udp_timeout_timer, carg->timeout, 0);

		memset(&carg->connect, 0, sizeof(carg->connect));
		carg->connect.data = carg;
		memset(&carg->client, 0, sizeof(carg->client));
		carg->client.data = carg;
		uv_tcp_init(carg->loop, &carg->client);
		carg->proxy_udp_control = 1;
		uv_ip4_addr(pdata->s, carg->proxy->numport, &carg->remote_addr);
		carg->connect_time = setrtime();
		uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)&carg->remote_addr, udp_socks_tcp_connected);
		return;
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
	carg->udp_client.data = carg;

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
	if (!carg || carg->remove_from_hash)
		return;

	/* oneshot: context_ttl set at create; connect once while unlocked and not yet closed */
	if (carg->context_ttl) {
		if (carg->lock || carg->close_counter)
			return;
		udp_client_connect(arg);
		return;
	}

	if (carg->period && carg->close_counter)
		return;

	udp_client_connect(arg);
}

char* udp_client(void *arg)
{
	if (!arg)
		return NULL;

	context_arg *carg = arg;
	alligator_ht_insert(ac->udpaggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	if (!carg->context_ttl)
		aggregator_get_addr(carg, carg->host, DNS_TYPE_A, DNS_CLASS_IN);
	return "udp";
}

void udp_client_del(context_arg *carg)
{
	if (!carg)
		return;

	if (carg->lock)
	{
		/* Active: schedule TTL expiry and close. Hash removal / free happen on
		 * the unlocked path from udp_client_closed (same as TCP). */
		r_time time = setrtime();
		carg->context_ttl = time.sec;
		udp_close_client(carg, NULL);
		return;
	}

	carg->lock = 1;

	if (carg->remove_from_hash)
		alligator_ht_remove_existing(ac->aggregators, &(carg->context_node));

	alligator_ht_remove_existing(ac->udpaggregator, &(carg->node));
	carg_free(carg);
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
