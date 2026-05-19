#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>

#include "main.h"
#include "chromecdp/chromecdp.h"
#include "chromecdp/cdp.h"
#include "chromecdp/target.h"
#include "common/json_query.h"
#include "events/context_arg.h"
#include "metric/namespace.h"
#include "common/logs.h"
#include "common/units.h"

extern aconf *ac;

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */
void chromecdp_connect_ws(void);
static void chromecdp_do_discovery(void);
static void on_page_done(cdp_page *page, void *ud);

/* ------------------------------------------------------------------ */
/* Internal: global crawler state (singleton)                         */
/* ------------------------------------------------------------------ */

/* Connection states for the Chrome DevTools WebSocket */
#define CDP_CONN_IDLE        0  /* no connection attempt              */
#define CDP_CONN_DISCOVER    1  /* HTTP GET /json/version in progress */
#define CDP_CONN_CONNECTING  2  /* WebSocket upgrade in progress      */
#define CDP_CONN_OPEN        3  /* fully connected                    */
#define CDP_CONN_FAILED      4  /* connection failed, retry next cycle*/

typedef struct chromecdp_disc {
	uv_tcp_t      tcp;
	uv_connect_t  connect_req;
	char          rbuf[8192];
	size_t        rbuf_len;
} chromecdp_disc;

typedef struct chromecdp_state {
	/* Chrome process */
	uv_process_t    chrome_proc;
	uv_stdio_container_t chrome_stdio[3];
	int             chrome_running;
	int             chrome_port;      /* --remote-debugging-port */
	char           *chrome_exec;      /* path to executable       */

	/* CDP connection */
	int             conn_state;
	chromecdp_disc *disc;             /* discovery TCP connection */
	char            ws_path[512];     /* extracted from /json/version */
	cdp_session    *cdp;              /* the live CDP session      */

	/* Active pages (linked list; up to max_concurrent in parallel) */
	cdp_page       *pages_head;
	int             pages_total;
	int             pages_done;

	/* Crawl queue for the current aggregate_period cycle */
	cdp_node      **crawl_queue;
	size_t          crawl_queue_len;
	size_t          crawl_queue_idx;

	/* Batched parallel crawl: batch_size URLs every batch_interval_ms */
	int             max_concurrent;
	int             batch_size;
	uint64_t        batch_interval_ms;
	uint64_t        setup_budget_ms;
	uint64_t        post_nav_budget_ms;
	uv_timer_t      batch_timer;
	int             batch_timer_inited;

	uv_loop_t      *loop;

	int             log_level;        /* L_OFF / L_INFO / L_DEBUG / L_TRACE */
} chromecdp_state;

static chromecdp_state g_cdp_state;
static uv_timer_t      g_chrome_ready_timer;
static int             g_chrome_ready_timer_inited;

void cslog(int priority, const char *format, ...)
{
	chromecdp_state *cs = &g_cdp_state;
	va_list args;

	if (cs->log_level < priority)
		return;
	va_start(args, format);
	wrlog(cs->log_level, priority, format, args);
	va_end(args);
}

static const char *cdp_extract_scalar(const char *key, json_t *value);
static int chromecdp_parse_log_level_json(json_t *value);
static context_arg *cdp_make_carg(json_t *config);
static void cdp_free_carg(context_arg *carg);
static void chromecdp_begin_crawl_cycle(chromecdp_state *cs);
static void chromecdp_fill_slots(chromecdp_state *cs, int max_start);
static void chromecdp_check_deadlines(chromecdp_state *cs);
static void chromecdp_maybe_finish_cycle(chromecdp_state *cs);
static void chromecdp_batch_tick(uv_timer_t *handle);
static void chromecdp_abandon_all_pages(chromecdp_state *cs);

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void chromecdp_sync_runtime(void)
{
	chromecdp_state *cs = &g_cdp_state;

	if (ac->chromecdp_port > 0)
		cs->chrome_port = ac->chromecdp_port;
	if (ac->chromecdp_exec && ac->chromecdp_exec[0])
		cs->chrome_exec = ac->chromecdp_exec;
}

static void chromecdp_chrome_ready_cb(uv_timer_t *handle)
{
	(void)handle;
	chromecdp_state *cs = &g_cdp_state;

	if (!cs->chrome_running)
		return;
	if (cs->disc || cs->cdp)
		return;
	if (cs->conn_state != CDP_CONN_IDLE && cs->conn_state != CDP_CONN_FAILED)
		return;

	cs->conn_state = CDP_CONN_IDLE;
	chromecdp_do_discovery();
}

static void chromecdp_schedule_discovery(void)
{
	chromecdp_state *cs = &g_cdp_state;

	if (!g_chrome_ready_timer_inited) {
		uv_timer_init(cs->loop, &g_chrome_ready_timer);
		g_chrome_ready_timer_inited = 1;
	}
	uv_timer_stop(&g_chrome_ready_timer);
	uv_timer_start(&g_chrome_ready_timer, chromecdp_chrome_ready_cb,
	               CHROMECDP_CHROME_START_DELAY_MS, 0);
}

