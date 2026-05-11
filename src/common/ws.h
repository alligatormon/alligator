#pragma once
#include <uv.h>
#include <stdint.h>
#include <stddef.h>

/* WebSocket opcodes (RFC 6455) */
#define WS_OP_CONTINUATION 0x0
#define WS_OP_TEXT         0x1
#define WS_OP_BINARY       0x2
#define WS_OP_CLOSE        0x8
#define WS_OP_PING         0x9
#define WS_OP_PONG         0xA

/* Connection states */
#define WS_STATE_INIT        0
#define WS_STATE_CONNECTING  1
#define WS_STATE_HANDSHAKE   2
#define WS_STATE_OPEN        3
#define WS_STATE_CLOSING     4
#define WS_STATE_CLOSED      5

#define WS_RBUF_INIT     65536
#define WS_RBUF_MAX      (8 * 1024 * 1024)

struct ws_conn;

typedef void (*ws_open_cb)   (struct ws_conn *ws);
typedef void (*ws_message_cb)(struct ws_conn *ws, const char *data, size_t len);
typedef void (*ws_close_cb)  (struct ws_conn *ws);

typedef struct ws_conn {
	uv_tcp_t       tcp;
	uv_connect_t   connect_req;

	char          *host;
	int            port;
	char          *path;
	char           sec_key[29];

	ws_open_cb    on_open;
	ws_message_cb on_message;
	ws_close_cb   on_close;
	void         *userdata;

	int            state;

	char          *rbuf;
	size_t         rbuf_len;
	size_t         rbuf_cap;

	char          *fbuf;
	size_t         fbuf_len;
	size_t         fbuf_cap;
} ws_conn;

/*
 * Allocate and begin a WebSocket connection.
 * Calls on_open when handshake is complete.
 * Calls on_message for each complete text/binary frame.
 * Calls on_close on disconnect.
 */
ws_conn *ws_connect(uv_loop_t *loop,
                    const char *host, int port, const char *path,
                    ws_open_cb on_open, ws_message_cb on_message,
                    ws_close_cb on_close, void *userdata);

/* Send a UTF-8 text frame (masked, as required for client→server). */
int ws_send(ws_conn *ws, const char *data, size_t len);

/* Initiate a clean close handshake. */
void ws_close(ws_conn *ws);

/* Release all resources (call only after on_close has fired). */
void ws_conn_free(ws_conn *ws);
