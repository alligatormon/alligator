#include "common/log_http.h"
#include "events/context_arg.h"
#include "events/tls.h"
#include "main.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>

enum {
	LOG_HTTP_IDLE = 0,
	LOG_HTTP_CONNECTING,
	LOG_HTTP_CONNECTED,
};

typedef struct log_http_wr {
	uv_write_t req;
	char *data;
	size_t len;
	log_http_sink *sink;
} log_http_wr;

struct log_http_sink {
	uv_tcp_t tcp;
	uv_connect_t connect_req;
	uv_timer_t reconnect_timer;
	uv_async_t write_async;
	uv_getaddrinfo_t getaddrinfo_req;
	char *host;
	char *path;
	char *auth_basic_b64;
	char *tls_cert_file;
	char *tls_key_file;
	char *tls_ca_file;
	context_arg *tls_carg;
	int port;
	int state;
	uint8_t use_tls;
	uint8_t tls_verify;
	uint8_t tls_verify_defined;
	uint8_t write_in_flight;
	uint8_t response_pending;
	uint8_t closing;
	uint8_t uv_ready;
	uint8_t tcp_initialized;
	uint8_t pending_closes;
	uint8_t aux_closes_started;
	uint8_t pending_gai;
	char *pending_body;
	size_t pending_body_len;
	char *pending_request;
	size_t pending_request_len;
	pthread_mutex_t lock;
};

extern aconf *ac;

static struct addrinfo log_http_gai_hints;

static void log_http_closed_cb(uv_handle_t *handle);
static void log_http_aux_closed_cb(uv_handle_t *handle);
static void log_http_reconnect_cb(uv_timer_t *timer);
static void log_http_start_connect(log_http_sink *sink);
static void log_http_mark_disconnected(log_http_sink *sink);
static void log_http_try_write(log_http_sink *sink);
static void log_http_sink_destroy(log_http_sink *sink);
static int log_http_is_connected(log_http_sink *sink);
static void log_http_schedule_drain(log_http_sink *sink);
static char *log_http_build_request(log_http_sink *sink, const char *body, size_t bodylen,
    size_t *outlen);
static void log_http_tls_cleanup(log_http_sink *sink);
static int log_http_tls_start(log_http_sink *sink);
static int log_http_tls_drive(log_http_sink *sink);
static void log_http_tls_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf);
static void log_http_tls_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
static void log_http_tls_written_cb(uv_write_t *req, int status);
static void log_http_handshake_timeout_cb(uv_timer_t *timer);
static void log_http_arm_handshake_timeout(log_http_sink *sink);
static void log_http_connect_failed(log_http_sink *sink);
static void log_http_shutdown_next(log_http_sink *sink);

static void log_http_close_handle(log_http_sink *sink, uv_handle_t *handle, uv_close_cb cb)
{
	if (!sink || !handle || uv_is_closing(handle))
		return;
	if (handle == (uv_handle_t *)&sink->tcp && !sink->tcp_initialized)
		return;
	sink->pending_closes++;
	uv_close(handle, cb);
}

static void log_http_pending_drop(log_http_sink *sink)
{
	char *body;
	char *request;

	if (!sink)
		return;

	pthread_mutex_lock(&sink->lock);
	body = sink->pending_body;
	request = sink->pending_request;
	sink->pending_body = NULL;
	sink->pending_body_len = 0;
	sink->pending_request = NULL;
	sink->pending_request_len = 0;
	pthread_mutex_unlock(&sink->lock);

	free(body);
	free(request);
}

static void log_http_schedule_reconnect(log_http_sink *sink)
{
	if (!sink || sink->closing || !sink->uv_ready)
		return;

	uv_timer_start(&sink->reconnect_timer, log_http_reconnect_cb,
	    LOG_HTTP_RECONNECT_MS, 0);
}

static void log_http_schedule_drain(log_http_sink *sink)
{
	if (!sink || sink->closing || !sink->uv_ready)
		return;

	if (log_http_is_connected(sink))
		uv_async_send(&sink->write_async);
}