static int chromecdp_compare(const void *arg, const void *obj)
{
	const char *s1 = (const char *)arg;
	const char *s2 = ((cdp_node *)obj)->url->s;
	return strcmp(s1, s2);
}

static cdp_node *chromecdp_get(const char *url)
{
	return alligator_ht_search(ac->chromecdp, chromecdp_compare,
	                           url, tommy_strhash_u32(0, url));
}

static int cdp_key_is_url(const char *key)
{
	if (!key || !key[0])
		return 0;
	return !strncmp(key, "http://", 7) || !strncmp(key, "https://", 8);
}

typedef struct crawl_queue_build {
	cdp_node **nodes;
	size_t     count;
	size_t     cap;
} crawl_queue_build;

static void crawl_queue_collect(void *funcarg, void *arg)
{
	crawl_queue_build *qb = funcarg;
	cdp_node *node = (cdp_node *)arg;

	if (!cdp_key_is_url(node->url->s))
		return;

	if (qb->count >= qb->cap) {
		size_t ncap = qb->cap ? qb->cap * 2 : 32;
		cdp_node **nn = realloc(qb->nodes, ncap * sizeof(*nn));
		if (!nn)
			return;
		qb->nodes = nn;
		qb->cap   = ncap;
	}
	qb->nodes[qb->count++] = node;
}

static void chromecdp_crawl_queue_free(chromecdp_state *cs)
{
	free(cs->crawl_queue);
	cs->crawl_queue     = NULL;
	cs->crawl_queue_len = 0;
	cs->crawl_queue_idx = 0;
}

static int chromecdp_count_pages(chromecdp_state *cs)
{
	int n = 0;
	cdp_page *p;

	for (p = cs->pages_head; p; p = p->next)
		n++;
	return n;
}

static void chromecdp_batch_timer_stop(chromecdp_state *cs)
{
	if (cs->batch_timer_inited)
		uv_timer_stop(&cs->batch_timer);
}

uint64_t chromecdp_config_timeout_ms(json_t *config)
{
	uint64_t timeout_ms = CHROMECDP_DEFAULT_TIMEOUT_MS;
	json_t *to_j;

	if (!config)
		return timeout_ms;

	to_j = json_object_get(config, "timeout");
	if (!to_j)
		return timeout_ms;

	if (json_is_string(to_j))
		timeout_ms = (uint64_t)get_ms_from_human_range(
		    json_string_value(to_j), json_string_length(to_j));
	else if (json_is_integer(to_j))
		timeout_ms = (uint64_t)json_integer_value(to_j);

	if (timeout_ms < 1000)
		timeout_ms = 1000;
	return timeout_ms;
}

uint64_t chromecdp_page_budget_ms(json_t *config)
{
	uint64_t nav_ms = chromecdp_config_timeout_ms(config);

	/* Hard cap from page start: CDP setup, then nav idle (≤ nav_ms), then collection/teardown. */
	return chromecdp_config_setup_budget_ms() + nav_ms +
	       chromecdp_config_post_nav_budget_ms();
}

static int chromecdp_start_page_for_node(chromecdp_state *cs, cdp_node *node)
{
	json_error_t jerr;
	json_t *config = node->value
	    ? json_loads(node->value, 0, &jerr)
	    : json_object();
	if (!config)
		config = json_object();

	context_arg *carg = cdp_make_carg(config);
	if (!carg) {
		json_decref(config);
		return -1;
	}

	cdp_page *page = cdp_page_start(cs->cdp, cs->loop,
	                                node->url->s, config, carg,
	                                on_page_done, NULL);
	if (!page) {
		cdp_free_carg(carg);
		json_decref(config);
		return -1;
	}

	page->next         = cs->pages_head;
	cs->pages_head     = page;
	page->deadline_ms  = uv_now(cs->loop) + chromecdp_page_budget_ms(config);

	cslog(L_DEBUG, "chromecdp: crawling %s (active %d/%d, deadline %" PRIu64 " ms)\n",
	      node->url->s, chromecdp_count_pages(cs), cs->max_concurrent,
	      chromecdp_page_budget_ms(config));

	return 0;
}

static void chromecdp_maybe_finish_cycle(chromecdp_state *cs)
{
	if (!cs->crawl_queue)
		return;
	if (cs->crawl_queue_idx < cs->crawl_queue_len)
		return;
	if (cs->pages_head)
		return;

	cslog(L_INFO, "chromecdp: crawl cycle finished (%d/%d URLs)\n",
	      cs->pages_done, cs->pages_total);

	chromecdp_batch_timer_stop(cs);
	chromecdp_crawl_queue_free(cs);
}

