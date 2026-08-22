#include "uva.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct uva_udp {
	uv_loop_t *loop;
	uv_udp_t handle;
	uv_udp_send_t send_req;
	uva_future_t fut;
	char *recv_dst;
	size_t recv_cap;
	ssize_t recv_n;
	char peer_ip[64];
	int peer_port;
	char *out_peer_ip;
	size_t out_peer_ip_len;
	int *out_peer_port;
	int initialized;
	int closing;
};

static void on_udp_closed(uv_handle_t *h)
{
	uva_udp_t *u = h->data;
	uva_future_complete(&u->fut, 0);
}

static void alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	uva_udp_t *u = handle->data;
	(void)suggested;
	buf->base = u->recv_dst;
	buf->len = u->recv_cap;
}

static void on_recv(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf,
		    const struct sockaddr *addr, unsigned flags)
{
	uva_udp_t *u = handle->data;
	(void)buf;
	(void)flags;

	/* nread == 0 && addr == NULL means nothing to read this tick. */
	if (nread == 0 && addr == NULL)
		return;

	uv_udp_recv_stop(handle);

	if (nread < 0) {
		u->recv_n = nread;
		uva_future_complete(&u->fut, (int)nread);
		return;
	}

	u->recv_n = nread;
	u->peer_port = 0;
	u->peer_ip[0] = '\0';

	if (addr) {
		if (addr->sa_family == AF_INET) {
			const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
			uv_ip4_name(a, u->peer_ip, sizeof(u->peer_ip));
			u->peer_port = ntohs(a->sin_port);
		} else if (addr->sa_family == AF_INET6) {
			const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)addr;
			uv_ip6_name(a, u->peer_ip, sizeof(u->peer_ip));
			u->peer_port = ntohs(a->sin6_port);
		}
	}

	if (u->out_peer_ip && u->out_peer_ip_len) {
		snprintf(u->out_peer_ip, u->out_peer_ip_len, "%s", u->peer_ip);
	}
	if (u->out_peer_port)
		*u->out_peer_port = u->peer_port;

	uva_future_complete(&u->fut, 0);
}

static void on_send(uv_udp_send_t *req, int status)
{
	uva_udp_t *u = req->data;
	uva_future_complete(&u->fut, status);
}

uva_udp_t *uva_udp_new(uv_loop_t *loop)
{
	uva_udp_t *u = calloc(1, sizeof(*u));
	if (!u)
		return NULL;

	u->loop = loop;
	uva_future_init(&u->fut, loop);

	if (uv_udp_init(loop, &u->handle) < 0) {
		free(u);
		return NULL;
	}
	u->handle.data = u;
	u->initialized = 1;
	return u;
}

int uva_udp_bind(uva_udp_t *u, const char *ip, int port)
{
	struct sockaddr_in addr;
	int r;

	if (!u || !ip)
		return UV_EINVAL;

	r = uv_ip4_addr(ip, port, &addr);
	if (r < 0)
		return r;

	return uv_udp_bind(&u->handle, (const struct sockaddr *)&addr, 0);
}

ssize_t uva_udp_send(uva_udp_t *u, const char *host, int port,
		     const void *buf, size_t len)
{
	struct sockaddr_in addr;
	uv_buf_t b;
	int r;

	if (!u || !host || !buf)
		return UV_EINVAL;

	r = uv_ip4_addr(host, port, &addr);
	if (r < 0)
		return r;

	uva_future_reset(&u->fut);
	b = uv_buf_init((char *)buf, (unsigned int)len);
	u->send_req.data = u;
	r = uv_udp_send(&u->send_req, &u->handle, &b, 1,
			(const struct sockaddr *)&addr, on_send);
	if (r < 0)
		return r;

	r = uva_await(&u->fut);
	if (r < 0)
		return r;
	return (ssize_t)len;
}

ssize_t uva_udp_recv(uva_udp_t *u, void *buf, size_t len,
		     char *peer_ip, size_t peer_ip_len, int *peer_port)
{
	int r;

	if (!u || !buf || len == 0)
		return UV_EINVAL;

	uva_future_reset(&u->fut);
	u->recv_dst = buf;
	u->recv_cap = len;
	u->recv_n = 0;
	u->out_peer_ip = peer_ip;
	u->out_peer_ip_len = peer_ip_len;
	u->out_peer_port = peer_port;

	r = uv_udp_recv_start(&u->handle, alloc_cb, on_recv);
	if (r < 0)
		return r;

	r = uva_await(&u->fut);
	if (r < 0)
		return r;
	return u->recv_n;
}

int uva_udp_close(uva_udp_t *u)
{
	if (!u || !u->initialized || u->closing)
		return 0;

	u->closing = 1;
	uva_future_reset(&u->fut);
	uv_udp_recv_stop(&u->handle);
	uv_close((uv_handle_t *)&u->handle, on_udp_closed);
	return uva_await(&u->fut);
}

void uva_udp_free(uva_udp_t *u)
{
	if (!u)
		return;
	if (u->initialized && !u->closing)
		uva_udp_close(u);
	free(u);
}