static int log_http_is_connected(log_http_sink *sink)
{
	int connected;

	if (!sink)
		return 0;

	pthread_mutex_lock(&sink->lock);
	connected = sink->state == LOG_HTTP_CONNECTED;
	pthread_mutex_unlock(&sink->lock);
	return connected;
}

static char *log_http_build_request(log_http_sink *sink, const char *body, size_t bodylen,
    size_t *outlen)
{
	char *req;
	size_t hdrlen;
	int n;
	const char *auth_header = "";

	if (!sink || !body || !bodylen || !outlen)
		return NULL;

	if (sink->auth_basic_b64 && sink->auth_basic_b64[0])
		auth_header = sink->auth_basic_b64;

	n = snprintf(NULL, 0,
	    "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/x-ndjson\r\nContent-Length: %zu\r\nConnection: keep-alive\r\n%s\r\n",
	    sink->path ? sink->path : "/", sink->host ? sink->host : "localhost",
	    sink->port, bodylen, auth_header);
	if (n < 0)
		return NULL;

	hdrlen = (size_t)n;
	req = malloc(hdrlen + bodylen + 1);
	if (!req)
		return NULL;

	snprintf(req, hdrlen + 1,
	    "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/x-ndjson\r\nContent-Length: %zu\r\nConnection: keep-alive\r\n%s\r\n",
	    sink->path ? sink->path : "/", sink->host ? sink->host : "localhost",
	    sink->port, bodylen, auth_header);
	memcpy(req + hdrlen, body, bodylen);
	req[hdrlen + bodylen] = '\0';
	*outlen = hdrlen + bodylen;
	return req;
}

static void log_http_tls_cleanup(log_http_sink *sink)
{
	if (!sink || !sink->tls_carg)
		return;

	/*
	 * Free SSL state only. Do not free write_buffer here: tls_write may have
	 * handed it to an in-flight uv_write; log_http_tls_written_cb owns that
	 * free. Destroy frees any leftover after handles are closed.
	 */
	tls_client_cleanup(sink->tls_carg, 1);
}

static void log_http_arm_handshake_timeout(log_http_sink *sink)
{
	if (!sink || sink->closing || !sink->uv_ready)
		return;

	uv_timer_start(&sink->reconnect_timer, log_http_handshake_timeout_cb,
	    LOG_HTTP_HANDSHAKE_MS, 0);
}

static void log_http_handshake_timeout_cb(uv_timer_t *timer)
{
	log_http_sink *sink = timer->data;

	if (!sink || sink->closing)
		return;

	if (sink->state != LOG_HTTP_CONNECTING || !sink->use_tls)
		return;
	if (sink->tls_carg && sink->tls_carg->tls_handshake_done)
		return;

	glog(L_ERROR, "log http: TLS handshake timed out host=%s port=%d\n",
	    sink->host ? sink->host : "?", sink->port);
	log_http_mark_disconnected(sink);
}

/* Fail connect/DNS setup: close TCP so closed_cb reconnects (never re-init live handle). */
static void log_http_connect_failed(log_http_sink *sink)
{
	if (!sink || sink->closing)
		return;

	pthread_mutex_lock(&sink->lock);
	sink->state = LOG_HTTP_IDLE;
	sink->write_in_flight = 0;
	sink->response_pending = 0;
	pthread_mutex_unlock(&sink->lock);
	log_http_pending_drop(sink);
	log_http_tls_cleanup(sink);

	log_http_close_handle(sink, (uv_handle_t *)&sink->tcp, log_http_closed_cb);
}

static context_arg *log_http_tls_carg_ensure(log_http_sink *sink)
{
	context_arg *carg;

	if (!sink)
		return NULL;

	if (sink->tls_carg)
		return sink->tls_carg;

	carg = calloc(1, sizeof(*carg));
	if (!carg)
		return NULL;

	carg->key = strdup(sink->host ? sink->host : "log-http");
	if (sink->host)
		snprintf(carg->host, sizeof(carg->host), "%s", sink->host);
	carg->log_level = L_INFO;
	carg->data = sink;
	carg->tls = 1;
	sink->tls_carg = carg;
	return carg;
}

