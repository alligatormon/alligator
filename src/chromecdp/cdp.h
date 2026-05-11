#pragma once
#include <jansson.h>
#include <stdint.h>
#include "chromecdp/ws.h"

/* ------------------------------------------------------------------ */
/* CDP pending request                                                 */
/* ------------------------------------------------------------------ */
struct cdp_session;

typedef void (*cdp_response_cb)(struct cdp_session *cdp,
                                json_t *result, json_t *error,
                                void *userdata);

typedef void (*cdp_event_cb)(struct cdp_session *cdp,
                             const char *session_id,
                             const char *method,
                             json_t *params,
                             void *userdata);

typedef void (*cdp_ready_cb) (struct cdp_session *cdp, void *userdata);
typedef void (*cdp_closed_cb)(struct cdp_session *cdp, void *userdata);

typedef struct cdp_pending {
	uint32_t          id;
	cdp_response_cb   cb;
	void             *userdata;
	struct cdp_pending *next;
} cdp_pending;

/* ------------------------------------------------------------------ */
/* CDP session                                                         */
/* ------------------------------------------------------------------ */
typedef struct cdp_session {
	ws_conn          *ws;
	uint32_t          next_id;

	cdp_pending      *pending_head;
	cdp_pending      *pending_tail;

	cdp_event_cb      event_cb;
	cdp_ready_cb      ready_cb;
	cdp_closed_cb     closed_cb;
	void             *userdata;
} cdp_session;

/*
 * Create a CDP session on a WebSocket connection.
 * ready_cb fires once the WebSocket handshake is complete.
 * event_cb fires for every CDP event (push message).
 * closed_cb fires when the WebSocket closes.
 */
cdp_session *cdp_session_new(uv_loop_t *loop,
                             const char *host, int port, const char *ws_path,
                             cdp_ready_cb ready_cb,
                             cdp_event_cb event_cb,
                             cdp_closed_cb closed_cb,
                             void *userdata);

/*
 * Send a CDP request.
 * session_id: NULL for browser-level commands; non-NULL for page sessions.
 * params: borrowed reference; may be NULL.
 * Returns the message id used.
 */
uint32_t cdp_send(cdp_session *cdp,
                  const char *session_id,
                  const char *method,
                  json_t *params,
                  cdp_response_cb cb, void *userdata);

void cdp_session_close(cdp_session *cdp);
void cdp_session_free(cdp_session *cdp);
