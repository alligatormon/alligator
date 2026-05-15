#pragma once
#include <jansson.h>
#include <uv.h>
#include "chromecdp/cdp.h"
#include "events/context_arg.h"

/* Page navigation states */
typedef enum {
	PAGE_STATE_IDLE = 0,
	PAGE_STATE_CREATE_CONTEXT,
	PAGE_STATE_CREATE_TARGET,
	PAGE_STATE_ATTACH,
	PAGE_STATE_ENABLE_DOMAINS,
	PAGE_STATE_NAVIGATE,
	PAGE_STATE_WAIT_IDLE,
	PAGE_STATE_GET_PERF,
	PAGE_STATE_EVAL_ENTRIES,
	PAGE_STATE_SCREENSHOT,
	PAGE_STATE_CLOSE_TARGET,
	PAGE_STATE_DISPOSE_CTX,
	PAGE_STATE_DONE,
	PAGE_STATE_ERROR,
} page_state_t;

/* Per-request timing tracker — keyed by CDP requestId */
typedef struct req_track {
	char              id[64];    /* CDP requestId          */
	char              url[512];  /* final URL (may update) */
	double            ts;        /* walltime at send (s)   */
	struct req_track *next;
} req_track;

typedef struct cdp_page cdp_page;
typedef void (*page_done_cb)(cdp_page *page, void *userdata);

struct cdp_page {
	cdp_session    *cdp;
	context_arg    *carg;           /* for metric_add — minimal, owned    */
	json_t         *config;         /* parsed per-URL options (owned)     */
	char           *url;            /* target URL (owned)                 */

	page_state_t    state;

	char           *context_id;    /* browserContextId (owned)           */
	char           *target_id;     /* targetId (owned)                   */
	char           *session_id;    /* CDP session id (owned)             */

	/* networkidle2 tracking */
	int             in_flight;
	int             idle_ticks;
	uv_timer_t     *idle_timer;
	uv_timer_t     *nav_timeout_timer;
	uv_loop_t      *loop;

	int             resp_code;     /* last top-level response code       */

	req_track      *req_map;       /* in-flight request tracker (owned)  */

	page_done_cb    done_cb;
	void           *done_userdata;

	struct cdp_page *next;         /* linked list in chromecdp_state     */
};

/*
 * Allocate a page context and start the navigation chain.
 * carg: borrowed — caller frees after on_done fires.
 * config: ownership transfers to the page; json_decref'd in cdp_page_free.
 */
cdp_page *cdp_page_start(cdp_session *cdp,
                          uv_loop_t *loop,
                          const char *url,
                          json_t *config,
                          context_arg *carg,
                          page_done_cb done_cb,
                          void *done_userdata);

/*
 * Called by chromecdp event dispatch for per-page events.
 * session_id is used to route events to the right page.
 */
void cdp_page_on_event(cdp_page *page,
                       const char *method,
                       json_t *params);

void cdp_page_free(cdp_page *page);