static int log_http_tls_verify_effective(const log_http_sink *sink)
{
	if (!sink)
		return 0;
	if (sink->tls_verify_defined)
		return sink->tls_verify ? 1 : 0;
	/* https:// defaults to peer verify (OpenSSL default CA paths if no tls_ca). */
	if (sink->use_tls)
		return 1;
	return sink->tls_ca_file ? 1 : 0;
}

static int log_http_tls_drive(log_http_sink *sink)
{
	context_arg *carg;
	int hs_ret;
	int err;

	if (!sink || !sink->tls_carg || !sink->tls_carg->ssl)
		return -1;

	carg = sink->tls_carg;
	if (carg->tls_handshake_done)
		return 1;

	hs_ret = SSL_do_handshake(carg->ssl);
	if (hs_ret == 1) {
		carg->tls_handshake_done = 1;
		uv_timer_stop(&sink->reconnect_timer);
		flush_tls_write(carg, (uv_stream_t *)&sink->tcp);
		pthread_mutex_lock(&sink->lock);
		sink->state = LOG_HTTP_CONNECTED;
		pthread_mutex_unlock(&sink->lock);
		glog(L_DEBUG, "log http: TLS handshake complete host=%s port=%d\n",
		    sink->host ? sink->host : "?", sink->port);
		log_http_schedule_drain(sink);
		log_http_try_write(sink);
		return 1;
	}

	err = SSL_get_error(carg->ssl, hs_ret);
	flush_tls_write(carg, (uv_stream_t *)&sink->tcp);
	if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
		return 0;

	{
		char *errmsg = openssl_get_error_string();
		glog(L_ERROR, "log http: TLS handshake failed host=%s port=%d: %s\n",
		    sink->host ? sink->host : "?", sink->port, errmsg ? errmsg : "?");
		free(errmsg);
	}
	return -1;
}

static int log_http_tls_start(log_http_sink *sink)
{
	context_arg *carg;
	int verify;

	if (!sink)
		return 0;

	carg = log_http_tls_carg_ensure(sink);
	if (!carg)
		return 0;

	if (sink->host)
		snprintf(carg->host, sizeof(carg->host), "%s", sink->host);
	carg->data = sink;
	carg->tls = 1;
	if (carg->tls_ca_file) {
		free(carg->tls_ca_file);
		carg->tls_ca_file = NULL;
	}
	if (carg->tls_cert_file) {
		free(carg->tls_cert_file);
		carg->tls_cert_file = NULL;
	}
	if (carg->tls_key_file) {
		free(carg->tls_key_file);
		carg->tls_key_file = NULL;
	}
	if (sink->tls_ca_file)
		carg->tls_ca_file = strdup(sink->tls_ca_file);
	if (sink->tls_cert_file)
		carg->tls_cert_file = strdup(sink->tls_cert_file);
	if (sink->tls_key_file)
		carg->tls_key_file = strdup(sink->tls_key_file);

	verify = log_http_tls_verify_effective(sink);
	carg->tls_verify = verify;
	carg->tls_verify_defined = 1;

	if (!tls_context_init(carg, SSLMODE_CLIENT, verify, sink->tls_ca_file,
	    sink->tls_cert_file, sink->tls_key_file, sink->host, NULL))
		return 0;

	uv_read_start((uv_stream_t *)&sink->tcp, log_http_tls_alloc_cb, log_http_tls_read_cb);
	log_http_arm_handshake_timeout(sink);
	if (log_http_tls_drive(sink) < 0)
		return 0;
	return 1;
}

static void log_http_tls_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	log_http_sink *sink = handle->data;
	context_arg *carg = sink ? sink->tls_carg : NULL;

	(void)suggested;

	if (carg) {
		buf->base = carg->ssl_read_buffer;
		buf->len = sizeof(carg->ssl_read_buffer);
		return;
	}

	/* No TLS context: stop the read rather than leaking a malloc'd buf. */
	buf->base = NULL;
	buf->len = 0;
}

