#include "common/log_http.h"
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
	int port;
	int state;
	uint8_t write_in_flight;
	uint8_t response_pending;
	uint8_t closing;
	uint8_t uv_ready;
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

	if (!sink || !body || !bodylen || !outlen)
		return NULL;

	n = snprintf(NULL, 0,
	    "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/x-ndjson\r\nContent-Length: %zu\r\nConnection: keep-alive\r\n\r\n",
	    sink->path ? sink->path : "/", sink->host ? sink->host : "localhost",
	    sink->port, bodylen);
	if (n < 0)
		return NULL;

	hdrlen = (size_t)n;
	req = malloc(hdrlen + bodylen + 1);
	if (!req)
		return NULL;

	snprintf(req, hdrlen + 1,
	    "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/x-ndjson\r\nContent-Length: %zu\r\nConnection: keep-alive\r\n\r\n",
	    sink->path ? sink->path : "/", sink->host ? sink->host : "localhost",
	    sink->port, bodylen);
	memcpy(req + hdrlen, body, bodylen);
	req[hdrlen + bodylen] = '\0';
	*outlen = hdrlen + bodylen;
	return req;
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

	if (!uv_is_closing((uv_handle_t *)&sink->tcp))
	{
		uv_read_stop((uv_stream_t *)&sink->tcp);
		uv_close((uv_handle_t *)&sink->tcp, log_http_closed_cb);
	}
	else
		log_http_schedule_reconnect(sink);
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
		sink->state = LOG_HTTP_IDLE;
		uv_close((uv_handle_t *)&sink->tcp, log_http_closed_cb);
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

	if (!sink || sink->closing)
	{
		if (res)
			uv_freeaddrinfo(res);
		return;
	}

	if (status != 0 || !res)
	{
		sink->state = LOG_HTTP_IDLE;
		log_http_schedule_reconnect(sink);
		if (res)
			uv_freeaddrinfo(res);
		return;
	}

	for (ai = res; ai; ai = ai->ai_next)
	{
		if (ai->ai_family == AF_INET)
			break;
	}

	if (!ai)
	{
		sink->state = LOG_HTTP_IDLE;
		uv_freeaddrinfo(res);
		log_http_schedule_reconnect(sink);
		return;
	}

	sink->connect_req.data = sink;
	if (uv_tcp_connect(&sink->connect_req, &sink->tcp, ai->ai_addr, log_http_connected_cb) != 0)
	{
		sink->state = LOG_HTTP_IDLE;
		uv_freeaddrinfo(res);
		log_http_schedule_reconnect(sink);
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

	uv_timer_stop(&sink->reconnect_timer);

	uv_tcp_init(ac->loop, &sink->tcp);
	sink->tcp.data = sink;
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
		}
		freeaddrinfo(res);
	}

	sink->getaddrinfo_req.data = sink;
	memset(&log_http_gai_hints, 0, sizeof(log_http_gai_hints));
	log_http_gai_hints.ai_family = AF_INET;
	log_http_gai_hints.ai_socktype = SOCK_STREAM;
	if (uv_getaddrinfo(ac->loop, &sink->getaddrinfo_req, log_http_getaddrinfo_cb,
	    sink->host, portbuf, &log_http_gai_hints) != 0)
	{
		sink->state = LOG_HTTP_IDLE;
		log_http_schedule_reconnect(sink);
	}
}

static void log_http_shutdown_next(log_http_sink *sink)
{
	if (!sink)
		return;

	if (!uv_is_closing((uv_handle_t *)&sink->reconnect_timer))
	{
		uv_close((uv_handle_t *)&sink->reconnect_timer, log_http_aux_closed_cb);
		return;
	}
	if (!uv_is_closing((uv_handle_t *)&sink->write_async))
	{
		uv_close((uv_handle_t *)&sink->write_async, log_http_aux_closed_cb);
		return;
	}
	log_http_sink_destroy(sink);
}

static void log_http_aux_closed_cb(uv_handle_t *handle)
{
	log_http_shutdown_next(handle->data);
}

static void log_http_closed_cb(uv_handle_t *handle)
{
	log_http_sink *sink = handle->data;

	if (!sink)
		return;

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
	pthread_mutex_destroy(&sink->lock);
	free(sink->host);
	free(sink->path);
	free(sink);
}

static void log_http_sink_uv_init(log_http_sink *sink)
{
	if (!sink || sink->uv_ready || !ac || !ac->loop)
		return;

	uv_tcp_init(ac->loop, &sink->tcp);
	sink->tcp.data = sink;

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

void log_http_sink_open(log_channel *ch, const char *host, int port, const char *path)
{
	log_http_sink *sink;

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
	sink->host = strdup(host);
	sink->path = path && path[0] ? strdup(path) : strdup("/");
	sink->port = port;
	sink->closing = 0;

	ch->dest_kind = LOG_DEST_HTTP;
	ch->socket = -1;

	if (!ac || !ac->loop)
		return;

	if (sink->state != LOG_HTTP_IDLE)
	{
		log_http_pending_drop(sink);
		if (!uv_is_closing((uv_handle_t *)&sink->tcp))
		{
			uv_read_stop((uv_stream_t *)&sink->tcp);
			uv_close((uv_handle_t *)&sink->tcp, log_http_closed_cb);
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

	if (!sink->uv_ready)
	{
		log_http_sink_destroy(sink);
		return;
	}

	if (sink->state != LOG_HTTP_IDLE && !uv_is_closing((uv_handle_t *)&sink->tcp))
	{
		uv_read_stop((uv_stream_t *)&sink->tcp);
		uv_close((uv_handle_t *)&sink->tcp, log_http_closed_cb);
	}
	else if (!uv_is_closing((uv_handle_t *)&sink->reconnect_timer))
		uv_close((uv_handle_t *)&sink->reconnect_timer, log_http_aux_closed_cb);
	else if (!uv_is_closing((uv_handle_t *)&sink->write_async))
		uv_close((uv_handle_t *)&sink->write_async, log_http_aux_closed_cb);
	else
		log_http_sink_destroy(sink);
}

int log_http_sink_write(log_channel *ch, const char *data, size_t len)
{
	log_http_sink *sink;
	char *copy;

	if (!ch || !data || !len)
		return -1;

	sink = ch->http_sink;
	if (!sink || sink->closing)
		return -1;

	if (!log_http_is_connected(sink))
		return -1;

	copy = malloc(len);
	if (!copy)
		return -1;
	memcpy(copy, data, len);

	pthread_mutex_lock(&sink->lock);
	if (sink->write_in_flight || sink->pending_body || sink->pending_request)
	{
		pthread_mutex_unlock(&sink->lock);
		free(copy);
		return -1;
	}

	sink->pending_body = copy;
	sink->pending_body_len = len;
	pthread_mutex_unlock(&sink->lock);

	log_http_schedule_drain(sink);
	return 0;
}

int log_channel_is_http(const log_channel *ch)
{
	return ch && ch->dest_kind == LOG_DEST_HTTP;
}