static void chromecdp_fill_slots(chromecdp_state *cs, int max_start)
{
	int started = 0;

	if (!cs->cdp || cs->conn_state != CDP_CONN_OPEN)
		return;
	if (!cs->crawl_queue)
		return;

	while (started < max_start && cs->crawl_queue_idx < cs->crawl_queue_len) {
		if (chromecdp_count_pages(cs) >= cs->max_concurrent)
			break;

		cdp_node *node = cs->crawl_queue[cs->crawl_queue_idx++];
		if (!cdp_key_is_url(node->url->s))
			continue;
		if (chromecdp_start_page_for_node(cs, node) == 0)
			started++;
	}

	chromecdp_maybe_finish_cycle(cs);
}

static void chromecdp_check_deadlines(chromecdp_state *cs)
{
	uint64_t now = uv_now(cs->loop);
	cdp_page *page = cs->pages_head;

	while (page) {
		cdp_page *next = page->next;

		if (page->deadline_ms && now >= page->deadline_ms) {
			if (!page->finished) {
				cslog(L_WARN, "chromecdp: deadline exceeded, aborting %s\n",
				      page->url);
				cdp_page_abort(page, "crawl deadline exceeded");
			} else if (!page->done_emitted) {
				/* Abort set finished=1 but CDP never replied; release the slot. */
				cslog(L_WARN,
				      "chromecdp: force-releasing stuck page %s after deadline\n",
				      page->url);
				cdp_page_force_finish(page);
			}
		}
		page = next;
	}
}

static void chromecdp_batch_tick(uv_timer_t *handle)
{
	chromecdp_state *cs = (chromecdp_state *)handle->data;

	chromecdp_check_deadlines(cs);
	chromecdp_fill_slots(cs, cs->batch_size);
}

static void chromecdp_abandon_all_pages(chromecdp_state *cs)
{
	cdp_page *page = cs->pages_head;

	cs->pages_head = NULL;
	while (page) {
		cdp_page *next = page->next;
		context_arg *carg = page->carg;

		page->finished     = 1;
		page->magic        = 0;
		page->cdp_inflight = 0;
		page->cdp          = NULL;
		page->carg         = NULL;
		cdp_page_stop_timers(page);
		cdp_free_carg(carg);
		cdp_page_destroy_async(page);
		page = next;
	}
}

static void chromecdp_begin_crawl_cycle(chromecdp_state *cs)
{
	if (cs->pages_head || cs->crawl_queue)
		return;

	crawl_queue_build qb = { NULL, 0, 0 };
	alligator_ht_foreach_arg(ac->chromecdp, crawl_queue_collect, &qb);

	cs->crawl_queue     = qb.nodes;
	cs->crawl_queue_len = qb.count;
	cs->crawl_queue_idx = 0;
	cs->pages_total     = (int)qb.count;
	cs->pages_done      = 0;

	if (!qb.count) {
		cslog(L_INFO, "chromecdp: no http(s) URLs in config\n");
		return;
	}

	if (!cs->batch_timer_inited) {
		uv_timer_init(cs->loop, &cs->batch_timer);
		cs->batch_timer.data = cs;
		cs->batch_timer_inited = 1;
	}

	uv_timer_start(&cs->batch_timer, chromecdp_batch_tick,
	               0, cs->batch_interval_ms);

	cslog(L_INFO,
	      "chromecdp: crawl cycle started (%zu URLs, batch %d every %" PRIu64 " ms, concurrency %d)\n",
	      qb.count, cs->batch_size, cs->batch_interval_ms, cs->max_concurrent);

	/* First batch immediately, then one batch per second */
	chromecdp_fill_slots(cs, cs->batch_size);
}