static void log_http_tls_written_cb(uv_write_t *req, int status)
{
	context_arg *carg = req->data;
	log_http_sink *sink = carg ? carg->data : NULL;

	if (carg) {
		free(carg->write_buffer.base);
		carg->write_buffer.base = NULL;
		carg->write_buffer.len = 0;
	}

	if (!sink || sink->closing)
		return;

	if (status != 0) {
		sink->write_in_flight = 0;
		/* Avoid double reconnect if disconnect/close already started. */
		if (sink->state != LOG_HTTP_IDLE &&
		    !uv_is_closing((uv_handle_t *)&sink->tcp))
			log_http_mark_disconnected(sink);
		return;
	}

	sink->response_pending = 1;
}

static void log_http_tls_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
	log_http_sink *sink = stream->data;
	context_arg *carg;
	int driven;

	(void)buf;

	if (!sink || sink->closing)
		return;

	carg = sink->tls_carg;
	if (nread <= 0) {
		if (nread < 0)
			log_http_mark_disconnected(sink);
		return;
	}

	if (!carg || !carg->ssl || !carg->rbio) {
		log_http_mark_disconnected(sink);
		return;
	}

	if (BIO_write(carg->rbio, carg->ssl_read_buffer, (int)nread) <= 0) {
		glog(L_ERROR, "log http: TLS BIO_write failed host=%s port=%d\n",
		    sink->host ? sink->host : "?", sink->port);
		log_http_mark_disconnected(sink);
		return;
	}

	if (!carg->tls_handshake_done) {
		driven = log_http_tls_drive(sink);
		if (driven < 0)
			log_http_mark_disconnected(sink);
		return;
	}

	/* Drain decrypted HTTP response; we only need to know bytes arrived. */
	{
		char discard[EVENT_BUFFER];
		int got_app = 0;

		while (1) {
			int r = SSL_read(carg->ssl, discard, (int)sizeof(discard));
			if (r > 0) {
				got_app = 1;
				continue;
			}

			{
				int err = SSL_get_error(carg->ssl, r);
				int need = tls_io_check_shutdown_need(carg, err, r);

				if (need != 0) {
					log_http_mark_disconnected(sink);
					return;
				}
				if (err == SSL_ERROR_WANT_WRITE)
					flush_tls_write(carg, (uv_stream_t *)&sink->tcp);
			}
			break;
		}

		if (got_app && sink->response_pending) {
			sink->response_pending = 0;
			sink->write_in_flight = 0;
			log_http_try_write(sink);
		}
	}
}

static void log_http_mark_disconnected(log_http_sink *sink)
{
	if (!sink || sink->closing)
		return;

	pthread_mutex_lock(&sink->lock);
	sink->state = LOG_HTTP_IDLE;
	sink->write_in_flight = 0;
	sink->response_pending = 0;
	pthread_mutex_unlock(&sink->lock);
	log_http_pending_drop(sink);
	log_http_tls_cleanup(sink);

	if (sink->tcp_initialized &&
	    !uv_is_closing((uv_handle_t *)&sink->tcp))
	{
		uv_read_stop((uv_stream_t *)&sink->tcp);
		log_http_close_handle(sink, (uv_handle_t *)&sink->tcp, log_http_closed_cb);
	}
	/* If TCP is already closing, closed_cb schedules reconnect (or shutdown). */
}

static void log_http_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	(void)handle;
	buf->base = malloc(suggested ? suggested : 256);
	buf->len = buf->base ? (unsigned int)(suggested ? suggested : 256) : 0;
}

static void log_http_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
	log_http_sink *sink = stream->data;

	if (buf && buf->base)
		free(buf->base);

	if (nread > 0)
	{
		if (sink && sink->response_pending)
		{
			sink->response_pending = 0;
			sink->write_in_flight = 0;
			log_http_try_write(sink);
		}
		return;
	}

	if (nread == 0)
		return;

	log_http_mark_disconnected(sink);
}

static void log_http_written_cb(uv_write_t *req, int status)
{
	log_http_wr *wr = req->data;
	log_http_sink *sink = wr->sink;

	free(wr->data);
	free(wr);

	if (!sink || sink->closing)
		return;

	if (status != 0)
	{
		sink->write_in_flight = 0;
		if (sink->state != LOG_HTTP_IDLE &&
		    !uv_is_closing((uv_handle_t *)&sink->tcp))
			log_http_mark_disconnected(sink);
		return;
	}

	sink->response_pending = 1;
}

