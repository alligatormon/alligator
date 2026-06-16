#include <stdlib.h>
#include <uv.h>
#include "context_arg.h"
#include "events/debug.h"
#include "events/uv_alloc.h"
#include "common/entrypoint.h"
#include "parsers/http_proto.h"
#include "parsers/multiparser.h"
#include "events/metrics.h"
#include "events/access.h"
#include "common/logs.h"
#include "events/tls.h"
#include "common/rtime.h"
#include "common/http_entrypoint.h"
#include "main.h"
extern aconf *ac;

static void tcp_server_http_idle_cb(uv_timer_t *handle);
static void tcp_server_http_idle_arm(context_arg *carg);
static void tcp_server_http_idle_stop(context_arg *carg);
static void tcp_server_deferred_release_closed(uv_handle_t *handle);
static void tcp_server_deferred_release(uv_timer_t *timer);
static void tcp_server_release_client(context_arg *carg);
static void tcp_server_write_complete(context_arg *carg, int status);
static int tcp_server_try_dispatch(context_arg *carg);
void tcp_server_written(uv_write_t *req, int status);
void tls_server_written(uv_write_t *req, int status);
void tcp_server_shutdown_client(uv_shutdown_t *req, int status);

#define carglog_elapsed_ms(carg, when) getrtime_elapsed_ms((carg)->connect_time, (when))
#define carglog_elapsed_sec(carg, when) getrtime_sec_float((when), (carg)->connect_time)

static int tcp_server_send_response(context_arg *carg, string *str)
{
	context_arg *srv_carg = carg->srv_carg;
	char *write_body;
	int write_allocated = 0;
	int ret = 0;

	write_body = http_entrypoint_prepare_response(carg, str->s, str->l, &write_allocated);
	carg->write_req.data = carg;
	if (uv_is_writable((uv_stream_t*)&carg->client))
	{
		srv_carg->write_bytes_counter += str->l;
		carg->write_time = setrtime();
		carg->http_write_pending = 1;
		tcp_server_http_idle_stop(carg);
		{
			size_t wlen = write_allocated ? strlen(write_body) : str->l;

			if (!carg->tls)
			{
				carg->response_buffer = uv_buf_init(write_body, wlen);
				ret = uv_write(&carg->write_req, (uv_stream_t*)&carg->client, (const struct uv_buf_t *)&carg->response_buffer, 1, tcp_server_written);
			}
			else
			{
				if (write_allocated)
					carg->response_buffer = uv_buf_init(write_body, wlen);
				tls_write(carg, (uv_stream_t*)&carg->client, write_body, wlen, tls_server_written);
				ret = 0;
			}
		}
	}
	else
	{
		if (write_allocated)
			free(write_body);
		carg->http_write_pending = 0;
	}

	carg->buffer_response_size = str->m;
	if (!write_allocated)
		str->s = NULL;
	string_free(str);
	return ret;
}

static int tcp_server_try_dispatch(context_arg *carg)
{
	http_reply_data *hrdata;
	size_t msg_size;
	char *pastr;
	size_t paslen;
	string *str;

	if (carg->http_write_pending || uv_is_closing((uv_handle_t*)&carg->client))
		return 0;

	if (!carg->full_body || !carg->full_body->l)
		return 0;

	pastr = carg->full_body->s;
	paslen = carg->full_body->l;

	hrdata = http_proto_get_request_data(pastr, paslen, carg->auth_header);
	if (!hrdata)
		return 0;

	if (!http_entrypoint_request_complete(pastr, paslen, hrdata, &msg_size))
	{
		http_reply_data_free(hrdata);
		return 0;
	}

	carg->expect_body_length = msg_size;
	carg->body_read = 1;
	carg->http_request_size = msg_size;
	http_entrypoint_negotiate(carg, hrdata);
	http_reply_data_free(hrdata);

	str = string_init(carg->buffer_response_size);
	alligator_multiparser(pastr, paslen, carg->parser_handler, str, carg);

	if (!str->l)
	{
		string_free(str);
		http_entrypoint_reset_request_state(carg);
		return 0;
	}

	if (tcp_server_send_response(carg, str))
	{
		carg->http_write_pending = 0;
		return 0;
	}

	if (carg->http_close_after_response)
	{
		http_entrypoint_consume_request(carg);
		http_entrypoint_reset_request_state(carg);
	}

	return 1;
}