/* ------------------------------------------------------------------ */
/* Build a minimal context_arg for metric_add()                       */
/* Caller owns the returned carg; free with carg_free().              */
/* ------------------------------------------------------------------ */
static context_arg *cdp_make_carg(json_t *config)
{
	context_arg *carg = calloc(1, sizeof(*carg));
	if (!carg) return NULL;

	/* Use default namespace */
	carg->namespace = NULL;

	/* Copy add_label entries into carg->labels */
	json_t *add_label = json_object_get(config, "add_label");
	if (add_label && json_is_object(add_label)) {
		carg->labels = calloc(1, sizeof(alligator_ht));
		if (carg->labels)
			alligator_ht_init(carg->labels);

		const char *lk;
		json_t *lv;
		json_object_foreach(add_label, lk, lv) {
			const char *lval = json_string_value(lv);
			if (!lval) continue;
			/* Reuse env_struct for label k/v storage */
			env_struct *es = calloc(1, sizeof(*es));
			if (es) {
				es->k = strdup(lk);
				es->v = strdup(lval);
				alligator_ht_insert(carg->labels, &es->node, es,
				                    tommy_strhash_u32(0, es->k));
			}
		}
	}

	/* metricstransform (object/array or JSON string) */
	json_t *mtx = json_object_get(config, "metricstransform");
	if (mtx && (json_is_object(mtx) || json_is_array(mtx)))
		carg->metricstransform = json_incref(mtx);
	else if (mtx && json_is_string(mtx))
	{
		json_error_t mtx_err;
		json_t *mtx_obj = json_loads(json_string_value(mtx), 0, &mtx_err);

		if (mtx_obj && (json_is_object(mtx_obj) || json_is_array(mtx_obj)))
			carg->metricstransform = mtx_obj;
		else if (mtx_obj)
			json_decref(mtx_obj);
	}

	/* TTL: per-URL override, else fall back to global default */
	json_t *json_ttl = json_object_get(config, "ttl");
	if (json_ttl) {
		if (json_typeof(json_ttl) == JSON_STRING)
			carg->ttl = get_sec_from_human_range(json_string_value(json_ttl),
			                                     json_string_length(json_ttl));
		else
			carg->ttl = json_integer_value(json_ttl);
	} else {
		carg->ttl = ac->ttl;
	}

	/* log_level: per-URL override (from URL block), else module default */
	json_t *json_ll = json_object_get(config, "log_level");
	if (json_ll)
		carg->log_level = chromecdp_parse_log_level_json(json_ll);
	else
		carg->log_level = g_cdp_state.log_level;

	return carg;
}

static void cdp_free_carg(context_arg *carg)
{
	if (!carg) return;
	if (carg->labels) {
		alligator_ht_forfree(carg->labels, free);
		free(carg->labels);
	}
	if (carg->metricstransform)
		json_decref(carg->metricstransform);
	free(carg);
}

/* ------------------------------------------------------------------ */
/* Chrome process management                                          */
/* ------------------------------------------------------------------ */

static void on_chrome_exit(uv_process_t *req, int64_t exit_status, int signal)
{
	(void)req; (void)signal;
	cslog(L_WARN, "chromecdp: Chrome exited with status %" PRId64 "\n",
	     exit_status);
	g_cdp_state.chrome_running = 0;
	g_cdp_state.conn_state     = CDP_CONN_IDLE;
	if (g_cdp_state.cdp) {
		cdp_session_close(g_cdp_state.cdp);
		cdp_session_free(g_cdp_state.cdp);
		g_cdp_state.cdp = NULL;
	}
}

static int chromecdp_launch_chrome(void)
{
	chromecdp_state *cs = &g_cdp_state;
	if (cs->chrome_running) return 0;

	chromecdp_sync_runtime();

	char port_arg[32];
	snprintf(port_arg, sizeof(port_arg),
	         "--remote-debugging-port=%d", cs->chrome_port);

	/* --headless is required for full chromium-browser binaries.
	   headless_shell already runs headless but accepts the flag harmlessly. */
	char *argv[] = {
		cs->chrome_exec,
		"--headless",
		port_arg,
		"--no-sandbox",
		"--disable-setuid-sandbox",
		"--disable-dev-shm-usage",
		"--blink-settings=imagesEnabled=false",
		"--disable-gpu",
		NULL
	};

	cs->chrome_stdio[0].flags = UV_IGNORE;
	cs->chrome_stdio[1].flags = UV_IGNORE;
	/* Inherit stderr only when log_level >= info, otherwise suppress Chrome noise */
	if (cs->log_level >= L_INFO) {
		cs->chrome_stdio[2].flags = UV_INHERIT_FD;
		cs->chrome_stdio[2].data.fd = 2;
	} else {
		cs->chrome_stdio[2].flags = UV_IGNORE;
	}

	uv_process_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.exit_cb     = on_chrome_exit;
	opts.file        = cs->chrome_exec;
	opts.args        = argv;
	opts.stdio       = cs->chrome_stdio;
	opts.stdio_count = 3;

	cslog(L_INFO, "chromecdp: launching: %s --headless %s --no-sandbox ...\n",
	      cs->chrome_exec, port_arg);

	int r = uv_spawn(cs->loop, &cs->chrome_proc, &opts);
	if (r != 0) {
		cslog(L_ERROR, "chromecdp: uv_spawn failed: %s\n", uv_strerror(r));
		return -1;
	}
	cs->chrome_running = 1;
	cslog(L_INFO, "chromecdp: Chrome started (pid %d, port %d)\n",
	      cs->chrome_proc.pid, cs->chrome_port);
	chromecdp_schedule_discovery();
	return 0;
}

/* ------------------------------------------------------------------ */
/* Discovery: HTTP GET /json/version → extract WebSocket path         */
/* ------------------------------------------------------------------ */

