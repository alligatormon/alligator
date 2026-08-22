#include "uva.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct uva_tcp {
	uv_loop_t *loop;
	uv_tcp_t handle;
	uv_connect_t connect_req;
	uv_write_t write_req;
	uv_getaddrinfo_t resolver;
	uva_future_t fut;
	uint64_t op_gen;
	uint64_t deadline_ms;
	char *read_dst;
	size_t read_cap;
	ssize_t read_n;
	int initialized;
	int connected;
	int closing;
	int reading;
};

static int tcp_await(uva_tcp_t *c)
{
	int r;

	if (!c->deadline_ms)
		return uva_await(&c->fut);

	{
		uint64_t now = uv_now(c->loop);
		if (now >= c->deadline_ms) {
			uva_future_cancel(&c->fut, UV_ETIMEDOUT);
			return UV_ETIMEDOUT;
		}
		r = uva_await_timeout(&c->fut, c->deadline_ms - now);
	}

	if (r == UV_ETIMEDOUT && c->reading) {
		uv_read_stop((uv_stream_t *)&c->handle);
		c->reading = 0;
	}
	return r;
}

static void on_tcp_closed(uv_handle_t *h)
{
	uva_tcp_t *c = h->data;
	uva_future_complete_gen(&c->fut, c->op_gen, 0);
}

static void alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	uva_tcp_t *c = handle->data;
	(void)suggested;
	buf->base = c->read_dst;
	buf->len = c->read_cap;
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
	uva_tcp_t *c = stream->data;
	(void)buf;

	c->reading = 0;
	uv_read_stop(stream);

	if (c->fut.gen != c->op_gen)
		return;

	if (nread == UV_EOF) {
		c->read_n = 0;
		uva_future_complete_gen(&c->fut, c->op_gen, 0);
		return;
	}
	if (nread < 0) {
		c->read_n = nread;
		uva_future_complete_gen(&c->fut, c->op_gen, (int)nread);
		return;
	}

	c->read_n = nread;
	uva_future_complete_gen(&c->fut, c->op_gen, 0);
}

static void on_write(uv_write_t *req, int status)
{
	uva_tcp_t *c = req->data;
	uva_future_complete_gen(&c->fut, c->op_gen, status);
}

static void on_connect(uv_connect_t *req, int status)
{
	uva_tcp_t *c = req->data;
	if (c->fut.gen != c->op_gen)
		return;
	if (status == 0)
		c->connected = 1;
	uva_future_complete_gen(&c->fut, c->op_gen, status);
}

static void on_resolved(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	uva_tcp_t *c = req->data;

	if (c->fut.gen != c->op_gen) {
		if (res)
			uv_freeaddrinfo(res);
		return;
	}

	if (status < 0) {
		if (res)
			uv_freeaddrinfo(res);
		uva_future_complete_gen(&c->fut, c->op_gen, status);
		return;
	}

	c->connect_req.data = c;
	status = uv_tcp_connect(&c->connect_req, &c->handle, res->ai_addr, on_connect);
	uv_freeaddrinfo(res);
	if (status < 0)
		uva_future_complete_gen(&c->fut, c->op_gen, status);
	/* else: wait for on_connect */
}

uva_tcp_t *uva_tcp_new(uv_loop_t *loop)
{
	uva_tcp_t *c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;

	c->loop = loop;
	uva_future_init(&c->fut, loop);

	if (uv_tcp_init(loop, &c->handle) < 0) {
		free(c);
		return NULL;
	}
	c->handle.data = c;
	c->initialized = 1;
	return c;
}

void uva_tcp_set_deadline(uva_tcp_t *c, uint64_t deadline_ms)
{
	if (c)
		c->deadline_ms = deadline_ms;
}

int uva_tcp_connect_addr(uva_tcp_t *c, const struct sockaddr *addr)
{
	int r;

	if (!c || !addr)
		return UV_EINVAL;

	uva_future_reset(&c->fut);
	c->op_gen = c->fut.gen;
	c->connect_req.data = c;
	r = uv_tcp_connect(&c->connect_req, &c->handle, addr, on_connect);
	if (r < 0)
		return r;
	return tcp_await(c);
}

int uva_tcp_connect(uva_tcp_t *c, const char *host, int port)
{
	struct addrinfo hints;
	char portstr[16];
	int r;

	if (!c || !host || port <= 0)
		return UV_EINVAL;

	uva_future_reset(&c->fut);
	c->op_gen = c->fut.gen;
	snprintf(portstr, sizeof(portstr), "%d", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	c->resolver.data = c;
	r = uv_getaddrinfo(c->loop, &c->resolver, on_resolved, host, portstr, &hints);
	if (r < 0)
		return r;

	return tcp_await(c);
}

ssize_t uva_tcp_write(uva_tcp_t *c, const void *buf, size_t len)
{
	uv_buf_t b;
	int r;

	if (!c || !c->connected || !buf)
		return UV_EINVAL;

	uva_future_reset(&c->fut);
	c->op_gen = c->fut.gen;
	b = uv_buf_init((char *)buf, (unsigned int)len);
	c->write_req.data = c;
	r = uv_write(&c->write_req, (uv_stream_t *)&c->handle, &b, 1, on_write);
	if (r < 0)
		return r;

	r = tcp_await(c);
	if (r < 0)
		return r;
	return (ssize_t)len;
}

ssize_t uva_tcp_read(uva_tcp_t *c, void *buf, size_t len)
{
	int r;

	if (!c || !c->connected || !buf || len == 0)
		return UV_EINVAL;

	uva_future_reset(&c->fut);
	c->op_gen = c->fut.gen;
	c->read_dst = buf;
	c->read_cap = len;
	c->read_n = 0;

	r = uv_read_start((uv_stream_t *)&c->handle, alloc_cb, on_read);
	if (r < 0)
		return r;
	c->reading = 1;

	r = tcp_await(c);
	c->reading = 0;
	if (r < 0)
		return r;
	return c->read_n;
}

int uva_tcp_close(uva_tcp_t *c)
{
	if (!c || !c->initialized)
		return 0;
	if (c->closing) {
		if (!c->fut.done)
			return tcp_await(c);
		return 0;
	}

	if (c->reading) {
		uv_read_stop((uv_stream_t *)&c->handle);
		c->reading = 0;
	}

	c->closing = 1;
	c->connected = 0;
	uva_future_reset(&c->fut);
	c->op_gen = c->fut.gen;
	uv_close((uv_handle_t *)&c->handle, on_tcp_closed);
	return tcp_await(c);
}

void uva_tcp_free(uva_tcp_t *c)
{
	if (!c)
		return;
	if (c->initialized && !c->closing)
		uva_tcp_close(c);
	else if (c->initialized && c->closing && !c->fut.done)
		tcp_await(c);
	free(c);
}