static void tcp_server_release_client(context_arg *carg)
{
	context_arg *srv_carg = carg->srv_carg;

	carglog(carg, L_INFO, "%"u64": tcp server closed client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);
	(srv_carg->close_counter)++;
	carg->close_time_finish = setrtime();

	if (carg->parser_handler && !carg->parser_status)
		carg->parser_status = 1;

	aggregator_events_metric_add(srv_carg, carg, NULL, "tcp", "entrypoint", srv_carg->key);

	if (!carg->body_read && carg->full_body)
		alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

	if (carg->tls)
		tls_client_cleanup(carg, 0);

	if (carg->full_body)
	{
		carg->buffer_request_size = carg->full_body->m;
		string_free(carg->full_body);
	}

	carg_uv_detach_timers(carg);
	free(carg);
}

static void tcp_server_deferred_release(uv_timer_t *timer)
{
	/* Closing-handle callbacks run after pending callbacks. */
	uv_close((uv_handle_t *)timer, tcp_server_deferred_release_closed);
}

static void tcp_server_deferred_release_closed(uv_handle_t *handle)
{
	context_arg *carg = handle ? handle->data : NULL;

	free(handle);
	if (carg)
		tcp_server_release_client(carg);
}

void tcp_server_closed_client(uv_handle_t* handle)
{
	context_arg *carg = handle->data;

	if (!carg)
		return;

	/* Detach before libuv finishes closing the embedded uv_tcp_t; free later. */
	handle->data = NULL;
	tcp_server_http_idle_stop(carg);

	if (!carg->loop)
	{
		tcp_server_release_client(carg);
		return;
	}

	{
		uv_timer_t *defer = malloc(sizeof(*defer));

		if (!defer)
		{
			tcp_server_release_client(carg);
			return;
		}

		uv_timer_init(carg->loop, defer);
		defer->data = carg;
		uv_timer_start(defer, tcp_server_deferred_release, 0, 0);
	}
}


void tcp_server_close_client(context_arg* carg)
{
	carglog(carg, L_INFO, "%"u64": tcp server call close client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	if (uv_is_closing((uv_handle_t*)&carg->client) == 0)
	{
		carg->close_time = setrtime();
		if (carg->tls)
		{
			if (carg->tls_handshake_done) {
				int ret = SSL_get_shutdown(carg->ssl);
				if (!ret)
				{
					do_tls_shutdown(carg, carg->ssl);
				}
			}
			uv_close((uv_handle_t*)&carg->client, tcp_server_closed_client);
		}
		else
		{
			uv_close((uv_handle_t*)&carg->client, tcp_server_closed_client);
		}
	}
}


static void tcp_server_begin_shutdown(context_arg *carg)
{
	if (!carg || uv_is_closing((uv_handle_t *)&carg->client))
		return;
	if (carg->shutdown_time.sec || carg->shutdown_time.nsec)
		return;

	carg->shutdown_req.data = carg;
	carg->shutdown_time = setrtime();
	if (uv_shutdown(&carg->shutdown_req, (uv_stream_t *)&carg->client, tcp_server_shutdown_client))
		tcp_server_close_client(carg);
}

void tcp_server_shutdown_client(uv_shutdown_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tcp server shutdowned client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, status: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	(srv_carg->shutdown_counter)++;
	carg->shutdown_time_finish = setrtime();

	tcp_server_close_client(carg);
	if (req != &carg->shutdown_req)
		free(req);
}



static void tcp_server_http_idle_cb(uv_timer_t *handle)
{
	context_arg *carg = handle->data;

	if (!carg)
		return;

	carglog(carg, L_INFO, "%"u64": http idle timeout, closing client %p\n", carg->count++, carg);
	tcp_server_close_client(carg);
}

static void tcp_server_http_idle_arm(context_arg *carg)
{
	const context_arg *policy;
	uint32_t sec;

	if (!carg || !carg->srv_carg)
		return;

	policy = carg->srv_carg;
	if (!policy->http_keepalive || carg->http_close_after_response)
		return;

	sec = policy->http_idle_timeout_sec;
	if (!sec)
		sec = HTTP_ENTRYPOINT_IDLE_TIMEOUT_DEFAULT_SEC;

	if (!carg->http_idle_timer)
	{
		carg->http_idle_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		if (!carg->http_idle_timer)
			return;
		uv_timer_init(carg->loop, carg->http_idle_timer);
		carg->http_idle_timer->data = carg;
		carg->http_idle_timer_active = 1;
	}

	uv_timer_start(carg->http_idle_timer, tcp_server_http_idle_cb, (uint64_t)sec * 1000, 0);
}

static void tcp_server_http_idle_stop(context_arg *carg)
{
	if (carg && carg->http_idle_timer_active && carg->http_idle_timer)
		uv_timer_stop(carg->http_idle_timer);
}

static void tcp_server_write_complete(context_arg *carg, int status)
{
	if (!carg)
		return;

	carg->http_write_pending = 0;

	if (carg->response_buffer.base && !strncmp(carg->response_buffer.base, "HTTP", 4))
	{
		if (http_entrypoint_should_shutdown(carg, status))
		{
			carglog(carg, L_INFO, "%"u64": tcp server call shutdown http client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);
			tcp_server_begin_shutdown(carg);
		}
		else
		{
			http_entrypoint_consume_request(carg);
			http_entrypoint_reset_request_state(carg);
			tcp_server_http_idle_arm(carg);
			tcp_server_try_dispatch(carg);
		}
	}
}

void tcp_server_written(uv_write_t* req, int status)
{
	context_arg *carg = req->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tcp server writed %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, status: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	(srv_carg->write_counter)++;
	carg->write_time_finish = setrtime();

	tcp_server_write_complete(carg, status);

	if (carg->response_buffer.base)
	{
		carglog(carg, L_INFO, "%"u64": tcp server writed call free %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, status: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
		free(carg->response_buffer.base);
		carg->response_buffer = uv_buf_init(NULL, 0);
	}
}


void tls_server_written(uv_write_t* req, int status) {
	context_arg *carg = req->data;
	if (status)
		carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] server data written %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d, write error: %s\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status, uv_strerror(status));
	else
		carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] server data written %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	tcp_server_write_complete(carg, status);
	free(carg->write_buffer.base);
	carg->write_buffer.len = 0;
	carg->write_buffer.base = 0;
	if (carg->response_buffer.base)
	{
		free(carg->response_buffer.base);
		carg->response_buffer = uv_buf_init(NULL, 0);
	}
}

void server_tcp_create_tls_client(context_arg *carg) {
	carg->ssl = SSL_new(carg->ssl_ctx);
	SSL_set_accept_state(carg->ssl);
	carg->rbio = BIO_new(BIO_s_mem());
	carg->wbio = BIO_new(BIO_s_mem());
	SSL_set_bio(carg->ssl, carg->rbio, carg->wbio);
}

int do_tls_handshake_server(context_arg *carg) {
	if (!carg->tls_handshake_done) {
		int hs_ret = SSL_accept(carg->ssl);

		if (hs_ret == 1) {
			carg->tls_handshake_done = 1;

			X509 *cert = SSL_get_peer_certificate(carg->ssl);
			if (cert) {
				x509_parse_cert(carg, cert, carg->host, carg->host);
				X509_free(cert);
			}

			return 1;
		}
		else if (hs_ret < 1) {
			int err = SSL_get_error(carg->ssl, hs_ret);
			if (err == SSL_ERROR_WANT_READ) {
				flush_tls_write(carg, (uv_stream_t *)&carg->client);
			} else if (err == SSL_ERROR_WANT_WRITE) {
				flush_tls_write(carg, (uv_stream_t *)&carg->client);
			} else {
				char *err = openssl_get_error_string();
				carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] handshake receive failed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, error: %s\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, err);
				free(err);

				return -1;
			}
			return 0;
		}
	}

	return 0;
}