static void disc_close_cb(uv_handle_t *h)
{
	chromecdp_disc *disc = (chromecdp_disc *)h->data;
	free(disc);
	g_cdp_state.disc = NULL;
}

static void disc_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	(void)handle; (void)suggested;
	chromecdp_disc *disc = (chromecdp_disc *)handle->data;
	size_t remaining = sizeof(disc->rbuf) - disc->rbuf_len - 1;
	buf->base = disc->rbuf + disc->rbuf_len;
	buf->len  = (unsigned int)remaining;
}

static void disc_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
	(void)buf;
	chromecdp_disc *disc = (chromecdp_disc *)stream->data;

	if (nread <= 0) {
		uv_read_stop(stream);
		if (!uv_is_closing((uv_handle_t *)stream))
			uv_close((uv_handle_t *)stream, disc_close_cb);
		return;
	}

	disc->rbuf_len += (size_t)nread;
	disc->rbuf[disc->rbuf_len] = '\0';

	/* Look for the HTTP body (after \r\n\r\n) */
	char *body = strstr(disc->rbuf, "\r\n\r\n");
	if (!body) return; /* need more data */
	body += 4;

	/* Parse JSON to find webSocketDebuggerUrl */
	json_error_t jerr;
	json_t *root = json_loads(body, 0, &jerr);
	if (!root) {
		cslog(L_ERROR, "chromecdp: /json/version parse error: %s\n", jerr.text);
		g_cdp_state.conn_state = CDP_CONN_FAILED;
		uv_read_stop(stream);
		uv_close((uv_handle_t *)stream, disc_close_cb);
		return;
	}

	const char *ws_url = json_string_value(
	    json_object_get(root, "webSocketDebuggerUrl"));
	if (!ws_url) {
		cslog(L_ERROR, "chromecdp: no webSocketDebuggerUrl in /json/version\n");
		json_decref(root);
		g_cdp_state.conn_state = CDP_CONN_FAILED;
		uv_read_stop(stream);
		uv_close((uv_handle_t *)stream, disc_close_cb);
		return;
	}

	/* Extract path from ws://host:port/PATH */
	const char *path_start = strstr(ws_url, "://");
	if (path_start) path_start += 3;
	else             path_start = ws_url;
	path_start = strchr(path_start, '/');
	if (!path_start) path_start = "/";

	strncpy(g_cdp_state.ws_path, path_start, sizeof(g_cdp_state.ws_path) - 1);
	g_cdp_state.ws_path[sizeof(g_cdp_state.ws_path) - 1] = '\0';

	json_decref(root);

	cslog(L_INFO, "chromecdp: got WebSocket path: %s\n", g_cdp_state.ws_path);
	g_cdp_state.conn_state = CDP_CONN_CONNECTING;

	uv_read_stop(stream);
	uv_close((uv_handle_t *)stream, disc_close_cb);

	/* Now open the real WebSocket connection */
	chromecdp_connect_ws();
}

static void disc_write_done_cb(uv_write_t *req, int status)
{
	(void)status;
	free(req->data); /* free the copied request buffer */
	free(req);
}

static void disc_connected(uv_connect_t *req, int status)
{
	chromecdp_disc *disc = (chromecdp_disc *)req->data;

	if (status != 0) {
		cslog(L_ERROR, "chromecdp: discovery connect failed: %s\n",
		     uv_strerror(status));
		g_cdp_state.conn_state = CDP_CONN_FAILED;
		uv_close((uv_handle_t *)&disc->tcp, disc_close_cb);
		return;
	}

	disc->tcp.data = disc;
	uv_read_start((uv_stream_t *)&disc->tcp, disc_alloc_cb, disc_read_cb);

	/* Send HTTP GET */
	char req_buf[256];
	int n = snprintf(req_buf, sizeof(req_buf),
	    "GET /json/version HTTP/1.1\r\n"
	    "Host: 127.0.0.1:%d\r\n"
	    "Connection: close\r\n"
	    "\r\n",
	    g_cdp_state.chrome_port);

	/* Send the GET request; use a heap copy so the write buffer stays valid. */
	char *req_copy = malloc((size_t)n);
	if (!req_copy) return;
	memcpy(req_copy, req_buf, (size_t)n);

	uv_write_t *wr = malloc(sizeof(uv_write_t));
	if (!wr) { free(req_copy); return; }
	wr->data = req_copy;

	uv_buf_t wbuf = uv_buf_init(req_copy, (unsigned int)n);
	uv_write(wr, (uv_stream_t *)&disc->tcp, &wbuf, 1, disc_write_done_cb);
}

