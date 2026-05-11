#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "common/ws.h"
#include "common/base64.h"
#include "common/logs.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void ws_gen_key(char out[29])
{
	uint8_t raw[16];
	for (int i = 0; i < 16; i++)
		raw[i] = (uint8_t)(rand() & 0xff);

	size_t outlen = 0;
	char *enc = base64_encode((const char *)raw, 16, &outlen);
	if (enc) {
		while (outlen > 0 && (enc[outlen-1] == '\n' || enc[outlen-1] == '\r' ||
		                       enc[outlen-1] == ' '))
			outlen--;
		if (outlen <= 28) {
			memcpy(out, enc, outlen);
			out[outlen] = '\0';
		} else {
			memcpy(out, "dGhlIHNhbXBsZSBub25jZQ==", 25);
		}
		free(enc);
	} else {
		memcpy(out, "dGhlIHNhbXBsZSBub25jZQ==", 25);
	}
}

/* ------------------------------------------------------------------ */
/* Write helpers                                                       */
/* ------------------------------------------------------------------ */

typedef struct ws_write_req {
	uv_write_t req;
	char      *buf;
} ws_write_req;

static void ws_write_done(uv_write_t *req, int status)
{
	ws_write_req *wr = (ws_write_req *)req;
	(void)status;
	free(wr->buf);
	free(wr);
}

