#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "chromecdp/cdp.h"
#include "chromecdp/chromecdp.h"
#include "common/logs.h"

/* ------------------------------------------------------------------ */
/* WebSocket callbacks — forward decls                                 */
/* ------------------------------------------------------------------ */
static void cdp_ws_open   (ws_conn *ws);
static void cdp_ws_message(ws_conn *ws, const char *data, size_t len);
static void cdp_ws_close  (ws_conn *ws);

/* ------------------------------------------------------------------ */
/* Public: create session                                              */
/* ------------------------------------------------------------------ */
cdp_session *cdp_session_new(uv_loop_t *loop,
                             const char *host, int port, const char *ws_path,
                             cdp_ready_cb ready_cb,
                             cdp_event_cb event_cb,
                             cdp_closed_cb closed_cb,
                             void *userdata)
{
	cdp_session *cdp = calloc(1, sizeof(*cdp));
	if (!cdp) return NULL;

	cdp->next_id   = 1;
	cdp->ready_cb  = ready_cb;
	cdp->event_cb  = event_cb;
	cdp->closed_cb = closed_cb;
	cdp->userdata  = userdata;

	cdp->ws = ws_connect(loop, host, port, ws_path,
	                     cdp_ws_open, cdp_ws_message, cdp_ws_close,
	                     cdp);
	if (!cdp->ws) {
		free(cdp);
		return NULL;
	}
	return cdp;
}

/* ------------------------------------------------------------------ */
/* Public: send a CDP request                                         */
/* ------------------------------------------------------------------ */
uint32_t cdp_send(cdp_session *cdp,
                  const char *session_id,
                  const char *method,
                  json_t *params,
                  cdp_response_cb cb, void *userdata)
{
	if (!cdp || !cdp->ws || cdp->ws->state != WS_STATE_OPEN)
		return 0;

	uint32_t id = cdp->next_id++;

	json_t *msg = json_object();
	json_object_set_new(msg, "id",     json_integer(id));
	json_object_set_new(msg, "method", json_string(method));

	if (params)
		json_object_set(msg, "params", params);
	else
		json_object_set_new(msg, "params", json_object());

	if (session_id)
		json_object_set_new(msg, "sessionId", json_string(session_id));

	char *text = json_dumps(msg, JSON_COMPACT);
	json_decref(msg);

	if (!text) return 0;

	/* Register pending entry before sending */
	if (cb) {
		cdp_pending *pend = calloc(1, sizeof(*pend));
		if (pend) {
			pend->id       = id;
			pend->cb       = cb;
			pend->userdata = userdata;

			if (!cdp->pending_head) {
				cdp->pending_head = cdp->pending_tail = pend;
			} else {
				cdp->pending_tail->next = pend;
				cdp->pending_tail       = pend;
			}
		}
	}

	ws_send(cdp->ws, text, strlen(text));
	free(text);
	return id;
}

/* ------------------------------------------------------------------ */
/* WebSocket callbacks                                                 */
/* ------------------------------------------------------------------ */
static void cdp_ws_open(ws_conn *ws)
{
	cdp_session *cdp = (cdp_session *)ws->userdata;
	if (cdp->ready_cb)
		cdp->ready_cb(cdp, cdp->userdata);
}

static void cdp_ws_message(ws_conn *ws, const char *data, size_t len)
{
	cdp_session *cdp = (cdp_session *)ws->userdata;

	json_error_t err;
	json_t *msg = json_loadb(data, len, 0, &err);
	if (!msg) {
		cslog(L_DEBUG, "chromecdp: CDP message JSON parse error: %s\n", err.text);
		return;
	}

	json_t *id_j = json_object_get(msg, "id");

	if (id_j && json_is_integer(id_j)) {
		/* This is a response to a pending request */
		uint32_t id = (uint32_t)json_integer_value(id_j);

		cdp_pending *prev = NULL;
		cdp_pending *pend = cdp->pending_head;
		while (pend) {
			if (pend->id == id) {
				/* Unlink */
				if (prev)
					prev->next = pend->next;
				else
					cdp->pending_head = pend->next;
				if (cdp->pending_tail == pend)
					cdp->pending_tail = prev;

				json_t *result = json_object_get(msg, "result");
				json_t *error  = json_object_get(msg, "error");
				pend->cb(cdp, result, error, pend->userdata);
				free(pend);
				break;
			}
			prev = pend;
			pend = pend->next;
		}
	} else {
		/* This is an event */
		json_t *method_j = json_object_get(msg, "method");
		json_t *params_j = json_object_get(msg, "params");

		const char *method     = method_j ? json_string_value(method_j) : NULL;
		json_t     *session_id_j = json_object_get(msg, "sessionId");
		const char *session_id   = session_id_j ? json_string_value(session_id_j) : NULL;

		if (method && cdp->event_cb)
			cdp->event_cb(cdp, session_id, method, params_j, cdp->userdata);
	}

	json_decref(msg);
}

static void cdp_ws_close(ws_conn *ws)
{
	cdp_session *cdp = (cdp_session *)ws->userdata;
	if (cdp->closed_cb)
		cdp->closed_cb(cdp, cdp->userdata);
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */
void cdp_session_close(cdp_session *cdp)
{
	if (cdp && cdp->ws)
		ws_close(cdp->ws);
}

void cdp_session_free(cdp_session *cdp)
{
	if (!cdp) return;

	/* Drain pending queue — do not invoke callbacks on a dying session */
	cdp_pending *p = cdp->pending_head;
	while (p) {
		cdp_pending *next = p->next;
		free(p);
		p = next;
	}
	cdp->pending_head = cdp->pending_tail = NULL;

	if (cdp->ws) {
		ws_conn *ws = cdp->ws;
		cdp->ws = NULL;
		ws->userdata = NULL;
		if (ws->state == WS_STATE_CLOSED)
			ws_conn_free(ws);
		else
			ws_close(ws);
	}

	free(cdp);
}
