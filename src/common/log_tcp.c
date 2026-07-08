#include "common/log_tcp.h"
#include "common/selector.h"
#include "main.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>

enum {
	LOG_TCP_IDLE = 0,
	LOG_TCP_CONNECTING,
	LOG_TCP_CONNECTED,
};

typedef struct log_tcp_wr {
	uv_write_t req;
	char *data;
	size_t len;
	log_tcp_sink *sink;
} log_tcp_wr;

struct log_tcp_sink {
	uv_tcp_t tcp;
	uv_connect_t connect_req;
	uv_timer_t reconnect_timer;
	uv_async_t write_async;
	uv_getaddrinfo_t getaddrinfo_req;
	char *host;
	int port;
	int state;
	uint8_t write_in_flight;
	uint8_t closing;
	uint8_t uv_ready;
	char *pending_data;
	size_t pending_len;
	pthread_mutex_t lock;
};

extern aconf *ac;

static struct addrinfo log_tcp_gai_hints;

static void log_tcp_closed_cb(uv_handle_t *handle);
static void log_tcp_aux_closed_cb(uv_handle_t *handle);
static void log_tcp_reconnect_cb(uv_timer_t *timer);
static void log_tcp_start_connect(log_tcp_sink *sink);
static void log_tcp_mark_disconnected(log_tcp_sink *sink);
static void log_tcp_try_write(log_tcp_sink *sink);
static void log_tcp_sink_destroy(log_tcp_sink *sink);
static int log_tcp_is_connected(log_tcp_sink *sink);
static void log_tcp_schedule_drain(log_tcp_sink *sink);

static void log_tcp_pending_drop(log_tcp_sink *sink)
{
	char *data;

	if (!sink)
		return;

	pthread_mutex_lock(&sink->lock);
	data = sink->pending_data;
	sink->pending_data = NULL;
	sink->pending_len = 0;
	pthread_mutex_unlock(&sink->lock);

	free(data);
}

static void log_tcp_schedule_reconnect(log_tcp_sink *sink)
{
	if (!sink || sink->closing || !sink->uv_ready)
		return;

	uv_timer_start(&sink->reconnect_timer, log_tcp_reconnect_cb,
	    LOG_TCP_RECONNECT_MS, 0);
}

static void log_tcp_schedule_drain(log_tcp_sink *sink)
{
	if (!sink || sink->closing || !sink->uv_ready)
		return;

	if (log_tcp_is_connected(sink))
		uv_async_send(&sink->write_async);
}

static int log_tcp_is_connected(log_tcp_sink *sink)
{
	int connected;

	if (!sink)
		return 0;

	pthread_mutex_lock(&sink->lock);
	connected = sink->state == LOG_TCP_CONNECTED;
	pthread_mutex_unlock(&sink->lock);
	return connected;
}

static void log_tcp_mark_disconnected(log_tcp_sink *sink)
{
	if (!sink || sink->closing)
		return;

	pthread_mutex_lock(&sink->lock);
	sink->state = LOG_TCP_IDLE;
	sink->write_in_flight = 0;
	pthread_mutex_unlock(&sink->lock);
	log_tcp_pending_drop(sink);

	if (!uv_is_closing((uv_handle_t *)&sink->tcp))
	{
		uv_read_stop((uv_stream_t *)&sink->tcp);
		uv_close((uv_handle_t *)&sink->tcp, log_tcp_closed_cb);
	}
	else
		log_tcp_schedule_reconnect(sink);
}

static void log_tcp_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	(void)handle;
	buf->base = malloc(suggested ? suggested : 256);
	buf->len = buf->base ? (unsigned int)(suggested ? suggested : 256) : 0;
}

static void log_tcp_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
	log_tcp_sink *sink = stream->data;

	if (buf && buf->base)
		free(buf->base);

	if (nread > 0)
		return;

	if (nread == 0)
		return;

	log_tcp_mark_disconnected(sink);
}

static void log_tcp_written_cb(uv_write_t *req, int status)
{
	log_tcp_wr *wr = req->data;
	log_tcp_sink *sink = wr->sink;

	free(wr->data);
	free(wr);

	if (!sink || sink->closing)
		return;

	sink->write_in_flight = 0;

	if (status != 0)
	{
		log_tcp_mark_disconnected(sink);
		return;
	}

	log_tcp_try_write(sink);
}