static int ws_raw_send(ws_conn *ws, char *data, size_t len)
{
	ws_write_req *wr = calloc(1, sizeof(*wr));
	if (!wr) return -1;
	wr->buf = data;
	wr->req.data = wr;

	uv_buf_t buf = uv_buf_init(data, (unsigned int)len);
	int r = uv_write(&wr->req, (uv_stream_t *)&ws->tcp, &buf, 1, ws_write_done);
	if (r != 0) {
		free(wr->buf);
		free(wr);
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Frame encoder                                                       */
/* ------------------------------------------------------------------ */

int ws_send(ws_conn *ws, const char *data, size_t len)
{
	if (!ws || ws->state != WS_STATE_OPEN)
		return -1;

	size_t hdr_len;
	uint8_t hdr[14];

	hdr[0] = 0x81; /* FIN + TEXT */
	if (len <= 125) {
		hdr[1] = 0x80 | (uint8_t)len;
		hdr_len = 2;
	} else if (len <= 0xFFFF) {
		hdr[1] = 0x80 | 126;
		hdr[2] = (uint8_t)(len >> 8);
		hdr[3] = (uint8_t)(len & 0xff);
		hdr_len = 4;
	} else {
		hdr[1] = 0x80 | 127;
		uint64_t l64 = (uint64_t)len;
		for (int i = 0; i < 8; i++)
			hdr[2 + i] = (uint8_t)(l64 >> (56 - 8 * i));
		hdr_len = 10;
	}

	uint8_t mask[4];
	uint32_t m = (uint32_t)rand();
	mask[0] = (m >> 24) & 0xff;
	mask[1] = (m >> 16) & 0xff;
	mask[2] = (m >> 8)  & 0xff;
	mask[3] = m         & 0xff;
	memcpy(hdr + hdr_len, mask, 4);
	hdr_len += 4;

	size_t total = hdr_len + len;
	char *frame = malloc(total);
	if (!frame) return -1;

	memcpy(frame, hdr, hdr_len);
	for (size_t i = 0; i < len; i++)
		frame[hdr_len + i] = data[i] ^ mask[i & 3];

	return ws_raw_send(ws, frame, total);
}

/* ------------------------------------------------------------------ */
/* Frame parser                                                        */
/* ------------------------------------------------------------------ */

static ssize_t ws_parse_one_frame(ws_conn *ws, const char *buf, size_t len)
{
	if (len < 2) return 0;

	uint8_t b0 = (uint8_t)buf[0];
	uint8_t b1 = (uint8_t)buf[1];

	int fin         = (b0 >> 7) & 1;
	int opcode      = b0 & 0x0f;
	int masked      = (b1 >> 7) & 1;
	uint64_t payload_len = b1 & 0x7f;

	size_t hdr = 2;

	if (payload_len == 126) {
		if (len < 4) return 0;
		payload_len = ((uint64_t)(uint8_t)buf[2] << 8) | (uint8_t)buf[3];
		hdr = 4;
	} else if (payload_len == 127) {
		if (len < 10) return 0;
		payload_len = 0;
		for (int i = 0; i < 8; i++)
			payload_len = (payload_len << 8) | (uint8_t)buf[2 + i];
		hdr = 10;
	}

	if (masked) hdr += 4;

	size_t total = hdr + (size_t)payload_len;
	if (len < total) return 0;

	if (opcode == WS_OP_PING) {
		size_t out_len = 2 + (size_t)payload_len;
		char *pong = malloc(out_len);
		if (pong) {
			pong[0] = (char)0x8A;
			pong[1] = (char)payload_len;
			memcpy(pong + 2, buf + hdr, (size_t)payload_len);
			ws_raw_send(ws, pong, out_len);
		}
		return (ssize_t)total;
	}

	if (opcode == WS_OP_CLOSE) {
		if (ws->state == WS_STATE_OPEN) {
			ws->state = WS_STATE_CLOSING;
			size_t out_len = 2;
			char *close_frame = malloc(out_len);
			if (close_frame) {
				close_frame[0] = (char)0x88;
				close_frame[1] = 0x00;
				ws_raw_send(ws, close_frame, out_len);
			}
		}
		return (ssize_t)total;
	}

	if (opcode != WS_OP_TEXT && opcode != WS_OP_BINARY &&
	    opcode != WS_OP_CONTINUATION)
		return (ssize_t)total;

	const char *payload = buf + hdr;

	if (opcode != WS_OP_CONTINUATION)
		ws->fbuf_len = 0;

	size_t needed = ws->fbuf_len + (size_t)payload_len + 1;
	if (needed > ws->fbuf_cap) {
		size_t new_cap = needed * 2;
		char *nb = realloc(ws->fbuf, new_cap);
		if (!nb) return -1;
		ws->fbuf     = nb;
		ws->fbuf_cap = new_cap;
	}
	memcpy(ws->fbuf + ws->fbuf_len, payload, (size_t)payload_len);
	ws->fbuf_len += (size_t)payload_len;

	if (fin) {
		ws->fbuf[ws->fbuf_len] = '\0';
		if (ws->on_message)
			ws->on_message(ws, ws->fbuf, ws->fbuf_len);
		ws->fbuf_len = 0;
	}

	return (ssize_t)total;
}

/* ------------------------------------------------------------------ */
/* HTTP Upgrade handshake parser                                       */
/* ------------------------------------------------------------------ */

static const char *ws_memmem(const char *haystack, size_t hlen,
                              const char *needle,   size_t nlen)
{
	if (nlen == 0) return haystack;
	if (hlen < nlen) return NULL;
	for (size_t i = 0; i <= hlen - nlen; i++) {
		if (memcmp(haystack + i, needle, nlen) == 0)
			return haystack + i;
	}
	return NULL;
}

static int ws_parse_handshake(ws_conn *ws, const char *buf, size_t len)
{
	const char *end = ws_memmem(buf, len, "\r\n\r\n", 4);
	if (!end) return 0;

	if (!strstr(buf, " 101 "))
		return -1;

	ws->state = WS_STATE_OPEN;

	size_t hshake_len = (size_t)(end - buf) + 4;
	if (ws->rbuf_len > hshake_len) {
		size_t extra = ws->rbuf_len - hshake_len;
		memmove(ws->rbuf, ws->rbuf + hshake_len, extra);
		ws->rbuf_len = extra;
	} else {
		ws->rbuf_len = 0;
	}

	if (ws->on_open)
		ws->on_open(ws);

	return 1;
}

/* ------------------------------------------------------------------ */
/* libuv read callback                                                 */
/* ------------------------------------------------------------------ */

static void ws_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	(void)handle;
	buf->base = malloc(suggested);
	buf->len  = buf->base ? (unsigned int)suggested : 0;
}

static void ws_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
	ws_conn *ws = (ws_conn *)stream->data;

	if (nread <= 0) {
		free(buf->base);
		if (nread == UV_EOF || nread == UV_ECONNRESET) {
			ws->state = WS_STATE_CLOSED;
			if (ws->on_close)
				ws->on_close(ws);
		}
		return;
	}

	size_t needed = ws->rbuf_len + (size_t)nread + 1;
	if (needed > ws->rbuf_cap) {
		size_t new_cap = needed * 2;
		if (new_cap > WS_RBUF_MAX) {
			glog(L_ERROR, "ws: receive buffer overflow\n");
			free(buf->base);
			return;
		}
		char *nb = realloc(ws->rbuf, new_cap);
		if (!nb) { free(buf->base); return; }
		ws->rbuf     = nb;
		ws->rbuf_cap = new_cap;
	}
	memcpy(ws->rbuf + ws->rbuf_len, buf->base, (size_t)nread);
	ws->rbuf_len += (size_t)nread;
	free(buf->base);

	if (ws->state == WS_STATE_HANDSHAKE) {
		int r = ws_parse_handshake(ws, ws->rbuf, ws->rbuf_len);
		if (r < 0) {
			glog(L_ERROR, "ws: WebSocket handshake failed\n");
			ws->state = WS_STATE_CLOSED;
			if (ws->on_close) ws->on_close(ws);
			return;
		}
		if (ws->rbuf_len == 0)
			return;
	}

	if (ws->state != WS_STATE_OPEN && ws->state != WS_STATE_CLOSING)
		return;

	size_t consumed = 0;
	while (consumed < ws->rbuf_len) {
		ssize_t n = ws_parse_one_frame(ws,
		                               ws->rbuf + consumed,
		                               ws->rbuf_len - consumed);
		if (n <= 0) break;
		consumed += (size_t)n;
	}
	if (consumed > 0) {
		ws->rbuf_len -= consumed;
		if (ws->rbuf_len > 0)
			memmove(ws->rbuf, ws->rbuf + consumed, ws->rbuf_len);
	}
}