void tcp_server_read_data(uv_stream_t* stream, ssize_t nread, char *base)
{
	context_arg* carg = (context_arg*)stream->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tcp server read %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, nread: %zd, EOF: %d, ECONNRESET: %d, ECONNABORTED: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread, (nread == UV_EOF), (nread == UV_ECONNRESET), (nread == UV_ECONNABORTED));
	(srv_carg->read_counter)++;
	carg->read_time_finish = setrtime();
	if (!carg->no_metric)
		entrypoint_read_metrics_throttled_push(srv_carg, carg, "tcp", 1, srv_carg->key);

	if (nread == 0)
		return;
	else if (nread == UV_EOF)
	{
		tcp_server_begin_shutdown(carg);
		return;
	}
	else if (nread == UV_ECONNRESET || nread == UV_ECONNABORTED)
	{
		tcp_server_close_client(carg);
		return;
	}

	if (nread < 0)
		return;

	if (!carg->full_body)
		carg->full_body = string_init(carg->buffer_request_size ? carg->buffer_request_size : 1024);

	if (base && nread > 0)
	{
		srv_carg->read_bytes_counter += nread;
		if (nread >= 65536)
			string_cat(carg->full_body, base, nread);
		else
			string_cat(carg->full_body, base, nread);
	}

	tcp_server_try_dispatch(carg);
}

