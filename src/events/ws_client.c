#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "events/ws_client.h"
#include "events/context_arg.h"
#include "events/metrics.h"
#include "common/ws.h"
#include "common/logs.h"
#include "common/url.h"
#include "parsers/multiparser.h"
#include "main.h"

extern aconf *ac;

/* ------------------------------------------------------------------ */
/* Per-connection state (stored in carg->data)                        */
/* ------------------------------------------------------------------ */

#define WS_CLIENT_RECONNECT_MS 10000   /* default reconnect interval  */

typedef struct ws_client_state {
	ws_conn    *conn;
	uv_timer_t *reconnect_timer;
	int         closing;   /* 1 = ws_client_del called, no reconnect */
} ws_client_state;

/* Forward declaration */
static void ws_client_connect(context_arg *carg);

/* ------------------------------------------------------------------ */
/* Callbacks                                                          */
/* ------------------------------------------------------------------ */

static void on_ws_open(ws_conn *ws)
{
	context_arg *carg = ws->userdata;
	carglog(carg, L_INFO,
	        "ws_client: connected to %s:%s%s\n",
	        carg->host, carg->port,
	        carg->query_url ? carg->query_url : "/");
	carg->conn_counter++;
}

static void on_ws_message(ws_conn *ws, const char *data, size_t len)
{
	context_arg *carg = ws->userdata;
	carg->read_counter++;
	carg->read_bytes_counter += len;

	/* Feed frame content directly into the parser pipeline */
	alligator_multiparser((char *)data, len, carg->parser_handler, NULL, carg);
}

static void reconnect_timer_cb(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	ws_client_connect(carg);
}

static void on_ws_close(ws_conn *ws)
{
	context_arg *carg = ws->userdata;
	ws_client_state *st = carg->data;

	carglog(carg, L_INFO,
	        "ws_client: disconnected from %s:%s\n",
	        carg->host, carg->port);
	carg->close_counter++;

	aggregator_events_metric_add(carg, carg, NULL, "ws", "aggregator", carg->host);

	ws_conn_free(st->conn);
	st->conn = NULL;
	carg->lock = 0;

	if (st->closing)
		return;

	/* Schedule reconnect */
	uint64_t delay = carg->period ? carg->period : WS_CLIENT_RECONNECT_MS;
	uv_timer_start(st->reconnect_timer, reconnect_timer_cb, delay, 0);
}

/* ------------------------------------------------------------------ */
/* Connect                                                            */
/* ------------------------------------------------------------------ */

static void ws_client_connect(context_arg *carg)
{
	if (carg->lock)
		return;

	ws_client_state *st = carg->data;
	if (!st || st->closing)
		return;

	uv_timer_stop(st->reconnect_timer);

	/* Build the WebSocket path — query_url already contains the path
	   component from parse_url (e.g. "/metrics"). */
	const char *path = (carg->query_url && carg->query_url[0])
	                   ? carg->query_url : "/";

	carg->lock = 1;
	carg->connect_time = setrtime();
	carglog(carg, L_INFO,
	        "ws_client: connecting to %s:%s%s\n",
	        carg->host, carg->port, path);

	st->conn = ws_connect(carg->loop, carg->host, carg->numport, path,
	                       on_ws_open, on_ws_message, on_ws_close, carg);
	if (!st->conn) {
		carg->lock = 0;
		/* Retry after delay */
		uint64_t delay = carg->period ? carg->period : WS_CLIENT_RECONNECT_MS;
		uv_timer_start(st->reconnect_timer, reconnect_timer_cb, delay, 0);
	}
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

char *ws_client(context_arg *carg)
{
	ws_client_state *st = calloc(1, sizeof(*st));
	if (!st)
		return NULL;

	carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);

	st->reconnect_timer = calloc(1, sizeof(uv_timer_t));
	if (!st->reconnect_timer) {
		free(st);
		return NULL;
	}
	st->reconnect_timer->data = carg;
	uv_timer_init(carg->loop, st->reconnect_timer);

	carg->data = st;

	ws_client_connect(carg);

	return "ws";
}

static void ws_reconnect_timer_close_cb(uv_handle_t *handle)
{
	free(handle);
}

void ws_client_del(context_arg *carg)
{
	ws_client_state *st = carg->data;
	if (!st)
		return;

	st->closing = 1;

	if (st->conn) {
		ws_close(st->conn);
		/* on_ws_close will call ws_conn_free */
	}

	if (st->reconnect_timer) {
		uv_timer_stop(st->reconnect_timer);
		if (!uv_is_closing((uv_handle_t *)st->reconnect_timer))
			uv_close((uv_handle_t *)st->reconnect_timer,
			         ws_reconnect_timer_close_cb);
		st->reconnect_timer = NULL;
	}

	free(st);
	carg->data = NULL;
}