static void log_http_try_write(log_http_sink *sink)
{
	char *body = NULL;
	char *request = NULL;
	size_t bodylen = 0;
	size_t reqlen = 0;
	log_http_wr *wr;
	uv_buf_t buf;
	int ret;

	if (!sink || sink->closing || !log_http_is_connected(sink) ||
	    sink->write_in_flight)
		return;

	pthread_mutex_lock(&sink->lock);
	if (sink->pending_request)
	{
		request = sink->pending_request;
		reqlen = sink->pending_request_len;
		sink->pending_request = NULL;
		sink->pending_request_len = 0;
		sink->pending_body = NULL;
		sink->pending_body_len = 0;
	}
	else if (sink->pending_body)
	{
		body = sink->pending_body;
		bodylen = sink->pending_body_len;
		sink->pending_body = NULL;
		sink->pending_body_len = 0;
	}
	pthread_mutex_unlock(&sink->lock);

	if (!request && body)
	{
		request = log_http_build_request(sink, body, bodylen, &reqlen);
		free(body);
	}

	if (!request || !reqlen)
	{
		free(request);
		return;
	}

	if (sink->use_tls)
	{
		context_arg *carg = sink->tls_carg;

		if (!carg || !carg->ssl || !carg->tls_handshake_done)
		{
			free(request);
			return;
		}

		sink->write_in_flight = 1;
		ret = tls_write(carg, (uv_stream_t *)&sink->tcp, request, reqlen,
		    log_http_tls_written_cb);
		free(request);
		if (ret != 0)
		{
			sink->write_in_flight = 0;
			log_http_mark_disconnected(sink);
		}
		return;
	}

	wr = calloc(1, sizeof(*wr));
	if (!wr)
	{
		free(request);
		return;
	}

	wr->data = request;
	wr->len = reqlen;
	wr->sink = sink;
	wr->req.data = wr;

	buf = uv_buf_init(request, (unsigned int)reqlen);
	sink->write_in_flight = 1;
	ret = uv_write(&wr->req, (uv_stream_t *)&sink->tcp, &buf, 1, log_http_written_cb);
	if (ret != 0)
	{
		sink->write_in_flight = 0;
		free(request);
		free(wr);
		log_http_mark_disconnected(sink);
	}
}

static void log_http_async_cb(uv_async_t *handle)
{
	log_http_try_write(handle->data);
}

static void log_http_reconnect_cb(uv_timer_t *timer)
{
	log_http_start_connect(timer->data);
}

static void log_http_connected_cb(uv_connect_t *req, int status)
{
	log_http_sink *sink = req->data;

	if (!sink || sink->closing)
		return;

	if (status != 0)
	{
		log_http_connect_failed(sink);
		return;
	}

	if (sink->use_tls)
	{
		/* Stay CONNECTING until handshake completes in log_http_tls_drive. */
		if (!log_http_tls_start(sink))
			log_http_connect_failed(sink);
		return;
	}

	pthread_mutex_lock(&sink->lock);
	sink->state = LOG_HTTP_CONNECTED;
	pthread_mutex_unlock(&sink->lock);
	uv_read_start((uv_stream_t *)&sink->tcp, log_http_alloc_cb, log_http_read_cb);
	log_http_schedule_drain(sink);
	log_http_try_write(sink);
}

static void log_http_getaddrinfo_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	log_http_sink *sink = req->data;
	struct addrinfo *ai;

	if (!sink)
	{
		if (res)
			uv_freeaddrinfo(res);
		return;
	}

	sink->pending_gai = 0;

	if (sink->closing)
	{
		if (res)
			uv_freeaddrinfo(res);
		log_http_shutdown_next(sink);
		return;
	}

	if (status != 0 || !res)
	{
		if (res)
			uv_freeaddrinfo(res);
		log_http_connect_failed(sink);
		return;
	}

	for (ai = res; ai; ai = ai->ai_next)
	{
		if (ai->ai_family == AF_INET)
			break;
	}

	if (!ai)
	{
		uv_freeaddrinfo(res);
		log_http_connect_failed(sink);
		return;
	}

	sink->connect_req.data = sink;
	if (uv_tcp_connect(&sink->connect_req, &sink->tcp, ai->ai_addr, log_http_connected_cb) != 0)
	{
		uv_freeaddrinfo(res);
		log_http_connect_failed(sink);
		return;
	}

	uv_freeaddrinfo(res);
}

