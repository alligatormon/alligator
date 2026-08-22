#include "uva.h"

#include <stdlib.h>
#include <string.h>

struct uva_unix {
	uv_loop_t *loop;
	uv_pipe_t handle;
	uv_connect_t connect_req;
	uv_write_t write_req;
	uva_future_t fut;
	char *read_dst;
	size_t read_cap;
	ssize_t read_n;
	int initialized;
	int connected;
	int closing;
};

static void on_unix_closed(uv_handle_t *h)
{
	uva_unix_t *c = h->data;
	uva_future_complete(&c->fut, 0);
}

static void alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	uva_unix_t *c = handle->data;
	(void)suggested;
	buf->base = c->read_dst;
	buf->len = c->read_cap;
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
	uva_unix_t *c = stream->data;
	(void)buf;

	uv_read_stop(stream);

	if (nread == UV_EOF) {
		c->read_n = 0;
		uva_future_complete(&c->fut, 0);
		return;
	}
	if (nread < 0) {
		c->read_n = nread;
		uva_future_complete(&c->fut, (int)nread);
		return;
	}

	c->read_n = nread;
	uva_future_complete(&c->fut, 0);
}

static void on_write(uv_write_t *req, int status)
{
	uva_unix_t *c = req->data;
	uva_future_complete(&c->fut, status);
}

static void on_connect(uv_connect_t *req, int status)
{
	uva_unix_t *c = req->data;
	if (status == 0)
		c->connected = 1;
	uva_future_complete(&c->fut, status);
}

uva_unix_t *uva_unix_new(uv_loop_t *loop)
{
	uva_unix_t *c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;

	c->loop = loop;
	uva_future_init(&c->fut, loop);

	if (uv_pipe_init(loop, &c->handle, 0) < 0) {
		free(c);
		return NULL;
	}
	c->handle.data = c;
	c->initialized = 1;
	return c;
}

int uva_unix_connect(uva_unix_t *c, const char *path)
{
	if (!c || !path)
		return UV_EINVAL;

	uva_future_reset(&c->fut);
	c->connect_req.data = c;
	uv_pipe_connect(&c->connect_req, &c->handle, path, on_connect);
	return uva_await(&c->fut);
}

ssize_t uva_unix_write(uva_unix_t *c, const void *buf, size_t len)
{
	uv_buf_t b;
	int r;

	if (!c || !c->connected || !buf)
		return UV_EINVAL;

	uva_future_reset(&c->fut);
	b = uv_buf_init((char *)buf, (unsigned int)len);
	c->write_req.data = c;
	r = uv_write(&c->write_req, (uv_stream_t *)&c->handle, &b, 1, on_write);
	if (r < 0)
		return r;

	r = uva_await(&c->fut);
	if (r < 0)
		return r;
	return (ssize_t)len;
}

ssize_t uva_unix_read(uva_unix_t *c, void *buf, size_t len)
{
	int r;

	if (!c || !c->connected || !buf || len == 0)
		return UV_EINVAL;

	uva_future_reset(&c->fut);
	c->read_dst = buf;
	c->read_cap = len;
	c->read_n = 0;

	r = uv_read_start((uv_stream_t *)&c->handle, alloc_cb, on_read);
	if (r < 0)
		return r;

	r = uva_await(&c->fut);
	if (r < 0)
		return r;
	return c->read_n;
}

int uva_unix_close(uva_unix_t *c)
{
	if (!c || !c->initialized || c->closing)
		return 0;

	c->closing = 1;
	c->connected = 0;
	uva_future_reset(&c->fut);
	uv_close((uv_handle_t *)&c->handle, on_unix_closed);
	return uva_await(&c->fut);
}

void uva_unix_free(uva_unix_t *c)
{
	if (!c)
		return;
	if (c->initialized && !c->closing)
		uva_unix_close(c);
	free(c);
}