void tcp_server_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
	tcp_server_read_data(stream, nread, buf ? buf->base : NULL);
}

void tls_server_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = stream->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tls server read %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, nread: %zd, is_closing: %d, handshake over: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread, uv_is_closing((uv_handle_t*)&carg->client), carg->tls_handshake_done);
	(srv_carg->tls_read_counter)++;
	carg->tls_read_time_finish = setrtime();

	if (nread <= 0)
	{
		tcp_server_read(stream, nread, 0);
		return;
	}

	carg->tls_read_bytes_counter += nread;

	BIO_write(carg->rbio, buf->base, nread);
	int handshaked = do_tls_handshake_server(carg);
	if (handshaked >= 0) {
		string *buffer = string_new();
		int read_size = 0;
		while ( 1 ) {
			char read_buffer[EVENT_BUFFER] = { 0 };
			read_size = SSL_read(carg->ssl, read_buffer, EVENT_BUFFER - 1);
			if (read_size <= 0) {
				break;
			}
			string_cat(buffer, read_buffer, read_size);
		}
		if (buffer->l > 0) {
			tcp_server_read_data(stream, buffer->l, buffer->s);
		}
		else {
			int err = SSL_get_error(carg->ssl, read_size);
			int need_shutdown = tls_io_check_shutdown_need(carg, err, read_size);
			if (need_shutdown == 1) {
				do_tls_shutdown(carg, carg->ssl);
				tcp_server_begin_shutdown(carg);
			} else if (need_shutdown == -1) {
				tcp_server_begin_shutdown(carg);
			}
		}
		string_free(buffer);
	}
	else {
		tcp_server_begin_shutdown(carg);
	}
}

void tls_server_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	context_arg* carg = handle->data;
	carg->user_read_buf.base = 0;
	carg->user_read_buf.len = 0;
	tcp_alloc(handle, suggested_size, &carg->user_read_buf);
	buf->base = carg->ssl_read_buffer;
	buf->len = sizeof(carg->ssl_read_buffer);
}