static void log_http_start_connect(log_http_sink *sink)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	char portbuf[16];
	int gai;

	if (!sink || sink->closing || !ac || !ac->loop)
		return;

	if (sink->state == LOG_HTTP_CONNECTING || sink->state == LOG_HTTP_CONNECTED)
		return;

	/* tcp_initialized is cleared from log_http_closed_cb, so it tells a
	 * finished close (handle reusable) from an in-flight one. uv_is_closing()
	 * alone stays true forever after a close. */
	if (sink->tcp_initialized && uv_is_closing((uv_handle_t *)&sink->tcp))
	{
		log_http_schedule_reconnect(sink);
		return;
	}

	uv_timer_stop(&sink->reconnect_timer);

	/* uv_tcp_init() inserts the handle into loop->handle_queue; calling it on
	 * a handle that is already initialized corrupts the queue. The handle is
	 * initialized once by log_http_sink_uv_init() and re-initialized only
	 * after its close callback has run. */
	if (!sink->tcp_initialized)
	{
		uv_tcp_init(ac->loop, &sink->tcp);
		sink->tcp.data = sink;
		sink->tcp_initialized = 1;
	}
	sink->state = LOG_HTTP_CONNECTING;

	snprintf(portbuf, sizeof(portbuf), "%d", sink->port);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	gai = getaddrinfo(sink->host, portbuf, &hints, &res);
	if (gai == 0 && res)
	{
		struct addrinfo *ai;

		for (ai = res; ai; ai = ai->ai_next)
		{
			if (ai->ai_family == AF_INET)
				break;
		}

		if (ai)
		{
			sink->connect_req.data = sink;
			if (uv_tcp_connect(&sink->connect_req, &sink->tcp, ai->ai_addr,
			    log_http_connected_cb) == 0)
			{
				freeaddrinfo(res);
				return;
			}
			freeaddrinfo(res);
			log_http_connect_failed(sink);
			return;
		}
		freeaddrinfo(res);
	}

	sink->getaddrinfo_req.data = sink;
	sink->pending_gai = 1;
	memset(&log_http_gai_hints, 0, sizeof(log_http_gai_hints));
	log_http_gai_hints.ai_family = AF_INET;
	log_http_gai_hints.ai_socktype = SOCK_STREAM;
	if (uv_getaddrinfo(ac->loop, &sink->getaddrinfo_req, log_http_getaddrinfo_cb,
	    sink->host, portbuf, &log_http_gai_hints) != 0)
	{
		/* On error libuv does not invoke the callback. */
		sink->pending_gai = 0;
		log_http_connect_failed(sink);
	}
}

static void log_http_shutdown_next(log_http_sink *sink)
{
	if (!sink)
		return;

	if (!sink->aux_closes_started)
	{
		sink->aux_closes_started = 1;
		log_http_close_handle(sink, (uv_handle_t *)&sink->reconnect_timer, log_http_aux_closed_cb);
		log_http_close_handle(sink, (uv_handle_t *)&sink->write_async, log_http_aux_closed_cb);
	}

	/* uv_is_closing() stays true after a handle is closed, so it cannot tell
	 * "still closing" from "already finished". pending_closes is the only
	 * reliable signal; tcp_initialized is cleared from log_http_closed_cb.
	 * pending_gai keeps the sink alive while an async getaddrinfo request
	 * still holds &sink->getaddrinfo_req (and req->data == sink). */
	if (sink->pending_closes || sink->tcp_initialized || sink->pending_gai)
		return;

	log_http_sink_destroy(sink);
}

static void log_http_aux_closed_cb(uv_handle_t *handle)
{
	log_http_sink *sink = handle->data;

	if (!sink)
		return;

	if (sink->pending_closes)
		sink->pending_closes--;

	log_http_shutdown_next(sink);
}