/* ------------------------------------------------------------------ */
/* TCP connect callback                                                */
/* ------------------------------------------------------------------ */

static void ws_tcp_connected(uv_connect_t *req, int status)
{
	ws_conn *ws = (ws_conn *)req->data;

	if (status != 0) {
		glog(L_ERROR, "ws: TCP connect to %s:%d failed: %s\n",
		     ws->host, ws->port, uv_strerror(status));
		ws->state = WS_STATE_CLOSED;
		if (ws->on_close) ws->on_close(ws);
		return;
	}

	ws->state    = WS_STATE_HANDSHAKE;
	ws->tcp.data = ws;
	uv_read_start((uv_stream_t *)&ws->tcp, ws_alloc_cb, ws_read_cb);

	size_t req_len = 256 + strlen(ws->path) + strlen(ws->host) + strlen(ws->sec_key);
	char *http_req = malloc(req_len);
	if (!http_req) return;

	int n = snprintf(http_req, req_len,
	    "GET %s HTTP/1.1\r\n"
	    "Host: %s:%d\r\n"
	    "Upgrade: websocket\r\n"
	    "Connection: Upgrade\r\n"
	    "Sec-WebSocket-Key: %s\r\n"
	    "Sec-WebSocket-Version: 13\r\n"
	    "\r\n",
	    ws->path, ws->host, ws->port, ws->sec_key);

	ws_raw_send(ws, http_req, (size_t)n);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

ws_conn *ws_connect(uv_loop_t *loop,
                    const char *host, int port, const char *path,
                    ws_open_cb on_open, ws_message_cb on_message,
                    ws_close_cb on_close, void *userdata)
{
	ws_conn *ws = calloc(1, sizeof(*ws));
	if (!ws) return NULL;

	ws->host       = strdup(host);
	ws->port       = port;
	ws->path       = strdup(path);
	ws->on_open    = on_open;
	ws->on_message = on_message;
	ws->on_close   = on_close;
	ws->userdata   = userdata;
	ws->state      = WS_STATE_CONNECTING;

	ws->rbuf_cap   = WS_RBUF_INIT;
	ws->rbuf       = malloc(ws->rbuf_cap);
	ws->fbuf_cap   = WS_RBUF_INIT;
	ws->fbuf       = malloc(ws->fbuf_cap);

	ws_gen_key(ws->sec_key);

	uv_tcp_init(loop, &ws->tcp);
	ws->tcp.data         = ws;
	ws->connect_req.data = ws;

	struct sockaddr_in addr;
	uv_ip4_addr(host, port, &addr);
	int r = uv_tcp_connect(&ws->connect_req, &ws->tcp,
	                        (const struct sockaddr *)&addr,
	                        ws_tcp_connected);
	if (r != 0) {
		glog(L_ERROR, "ws: uv_tcp_connect to %s:%d failed: %s\n",
		     host, port, uv_strerror(r));
		ws_conn_free(ws);
		return NULL;
	}
	return ws;
}

static void ws_handle_closed(uv_handle_t *handle)
{
	(void)handle;
}

void ws_close(ws_conn *ws)
{
	if (!ws || ws->state == WS_STATE_CLOSED || ws->state == WS_STATE_CLOSING)
		return;
	ws->state = WS_STATE_CLOSING;

	char *close_frame = malloc(2);
	if (close_frame) {
		close_frame[0] = (char)0x88;
		close_frame[1] = 0x00;
		ws_raw_send(ws, close_frame, 2);
	}

	if (!uv_is_closing((uv_handle_t *)&ws->tcp))
		uv_close((uv_handle_t *)&ws->tcp, ws_handle_closed);
}

void ws_conn_free(ws_conn *ws)
{
	if (!ws) return;
	free(ws->host);
	free(ws->path);
	free(ws->rbuf);
	free(ws->fbuf);
	free(ws);
}