void tcp_server_connected(uv_stream_t* stream, int status) 
{
	context_arg* srv_carg = stream->data;

	if (status != 0)
		return;

	context_arg *carg = malloc(sizeof(*carg));
	memcpy(carg, srv_carg, sizeof(context_arg));
	carg_inherited_uv_reset(carg);
	carglog(carg, L_INFO, "%"u64": tcp server accepted on server %p, context %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, srv_carg, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);
	(srv_carg->conn_counter)++;

	uv_tcp_init(carg->loop, &carg->client);

	carg->srv_carg = srv_carg;
	carg->client.data = carg;
	carg->curr_ttl = carg->ttl;

	if (uv_tcp_nodelay(&carg->client, 1)) {
		tcp_server_close_client(carg);
		return;
	}

	if (uv_accept(stream, (uv_stream_t*)&carg->client)) {
		tcp_server_close_client(carg);
		return;
	}

	if (!check_ip_port((uv_tcp_t*)&carg->client, carg))
	{
		carglog(carg, L_ERROR, "no access!\n");
		tcp_server_close_client(carg);
		return;
	}

	carg->read_time = setrtime();
	carg->shutdown_time = (r_time){0, 0};
	carg->shutdown_time_finish = (r_time){0, 0};
	carg->close_time = (r_time){0, 0};
	carg->close_time_finish = (r_time){0, 0};
	carg->http_write_pending = 0;
	carg->http_close_after_response = 1;
	http_entrypoint_reset_request_state(carg);
	if (carg->tls)
	{
		carg->tls_read_time = setrtime();
		server_tcp_create_tls_client(carg);
		//do_tls_handshake_server(carg);

		if (uv_read_start((uv_stream_t*)&carg->client, tls_server_alloc, tls_server_read)) {
			tcp_server_close_client(carg);
			return;
		}
	}
	else
	{
		if (uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, tcp_server_read)) {
			tcp_server_close_client(carg);
			return;
		}
	}
}

static void tcp_entrypoint_server_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	if (carg && carg->threads && carg->loop)
		uv_stop(carg->loop);
}

static void tcp_entrypoint_stop_async_cb(uv_async_t *handle)
{
	context_arg *carg = handle->data;
	if (!carg || uv_is_closing((uv_handle_t *)&carg->server))
		return;
	uv_close((uv_handle_t *)&carg->server, tcp_entrypoint_server_closed);
}

static int tcp_server_setup(context_arg *srv_carg, uv_loop_t *loop)
{
	struct sockaddr_in addr;

	srv_carg->loop = loop;
	srv_carg->loop_allocated = 0;

	if (srv_carg->tls) {
		int rc = tls_context_init(srv_carg, SSLMODE_SERVER, srv_carg->tls_verify, srv_carg->tls_ca_file,
					  srv_carg->tls_cert_file, srv_carg->tls_key_file, NULL, NULL);
		if (!rc)
			srv_carg->running = -1;
	}

	if (uv_ip4_addr(srv_carg->host, srv_carg->numport, &addr))
		srv_carg->running = -1;

	uv_tcp_init_ex(srv_carg->loop, &srv_carg->server, AF_INET);
	uv_os_fd_t fd;
	int on = 1;
	uv_fileno((uv_handle_t *)&srv_carg->server, &fd);
	setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));

	if (uv_tcp_bind(&srv_carg->server, (const struct sockaddr *)&addr, 0))
		srv_carg->running = -1;

	if (uv_tcp_nodelay(&srv_carg->server, 1))
		srv_carg->running = -1;

	if (uv_listen((uv_stream_t *)&srv_carg->server, 1024, tcp_server_connected)) {
		carglog(srv_carg, L_FATAL, "Listen '%s:%d' error\n", srv_carg->host, srv_carg->numport);
		srv_carg->running = -1;
	}

	if (srv_carg->running != -1)
		srv_carg->running = 1;

	return srv_carg->running == 1;
}