static void log_http_closed_cb(uv_handle_t *handle)
{
	log_http_sink *sink = handle->data;

	if (!sink)
		return;

	if (sink->pending_closes)
		sink->pending_closes--;
	sink->tcp_initialized = 0;

	sink->state = LOG_HTTP_IDLE;
	sink->write_in_flight = 0;
	sink->response_pending = 0;
	log_http_pending_drop(sink);

	if (sink->closing)
	{
		log_http_shutdown_next(sink);
		return;
	}

	log_http_schedule_reconnect(sink);
}

static void log_http_sink_destroy(log_http_sink *sink)
{
	if (!sink)
		return;

	log_http_pending_drop(sink);
	log_http_tls_cleanup(sink);
	if (sink->tls_carg) {
		free(sink->tls_carg->write_buffer.base);
		sink->tls_carg->write_buffer.base = NULL;
		sink->tls_carg->write_buffer.len = 0;
		free(sink->tls_carg->key);
		free(sink->tls_carg->tls_ca_file);
		free(sink->tls_carg->tls_cert_file);
		free(sink->tls_carg->tls_key_file);
		free(sink->tls_carg);
		sink->tls_carg = NULL;
	}
	pthread_mutex_destroy(&sink->lock);
	free(sink->host);
	free(sink->path);
	free(sink->auth_basic_b64);
	free(sink->tls_cert_file);
	free(sink->tls_key_file);
	free(sink->tls_ca_file);
	free(sink);
}

static void log_http_sink_uv_init(log_http_sink *sink)
{
	if (!sink || sink->uv_ready || !ac || !ac->loop)
		return;

	uv_tcp_init(ac->loop, &sink->tcp);
	sink->tcp.data = sink;
	sink->tcp_initialized = 1;

	uv_timer_init(ac->loop, &sink->reconnect_timer);
	sink->reconnect_timer.data = sink;

	uv_async_init(ac->loop, &sink->write_async, log_http_async_cb);
	sink->write_async.data = sink;

	sink->uv_ready = 1;
}

static log_http_sink *log_http_sink_create(void)
{
	log_http_sink *sink = calloc(1, sizeof(*sink));

	if (!sink)
		return NULL;

	pthread_mutex_init(&sink->lock, NULL);
	log_http_sink_uv_init(sink);
	return sink;
}

void log_http_sink_open(log_channel *ch, const char *host, int port, const char *path,
    const char *auth_basic_b64, int use_tls)
{
	log_http_sink *sink;
	char *auth_header = NULL;

	if (!ch || !host || !host[0] || port <= 0)
		return;

	sink = ch->http_sink;
	if (!sink)
	{
		sink = log_http_sink_create();
		if (!sink)
			return;
		ch->http_sink = sink;
	}

	log_http_sink_uv_init(sink);

	if (sink->host)
		free(sink->host);
	if (sink->path)
		free(sink->path);
	if (sink->auth_basic_b64)
		free(sink->auth_basic_b64);
	if (sink->tls_cert_file)
		free(sink->tls_cert_file);
	if (sink->tls_key_file)
		free(sink->tls_key_file);
	if (sink->tls_ca_file)
		free(sink->tls_ca_file);

	if (auth_basic_b64 && auth_basic_b64[0])
	{
		int n = snprintf(NULL, 0, "Authorization: Basic %s\r\n", auth_basic_b64);
		if (n > 0)
		{
			auth_header = calloc(1, (size_t)n + 1);
			if (auth_header)
				snprintf(auth_header, (size_t)n + 1, "Authorization: Basic %s\r\n", auth_basic_b64);
		}
	}
	sink->host = strdup(host);
	sink->path = path && path[0] ? strdup(path) : strdup("/");
	sink->auth_basic_b64 = auth_header;
	sink->port = port;
	sink->use_tls = use_tls ? 1 : 0;
	sink->tls_cert_file = ch->tls_cert_file && ch->tls_cert_file[0] ? strdup(ch->tls_cert_file) : NULL;
	sink->tls_key_file = ch->tls_key_file && ch->tls_key_file[0] ? strdup(ch->tls_key_file) : NULL;
	sink->tls_ca_file = ch->tls_ca_file && ch->tls_ca_file[0] ? strdup(ch->tls_ca_file) : NULL;
	sink->tls_verify = ch->tls_verify;
	sink->tls_verify_defined = ch->tls_verify_defined;
	sink->closing = 0;
	if (sink->tls_carg) {
		free(sink->tls_carg->key);
		sink->tls_carg->key = strdup(sink->host ? sink->host : "log-http");
		if (sink->host)
			snprintf(sink->tls_carg->host, sizeof(sink->tls_carg->host), "%s", sink->host);
		sink->tls_carg->data = sink;
	}

	ch->dest_kind = LOG_DEST_HTTP;
	ch->socket = -1;

	if (!ac || !ac->loop)
		return;

	/*
	 * Active connection or TCP close still in flight: update settings above,
	 * then let closed_cb reconnect with the new material. Never uv_tcp_init
	 * on a handle that is still closing.
	 */
	if (sink->state != LOG_HTTP_IDLE ||
	    (sink->tcp_initialized && uv_is_closing((uv_handle_t *)&sink->tcp)))
	{
		log_http_pending_drop(sink);
		log_http_tls_cleanup(sink);
		if (sink->state != LOG_HTTP_IDLE &&
		    !(sink->tcp_initialized && uv_is_closing((uv_handle_t *)&sink->tcp)))
		{
			uv_read_stop((uv_stream_t *)&sink->tcp);
			log_http_close_handle(sink, (uv_handle_t *)&sink->tcp, log_http_closed_cb);
		}
		return;
	}

	log_http_start_connect(sink);
}