static void log_tcp_try_write(log_tcp_sink *sink)
{
	char *data = NULL;
	size_t len = 0;
	log_tcp_wr *wr;
	uv_buf_t buf;
	int ret;

	if (!sink || sink->closing || !log_tcp_is_connected(sink) ||
	    sink->write_in_flight)
		return;

	pthread_mutex_lock(&sink->lock);
	if (sink->pending_data)
	{
		data = sink->pending_data;
		len = sink->pending_len;
		sink->pending_data = NULL;
		sink->pending_len = 0;
	}
	pthread_mutex_unlock(&sink->lock);

	if (!data || !len)
		return;

	wr = calloc(1, sizeof(*wr));
	if (!wr)
	{
		free(data);
		return;
	}

	wr->data = data;
	wr->len = len;
	wr->sink = sink;
	wr->req.data = wr;

	buf = uv_buf_init(data, (unsigned int)len);
	sink->write_in_flight = 1;
	ret = uv_write(&wr->req, (uv_stream_t *)&sink->tcp, &buf, 1, log_tcp_written_cb);
	if (ret != 0)
	{
		sink->write_in_flight = 0;
		free(data);
		free(wr);
		log_tcp_mark_disconnected(sink);
	}
}

static void log_tcp_async_cb(uv_async_t *handle)
{
	log_tcp_try_write(handle->data);
}

static void log_tcp_reconnect_cb(uv_timer_t *timer)
{
	log_tcp_start_connect(timer->data);
}

static void log_tcp_connected_cb(uv_connect_t *req, int status)
{
	log_tcp_sink *sink = req->data;

	if (!sink || sink->closing)
		return;

	if (status != 0)
	{
		sink->state = LOG_TCP_IDLE;
		uv_close((uv_handle_t *)&sink->tcp, log_tcp_closed_cb);
		return;
	}

	pthread_mutex_lock(&sink->lock);
	sink->state = LOG_TCP_CONNECTED;
	pthread_mutex_unlock(&sink->lock);
	uv_read_start((uv_stream_t *)&sink->tcp, log_tcp_alloc_cb, log_tcp_read_cb);
	log_tcp_schedule_drain(sink);
	log_tcp_try_write(sink);
}

static void log_tcp_getaddrinfo_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	log_tcp_sink *sink = req->data;
	struct addrinfo *ai;

	if (!sink || sink->closing)
	{
		if (res)
			uv_freeaddrinfo(res);
		return;
	}

	if (status != 0 || !res)
	{
		sink->state = LOG_TCP_IDLE;
		log_tcp_schedule_reconnect(sink);
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
		sink->state = LOG_TCP_IDLE;
		uv_freeaddrinfo(res);
		log_tcp_schedule_reconnect(sink);
		return;
	}

	sink->connect_req.data = sink;
	if (uv_tcp_connect(&sink->connect_req, &sink->tcp, ai->ai_addr, log_tcp_connected_cb) != 0)
	{
		sink->state = LOG_TCP_IDLE;
		uv_freeaddrinfo(res);
		log_tcp_schedule_reconnect(sink);
		return;
	}

	uv_freeaddrinfo(res);
}

static void log_tcp_start_connect(log_tcp_sink *sink)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	char portbuf[16];
	int gai;

	if (!sink || sink->closing || !ac || !ac->loop)
		return;

	if (sink->state == LOG_TCP_CONNECTING || sink->state == LOG_TCP_CONNECTED)
		return;

	uv_timer_stop(&sink->reconnect_timer);
	log_tcp_pending_drop(sink);

	uv_tcp_init(ac->loop, &sink->tcp);
	sink->tcp.data = sink;
	sink->state = LOG_TCP_CONNECTING;

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
			    log_tcp_connected_cb) == 0)
			{
				freeaddrinfo(res);
				return;
			}
		}
		freeaddrinfo(res);
	}

	sink->getaddrinfo_req.data = sink;
	memset(&log_tcp_gai_hints, 0, sizeof(log_tcp_gai_hints));
	log_tcp_gai_hints.ai_family = AF_INET;
	log_tcp_gai_hints.ai_socktype = SOCK_STREAM;
	if (uv_getaddrinfo(ac->loop, &sink->getaddrinfo_req, log_tcp_getaddrinfo_cb,
	    sink->host, portbuf, &log_tcp_gai_hints) != 0)
	{
		sink->state = LOG_TCP_IDLE;
		log_tcp_schedule_reconnect(sink);
	}
}

static void log_tcp_shutdown_next(log_tcp_sink *sink)
{
	if (!sink)
		return;

	if (!uv_is_closing((uv_handle_t *)&sink->reconnect_timer))
	{
		uv_close((uv_handle_t *)&sink->reconnect_timer, log_tcp_aux_closed_cb);
		return;
	}
	if (!uv_is_closing((uv_handle_t *)&sink->write_async))
	{
		uv_close((uv_handle_t *)&sink->write_async, log_tcp_aux_closed_cb);
		return;
	}
	log_tcp_sink_destroy(sink);
}

static void log_tcp_aux_closed_cb(uv_handle_t *handle)
{
	log_tcp_shutdown_next(handle->data);
}