void tcp_server_run(void *passarg) {
	context_arg *srv_carg = passarg;

	srv_carg->loop = malloc(sizeof *srv_carg->loop);
	srv_carg->loop_allocated = 1;
	uv_loop_init(srv_carg->loop);
	uv_async_init(srv_carg->loop, &srv_carg->entrypoint_stop_async, tcp_entrypoint_stop_async_cb);
	srv_carg->entrypoint_stop_async.data = srv_carg;
	srv_carg->entrypoint_stop_async_ready = 1;

	if (!tcp_server_setup(srv_carg, srv_carg->loop))
		goto done;

	uv_run(srv_carg->loop, UV_RUN_DEFAULT);

done:
	if (srv_carg->entrypoint_stop_async_ready && !uv_is_closing((uv_handle_t *)&srv_carg->entrypoint_stop_async))
		uv_close((uv_handle_t *)&srv_carg->entrypoint_stop_async, NULL);
	uv_run(srv_carg->loop, UV_RUN_NOWAIT);
	carg_free(srv_carg);
}

int tcp_server_init(uv_loop_t *loop, const char* ip, int port, uint8_t tls, context_arg *import_carg)
{
	for (uint64_t i = 0; i < import_carg->threads; ++i) {
		context_arg* srv_carg = NULL;
		if (import_carg) {
			srv_carg = carg_copy(import_carg);
		} else {
			srv_carg = calloc(1, sizeof(context_arg));
		}

		srv_carg->numport = port;
		strlcpy(srv_carg->host, ip, HOSTHEADER_SIZE);

		srv_carg->tls = tls;
		srv_carg->server.data = srv_carg;

		entrypoint_carg_replace_key(srv_carg, "tcp:%" PRIu64 ":%s:%u", i, srv_carg->host, port);
		carglog(srv_carg, L_INFO, "init server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", loop, tls, srv_carg, srv_carg->host, port);

		uv_thread_create(&srv_carg->thread, tcp_server_run, srv_carg);
		alligator_ht_insert(ac->entrypoints, &(srv_carg->context_node), srv_carg, tommy_strhash_u32(0, srv_carg->key));
	}
	if (!import_carg->threads) {
		context_arg* srv_carg = NULL;
		if (import_carg) {
			srv_carg = carg_copy(import_carg);
		} else {
			srv_carg = calloc(1, sizeof(context_arg));
		}

		srv_carg->numport = port;
		strlcpy(srv_carg->host, ip, HOSTHEADER_SIZE);

		srv_carg->tls = tls;
		srv_carg->server.data = srv_carg;

		entrypoint_carg_replace_key(srv_carg, "tcp:0:%s:%u", srv_carg->host, port);
		carglog(srv_carg, L_INFO, "init server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", loop, tls, srv_carg, srv_carg->host, port);

		if (tcp_server_setup(srv_carg, loop))
			alligator_ht_insert(ac->entrypoints, &(srv_carg->context_node), srv_carg, tommy_strhash_u32(0, srv_carg->key));
		else
			carg_free(srv_carg);
	}
	return 1;
}

static void tcp_entrypoint_server_closed_default(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	carg_free(carg);
}

void tcp_server_stop(const char* ip, int port)
{
	context_arg **matches = NULL;
	size_t n = entrypoint_collect_transport("tcp", ip, (uint16_t)port, &matches);
	size_t i;

	for (i = 0; i < n; ++i) {
		context_arg *carg = matches[i];
		if (!carg)
			continue;
		alligator_ht_remove_existing(ac->entrypoints, &(carg->context_node));
		if (carg->threads && carg->entrypoint_stop_async_ready)
			uv_async_send(&carg->entrypoint_stop_async);
		else if (!uv_is_closing((uv_handle_t *)&carg->server))
			uv_close((uv_handle_t *)&carg->server, tcp_entrypoint_server_closed_default);
	}

	free(matches);
}

//int main()
//{
//	context_arg* carg = tcp_server_init(uv_default_loop(), "0.0.0.0", 444, 1);
//	uv_run(uv_default_loop(), UV_RUN_DEFAULT);
//}