static void chromecdp_do_discovery(void)
{
	if (g_cdp_state.disc) return; /* already in progress */

	chromecdp_disc *disc = calloc(1, sizeof(*disc));
	if (!disc) return;

	g_cdp_state.disc       = disc;
	g_cdp_state.conn_state = CDP_CONN_DISCOVER;

	uv_tcp_init(g_cdp_state.loop, &disc->tcp);
	disc->tcp.data        = disc;
	disc->connect_req.data = disc;

	struct sockaddr_in addr;
	uv_ip4_addr("127.0.0.1", g_cdp_state.chrome_port, &addr);
	uv_tcp_connect(&disc->connect_req, &disc->tcp,
	               (const struct sockaddr *)&addr,
	               disc_connected);
}

/* ------------------------------------------------------------------ */
/* WebSocket CDP connection callbacks                                 */
/* ------------------------------------------------------------------ */

static void on_cdp_ready(cdp_session *cdp, void *ud);
static void on_cdp_event(cdp_session *cdp, const char *session_id,
                         const char *method, json_t *params, void *ud);
static void on_cdp_closed(cdp_session *cdp, void *ud);

void chromecdp_connect_ws(void)
{
	chromecdp_state *cs = &g_cdp_state;
	if (cs->cdp) return;

	cs->cdp = cdp_session_new(cs->loop,
	                          "127.0.0.1", cs->chrome_port, cs->ws_path,
	                          on_cdp_ready, on_cdp_event, on_cdp_closed,
	                          cs);
	if (!cs->cdp) {
		cslog(L_ERROR, "chromecdp: cdp_session_new failed\n");
		cs->conn_state = CDP_CONN_FAILED;
	}
}

/* ------------------------------------------------------------------ */
/* CDP event routing                                                  */
/* ------------------------------------------------------------------ */

static void on_cdp_event(cdp_session *cdp, const char *session_id,
                         const char *method, json_t *params, void *ud)
{
	(void)cdp; (void)ud;
	if (!session_id || !method) return;

	/* Find the page with matching session_id */
	chromecdp_state *cs = &g_cdp_state;
	cdp_page *page = cs->pages_head;
	while (page) {
		if (page->session_id && !strcmp(page->session_id, session_id)) {
			cdp_page_on_event(page, method, params);
			return;
		}
		page = page->next;
	}
}

/* ------------------------------------------------------------------ */
/* Page done callback                                                 */
/* ------------------------------------------------------------------ */
static void on_page_done(cdp_page *page, void *ud)
{
	(void)ud;
	chromecdp_state *cs = &g_cdp_state;

	/* Unlink from pages list */
	cdp_page **pp = &cs->pages_head;
	while (*pp) {
		if (*pp == page) { *pp = page->next; break; }
		pp = &(*pp)->next;
	}

	/* Release context_arg; defer struct free until after pending CDP I/O. */
	{
		context_arg *carg = page->carg;
		page->carg = NULL;
		cdp_free_carg(carg);
	}
	cdp_page_destroy_async(page);

	cs->pages_done++;
	cslog(L_DEBUG, "chromecdp: page done (%d/%d, active %d)\n",
	      cs->pages_done, cs->pages_total, chromecdp_count_pages(cs));

	chromecdp_fill_slots(cs, cs->batch_size);
}

static void on_cdp_ready(cdp_session *cdp, void *ud)
{
	(void)cdp;
	chromecdp_state *cs = (chromecdp_state *)ud;
	cs->conn_state = CDP_CONN_OPEN;

	cslog(L_INFO, "chromecdp: CDP WebSocket connected\n");

	cs->pages_head = NULL;
	chromecdp_begin_crawl_cycle(cs);
}

static void on_cdp_closed(cdp_session *cdp, void *ud)
{
	chromecdp_state *cs = (chromecdp_state *)ud;

	cslog(L_INFO, "chromecdp: CDP WebSocket closed\n");

	cs->conn_state = CDP_CONN_IDLE;
	chromecdp_batch_timer_stop(cs);
	chromecdp_crawl_queue_free(cs);

	/* Active pages must not call cdp_send after the session is freed. */
	chromecdp_abandon_all_pages(cs);

	if (cs->cdp) {
		cdp_session *dead = cs->cdp;
		cs->cdp = NULL;
		cdp_session_free(dead);
	} else if (cdp) {
		cdp_session_free(cdp);
	}
}