void log_http_sink_close(log_channel *ch)
{
	log_http_sink *sink;

	if (!ch || !ch->http_sink)
		return;

	sink = ch->http_sink;
	ch->http_sink = NULL;
	ch->dest_kind = LOG_DEST_FD;
	ch->socket = fileno(stdout);

	if (sink->closing)
		return;

	sink->closing = 1;

	uv_timer_stop(&sink->reconnect_timer);
	log_http_pending_drop(sink);
	log_http_tls_cleanup(sink);

	if (!sink->uv_ready)
	{
		log_http_sink_destroy(sink);
		return;
	}

	/*
	 * Disconnect may have already set state=IDLE and started uv_close(tcp).
	 * Wait for closed_cb → shutdown_next instead of destroying via the aux
	 * path while tcp/write callbacks are still pending.
	 */
	if (sink->tcp_initialized && uv_is_closing((uv_handle_t *)&sink->tcp))
		return;

	if (sink->tcp_initialized)
	{
		if (sink->state != LOG_HTTP_IDLE)
			uv_read_stop((uv_stream_t *)&sink->tcp);
		log_http_close_handle(sink, (uv_handle_t *)&sink->tcp, log_http_closed_cb);
		return;
	}

	/* TCP already finished (uv_is_closing stays true after close). */
	log_http_shutdown_next(sink);
}

int log_http_sink_write(log_channel *ch, const char *data, size_t len, const char *kind)
{
	log_http_sink *sink;
	char *copy;

	if (!ch || !data || !len)
		return -1;
	if (!kind || !kind[0])
		kind = "unknown";

	sink = ch->http_sink;
	if (!sink || sink->closing) {
		log_channel_account(ch, kind, "dropped", "disconnected");
		return -1;
	}

	if (!log_http_is_connected(sink)) {
		log_channel_account(ch, kind, "dropped", "disconnected");
		return -1;
	}

	copy = malloc(len);
	if (!copy) {
		log_channel_account(ch, kind, "error", "oom");
		return -1;
	}
	memcpy(copy, data, len);

	pthread_mutex_lock(&sink->lock);
	if (sink->write_in_flight || sink->pending_body || sink->pending_request)
	{
		pthread_mutex_unlock(&sink->lock);
		free(copy);
		log_channel_account(ch, kind, "dropped", "busy");
		return -1;
	}

	sink->pending_body = copy;
	sink->pending_body_len = len;
	pthread_mutex_unlock(&sink->lock);

	log_http_schedule_drain(sink);
	log_channel_account(ch, kind, "sent", NULL);
	return 0;
}

int log_channel_is_http(const log_channel *ch)
{
	return ch && ch->dest_kind == LOG_DEST_HTTP;
}