static void log_tcp_closed_cb(uv_handle_t *handle)
{
	log_tcp_sink *sink = handle->data;

	if (!sink)
		return;

	sink->state = LOG_TCP_IDLE;
	sink->write_in_flight = 0;
	log_tcp_pending_drop(sink);

	if (sink->closing)
	{
		log_tcp_shutdown_next(sink);
		return;
	}

	log_tcp_schedule_reconnect(sink);
}

static void log_tcp_sink_destroy(log_tcp_sink *sink)
{
	if (!sink)
		return;

	log_tcp_pending_drop(sink);
	pthread_mutex_destroy(&sink->lock);
	free(sink->host);
	free(sink);
}

static void log_tcp_sink_uv_init(log_tcp_sink *sink)
{
	if (!sink || sink->uv_ready || !ac || !ac->loop)
		return;

	uv_tcp_init(ac->loop, &sink->tcp);
	sink->tcp.data = sink;

	uv_timer_init(ac->loop, &sink->reconnect_timer);
	sink->reconnect_timer.data = sink;

	uv_async_init(ac->loop, &sink->write_async, log_tcp_async_cb);
	sink->write_async.data = sink;

	sink->uv_ready = 1;
}

static log_tcp_sink *log_tcp_sink_create(void)
{
	log_tcp_sink *sink = calloc(1, sizeof(*sink));

	if (!sink)
		return NULL;

	pthread_mutex_init(&sink->lock, NULL);
	log_tcp_sink_uv_init(sink);
	return sink;
}

void log_tcp_sink_open(log_channel *ch, const char *host, int port)
{
	log_tcp_sink *sink;

	if (!ch || !host || !host[0] || port <= 0)
		return;

	sink = ch->tcp_sink;
	if (!sink)
	{
		sink = log_tcp_sink_create();
		if (!sink)
			return;
		ch->tcp_sink = sink;
	}

	log_tcp_sink_uv_init(sink);

	if (sink->host)
		free(sink->host);
	sink->host = strdup(host);
	sink->port = port;
	sink->closing = 0;

	ch->dest_kind = LOG_DEST_TCP;
	ch->socket = -1;

	if (!ac || !ac->loop)
		return;

	if (sink->state != LOG_TCP_IDLE)
	{
		log_tcp_pending_drop(sink);
		if (!uv_is_closing((uv_handle_t *)&sink->tcp))
		{
			uv_read_stop((uv_stream_t *)&sink->tcp);
			uv_close((uv_handle_t *)&sink->tcp, log_tcp_closed_cb);
		}
		return;
	}

	log_tcp_start_connect(sink);
}

void log_tcp_sink_close(log_channel *ch)
{
	log_tcp_sink *sink;

	if (!ch || !ch->tcp_sink)
		return;

	sink = ch->tcp_sink;
	ch->tcp_sink = NULL;
	ch->dest_kind = LOG_DEST_FD;
	ch->socket = fileno(stdout);

	if (sink->closing)
		return;

	sink->closing = 1;

	uv_timer_stop(&sink->reconnect_timer);
	log_tcp_pending_drop(sink);

	if (!sink->uv_ready)
	{
		log_tcp_sink_destroy(sink);
		return;
	}

	if (sink->state != LOG_TCP_IDLE && !uv_is_closing((uv_handle_t *)&sink->tcp))
	{
		uv_read_stop((uv_stream_t *)&sink->tcp);
		uv_close((uv_handle_t *)&sink->tcp, log_tcp_closed_cb);
	}
	else if (!uv_is_closing((uv_handle_t *)&sink->reconnect_timer))
		uv_close((uv_handle_t *)&sink->reconnect_timer, log_tcp_aux_closed_cb);
	else if (!uv_is_closing((uv_handle_t *)&sink->write_async))
		uv_close((uv_handle_t *)&sink->write_async, log_tcp_aux_closed_cb);
	else
		log_tcp_sink_destroy(sink);
}

int log_tcp_sink_write(log_channel *ch, const char *data, size_t len)
{
	log_tcp_sink *sink;
	char *copy;

	if (!ch || !data || !len)
		return -1;

	sink = ch->tcp_sink;
	if (!sink || sink->closing)
		return -1;

	if (!log_tcp_is_connected(sink))
		return -1;

	copy = malloc(len);
	if (!copy)
		return -1;
	memcpy(copy, data, len);

	pthread_mutex_lock(&sink->lock);
	if (sink->write_in_flight || sink->pending_data)
	{
		pthread_mutex_unlock(&sink->lock);
		free(copy);
		return -1;
	}

	sink->pending_data = copy;
	sink->pending_len = len;
	pthread_mutex_unlock(&sink->lock);

	log_tcp_schedule_drain(sink);
	return 0;
}

int log_channel_is_tcp(const log_channel *ch)
{
	return ch && ch->dest_kind == LOG_DEST_TCP;
}