/* ------------------------------------------------------------------ */
/* Main timer callback                                                */
/* ------------------------------------------------------------------ */
static void chromecdp_crawl(uv_timer_t *handle)
{
	(void)handle;
	chromecdp_state *cs = &g_cdp_state;

	if (!alligator_ht_count(ac->chromecdp))
		return;

	/* Ensure Chrome is running */
	if (!cs->chrome_running) {
		if (chromecdp_launch_chrome() != 0)
			return;
		/* Discovery is scheduled by chromecdp_launch_chrome() */
		return;
	}

	switch (cs->conn_state) {
	case CDP_CONN_IDLE:
		chromecdp_do_discovery();
		break;

	case CDP_CONN_FAILED:
		/* Reset and retry on next cycle */
		cs->conn_state = CDP_CONN_IDLE;
		break;

	case CDP_CONN_OPEN:
		chromecdp_check_deadlines(cs);
		if (!cs->crawl_queue && !cs->pages_head)
			chromecdp_begin_crawl_cycle(cs);
		else if (cs->crawl_queue)
			chromecdp_fill_slots(cs, cs->batch_size);
		break;

	case CDP_CONN_DISCOVER:
	case CDP_CONN_CONNECTING:
		/* In progress — wait */
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Public: node management (mirrors puppeteer API)                    */
/* ------------------------------------------------------------------ */

/* Extract a string value from plain-parser output.
   Plain parser wraps scalars as {"key": "value"}, JSON config passes them
   directly as string or integer. */
static const char *cdp_extract_scalar(const char *key, json_t *value)
{
	if (json_is_string(value))
		return json_string_value(value);
	if (json_is_object(value)) {
		json_t *inner = json_object_get(value, key);
		if (inner && json_is_string(inner))
			return json_string_value(inner);
	}
	return NULL;
}

static int chromecdp_parse_log_level_json(json_t *value)
{
	const char *s;

	if (!value)
		return L_OFF;
	if (json_is_string(value))
		return (int)get_log_level_by_name(json_string_value(value),
		                                  json_string_length(value));
	if (json_is_integer(value))
		return (int)json_integer_value(value);
	s = cdp_extract_scalar("log_level", value);
	if (s)
		return (int)get_log_level_by_name(s, strlen(s));
	return L_OFF;
}

void cdp_insert(json_t *root)
{
	const char *key;
	json_t *value;
	json_object_foreach(root, key, value) {
		/* Skip spurious self-reference produced by plain parser */
		if (!strcmp(key, "chromecdp"))
			continue;

		/* port — scalar directive, not a URL */
		if (!strcmp(key, "port")) {
			int port = 0;
			if (json_is_integer(value))
				port = (int)json_integer_value(value);
			else {
				const char *s = cdp_extract_scalar("port", value);
				if (s) port = atoi(s);
			}
			if (port > 0) {
				ac->chromecdp_port = port;
				chromecdp_sync_runtime();
				cslog(L_INFO, "chromecdp: port set to %d\n", port);
			}
			continue;
		}

		/* executable — scalar directive, not a URL */
		if (!strcmp(key, "executable")) {
			const char *exec = cdp_extract_scalar("executable", value);
			if (exec) {
				free(ac->chromecdp_exec);
				ac->chromecdp_exec = strdup(exec);
				chromecdp_sync_runtime();
				cslog(L_INFO, "chromecdp: executable set to '%s'\n", exec);
			}
			continue;
		}

		/* concurrency — max parallel page crawls */
		if (!strcmp(key, "concurrency")) {
			int v = 0;
			if (json_is_integer(value))
				v = (int)json_integer_value(value);
			else {
				const char *s = cdp_extract_scalar("concurrency", value);
				if (s) v = atoi(s);
			}
			if (v > 0)
				g_cdp_state.max_concurrent = v;
			continue;
		}

		/* batch_size — URLs to start on each batch tick */
		if (!strcmp(key, "batch_size")) {
			int v = 0;
			if (json_is_integer(value))
				v = (int)json_integer_value(value);
			else {
				const char *s = cdp_extract_scalar("batch_size", value);
				if (s) v = atoi(s);
			}
			if (v > 0)
				g_cdp_state.batch_size = v;
			continue;
		}

		/* batch_interval — delay between batch ticks (e.g. "1s") */
		if (!strcmp(key, "batch_interval")) {
			uint64_t ms = 0;
			if (json_is_string(value))
				ms = (uint64_t)get_ms_from_human_range(
				    json_string_value(value), json_string_length(value));
			else if (json_is_integer(value))
				ms = (uint64_t)json_integer_value(value);
			if (ms > 0)
				g_cdp_state.batch_interval_ms = ms;
			continue;
		}

		/* setup_budget — CDP attach/enable slack before navigation (ms or "10s") */
		if (!strcmp(key, "setup_budget")) {
			uint64_t ms = 0;
			if (json_is_string(value))
				ms = (uint64_t)get_ms_from_human_range(
				    json_string_value(value), json_string_length(value));
			else if (json_is_integer(value))
				ms = (uint64_t)json_integer_value(value);
			if (ms > 0)
				g_cdp_state.setup_budget_ms = ms;
			continue;
		}

		/* post_nav_budget — perf/eval/screenshot/teardown slack after navigation */
		if (!strcmp(key, "post_nav_budget")) {
			uint64_t ms = 0;
			if (json_is_string(value))
				ms = (uint64_t)get_ms_from_human_range(
				    json_string_value(value), json_string_length(value));
			else if (json_is_integer(value))
				ms = (uint64_t)json_integer_value(value);
			if (ms > 0)
				g_cdp_state.post_nav_budget_ms = ms;
			continue;
		}

		/* log_level — off/info/debug/trace or L_* integer */
		if (!strcmp(key, "log_level")) {
			g_cdp_state.log_level = chromecdp_parse_log_level_json(value);
			continue;
		}

		/* Only http(s) keys are target URLs */
		if (!cdp_key_is_url(key)) {
			cslog(L_TRACE, "chromecdp: skip non-URL key '%s'\n", key);
			continue;
		}

		{
			char *new_value = json_dumps(value, 0);
			if (!new_value)
				continue;

			cdp_node *existing = chromecdp_get(key);
			if (existing) {
				free(existing->value);
				existing->value = new_value;
				cslog(L_INFO, "chromecdp: update url '%s'\n", key);
				continue;
			}

			cdp_node *node = calloc(1, sizeof(*node));
			if (!node) {
				free(new_value);
				continue;
			}
			node->url   = string_init_dup((char *)key);
			node->value = new_value;

			cslog(L_INFO, "chromecdp: insert url '%s'\n", node->url->s);

			alligator_ht_insert(ac->chromecdp, &node->node, node,
			                    tommy_strhash_u32(0, node->url->s));
		}
	}
}

void cdp_delete(json_t *root)
{
	const char *key;
	json_t *value;
	json_object_foreach(root, key, value) {
		(void)value;
		cdp_node *node = chromecdp_get(key);
		if (!node) continue;
		if (alligator_ht_remove_existing(ac->chromecdp, &node->node)) {
			string_free(node->url);
			free(node->value);
			free(node);
		}
	}
}

static void cdp_foreach_free(void *funcarg, void *arg)
{
	(void)funcarg;
	cdp_node *node = (cdp_node *)arg;
	string_free(node->url);
	free(node->value);
	free(node);
}

void cdp_done(void)
{
	chromecdp_batch_timer_stop(&g_cdp_state);
	chromecdp_crawl_queue_free(&g_cdp_state);

	alligator_ht_foreach_arg(ac->chromecdp, cdp_foreach_free, NULL);
	alligator_ht_done(ac->chromecdp);
	free(ac->chromecdp);
	ac->chromecdp = NULL;

	/* Kill Chrome if we started it */
	if (g_cdp_state.chrome_running)
		uv_process_kill(&g_cdp_state.chrome_proc, SIGTERM);

	if (g_cdp_state.cdp) {
		cdp_session_close(g_cdp_state.cdp);
		cdp_session_free(g_cdp_state.cdp);
		g_cdp_state.cdp = NULL;
	}
}

int chromecdp_config_log_level(void)
{
	return g_cdp_state.log_level;
}

int chromecdp_config_concurrency(void)
{
	return g_cdp_state.max_concurrent;
}

int chromecdp_config_batch_size(void)
{
	return g_cdp_state.batch_size;
}

uint64_t chromecdp_config_batch_interval_ms(void)
{
	return g_cdp_state.batch_interval_ms;
}

uint64_t chromecdp_config_setup_budget_ms(void)
{
	return g_cdp_state.setup_budget_ms;
}

uint64_t chromecdp_config_post_nav_budget_ms(void)
{
	return g_cdp_state.post_nav_budget_ms;
}

/* ------------------------------------------------------------------ */
/* Public: generator (called from main.c)                            */
/* ------------------------------------------------------------------ */
void chromecdp_generator(void)
{
	chromecdp_state *cs = &g_cdp_state;
	cs->loop = ac->loop;

	/* Read config: chrome_port, chrome_exec */
	cs->chrome_port = CHROMECDP_DEFAULT_PORT;
	cs->chrome_exec = CHROMECDP_DEFAULT_EXEC;

	cs->max_concurrent     = CHROMECDP_DEFAULT_MAX_CONCURRENT;
	cs->batch_size         = CHROMECDP_DEFAULT_BATCH_SIZE;
	cs->batch_interval_ms  = CHROMECDP_BATCH_INTERVAL_MS;
	cs->setup_budget_ms    = CHROMECDP_SETUP_BUDGET_MS;
	cs->post_nav_budget_ms = CHROMECDP_POST_NAV_BUDGET_MS;

	/* Override from ac->chromecdp_port / ac->chromecdp_exec if set */
	if (ac->chromecdp_port > 0)
		cs->chrome_port = ac->chromecdp_port;
	if (ac->chromecdp_exec && ac->chromecdp_exec[0])
		cs->chrome_exec = ac->chromecdp_exec;

	uv_timer_init(ac->loop, &ac->chromecdp_timer);
	uv_timer_start(&ac->chromecdp_timer, chromecdp_crawl,
	               ac->aggregator_startup + CHROMECDP_CHROME_START_DELAY_MS,
	               ac->aggregator_repeat);
}
