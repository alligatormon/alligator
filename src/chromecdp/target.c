#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "chromecdp/chromecdp.h"
#include "chromecdp/target.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "common/logs.h"
#include "common/units.h"
#include "events/fs_write.h"
#include "common/base64.h"
#include "common/mkdirp.h"

/* ------------------------------------------------------------------ */
/* Forward declarations for the navigation callback chain             */
/* ------------------------------------------------------------------ */
static void step_create_context   (cdp_page *page);
static void step_create_target    (cdp_page *page);
static void step_attach           (cdp_page *page);
static void step_enable_domains   (cdp_page *page);
static void step_navigate         (cdp_page *page);
static void step_wait_idle        (cdp_page *page);
static void step_get_perf_metrics (cdp_page *page);
static void step_eval_entries     (cdp_page *page);
static void step_screenshot       (cdp_page *page);
static void step_close_target     (cdp_page *page);
static void step_dispose_context  (cdp_page *page);
static void page_try_finish        (cdp_page *page);
static void page_emit_done         (cdp_page *page);
static void page_cdp_rsp_done      (cdp_page *page);
static uint32_t page_cdp_send      (cdp_page *page, cdp_session *cdp,
                                    const char *session_id, const char *method,
                                    json_t *params, cdp_response_cb cb,
                                    void *userdata);
static void page_error            (cdp_page *page, const char *reason);
static void idle_timer_closed     (uv_handle_t *handle);
static void page_destroy_idle_cb  (uv_idle_t *handle);

#define PAGE_MAGIC 0xCD050A6Eu

#define CDP_PAGE_GUARD(page) \
	do { \
		if (!(page) || (page)->magic != PAGE_MAGIC || (page)->finished) \
			return; \
	} while (0)

/* ------------------------------------------------------------------ */
/* CDP request lifetime (page must outlive pending callbacks)         */
/* ------------------------------------------------------------------ */
static void page_cdp_begin(cdp_page *page)
{
	if (page && page->magic == PAGE_MAGIC)
		page->cdp_inflight++;
}

static void page_cdp_rsp_done(cdp_page *page)
{
	if (!page || page->magic != PAGE_MAGIC)
		return;

	page->cdp_inflight--;
	if (page->cdp_inflight < 0)
		page->cdp_inflight = 0;

	if (page->finished && page->cdp_inflight == 0)
		page_emit_done(page);
}

static uint32_t page_cdp_send(cdp_page *page, cdp_session *cdp,
                              const char *session_id, const char *method,
                              json_t *params, cdp_response_cb cb,
                              void *userdata)
{
	uint32_t id;

	if (cb)
		page_cdp_begin(page);
	id = cdp_send(cdp, session_id, method, params, cb, userdata);
	if (cb && id == 0)
		page_cdp_rsp_done(page);
	return id;
}

static void page_emit_done(cdp_page *page)
{
	if (!page || page->magic != PAGE_MAGIC || page->done_emitted)
		return;

	page->done_emitted = 1;
	if (page->done_cb)
		page->done_cb(page, page->done_userdata);
}

/* ------------------------------------------------------------------ */
/* Metric emission helpers                                             */
/* ------------------------------------------------------------------ */

static void emit_double(cdp_page *page, const char *name,
                        const char *lname, const char *lval,
                        double value)
{
	/* metric_add_labels takes char* — safe cast, values are not mutated */
	metric_add_labels((char *)name, &value, DATATYPE_DOUBLE, page->carg,
	                  (char *)lname, (char *)lval);
}

/* Safe numeric extraction — handles JSON real, integer, and NULL. */
static double json_num(json_t *v)
{
	if (!v) return 0.0;
	if (json_is_real(v))    return json_real_value(v);
	if (json_is_integer(v)) return (double)json_integer_value(v);
	return 0.0;
}

/* Truncate a string to at most 128 chars (matching puppeteer behaviour). */
static const char *trunc128(const char *s, char *buf, size_t bufsz)
{
	if (!s) return "";
	size_t l = strlen(s);
	if (l <= 128) return s;
	size_t t = bufsz - 1 < 128 ? bufsz - 1 : 128;
	memcpy(buf, s, t);
	buf[t] = '\0';
	return buf;
}

/* ------------------------------------------------------------------ */
/* CDP event handler (called from chromecdp event dispatch)           */
/* ------------------------------------------------------------------ */
void cdp_page_on_event(cdp_page *page, const char *method, json_t *params)
{
	if (!page || !method || page->magic != PAGE_MAGIC || page->finished)
		return;

	char buf128[129];

	if (!strcmp(method, "Network.requestWillBeSent") && params) {
		page->in_flight++;
		page->idle_ticks = 0;

		/* Record request start time and URL for duration/size tracking */
		const char *req_id  = json_string_value(json_object_get(params, "requestId"));
		json_t     *req_obj = json_object_get(params, "request");
		const char *url_s   = json_string_value(json_object_get(req_obj, "url"));
		double      ts      = json_num(json_object_get(params, "timestamp"));
		if (req_id) {
			req_track *rt = calloc(1, sizeof(*rt));
			if (rt) {
				strncpy(rt->id,  req_id, sizeof(rt->id)  - 1);
				if (url_s)
					strncpy(rt->url, url_s, sizeof(rt->url) - 1);
				rt->ts   = ts;
				rt->next = page->req_map;
				page->req_map = rt;
			}
		}

	} else if (!strcmp(method, "Network.responseReceived") && params) {
		json_t     *resp   = json_object_get(params, "response");
		int         status = (int)json_integer_value(json_object_get(resp, "status"));
		const char *url    = json_string_value(json_object_get(resp, "url"));
		if (!url) url = "";

		/* Track top-level page response code for screenshot threshold */
		if (page->resp_code == 0)
			page->resp_code = status;

		int64_t st = (int64_t)status;
		metric_add_labels2("chromecdp_resource_http_status",
		    &st, DATATYPE_INT, page->carg,
		    "resource", page->url,
		    "source",   (char *)trunc128(url, buf128, sizeof(buf128)));

		/* Update the URL in req_map (may change after redirects) */
		const char *req_id = json_string_value(json_object_get(params, "requestId"));
		if (req_id) {
			for (req_track *c = page->req_map; c; c = c->next) {
				if (strcmp(c->id, req_id) == 0) {
					strncpy(c->url, url, sizeof(c->url) - 1);
					break;
				}
			}
		}

	} else if (!strcmp(method, "Network.loadingFinished") && params) {
		if (page->in_flight > 0) page->in_flight--;

		const char *req_id  = json_string_value(json_object_get(params, "requestId"));
		double      ts      = json_num(json_object_get(params, "timestamp"));
		double      enc_len = json_num(json_object_get(params, "encodedDataLength"));

		if (req_id) {
			req_track *rt = NULL, *prev = NULL;
			for (req_track *c = page->req_map; c; prev = c, c = c->next) {
				if (strcmp(c->id, req_id) == 0) {
					rt = c;
					if (prev) prev->next = c->next;
					else      page->req_map = c->next;
					break;
				}
			}
			if (rt) {
				double dur_ms = (ts - rt->ts) * 1000.0;
				char trunc_url[129];
				const char *src = trunc128(rt->url, trunc_url, sizeof(trunc_url));

				metric_add_labels2("chromecdp_resource_duration_milliseconds",
				    &dur_ms, DATATYPE_DOUBLE, page->carg,
				    "resource", page->url,
				    "source",   (char *)src);

				if (enc_len > 0)
					metric_add_labels2("chromecdp_resource_size_bytes",
					    &enc_len, DATATYPE_DOUBLE, page->carg,
					    "resource", page->url,
					    "source",   (char *)src);

				free(rt);
			}
		}

	} else if (!strcmp(method, "Network.loadingFailed") && params) {
		if (page->in_flight > 0) page->in_flight--;

		const char *req_id = json_string_value(json_object_get(params, "requestId"));
		if (req_id) {
			req_track *rt = NULL, *prev = NULL;
			for (req_track *c = page->req_map; c; prev = c, c = c->next) {
				if (strcmp(c->id, req_id) == 0) {
					rt = c;
					if (prev) prev->next = c->next;
					else      page->req_map = c->next;
					break;
				}
			}
			if (rt) {
				int64_t one = 1;
				metric_add_labels2("chromecdp_resource_failures_total",
				    &one, DATATYPE_INT, page->carg,
				    "resource", page->url,
				    "source",   (char *)trunc128(rt->url, buf128, sizeof(buf128)));
				free(rt);
			}
		}

	} else if (!strcmp(method, "Runtime.consoleAPICalled") && params) {
		json_t *config = page->config;
		int emit = json_is_true(json_object_get(config, "console_events")) ||
		           json_integer_value(json_object_get(config, "console_events")) == 1;
		if (!emit) return;

		/* Concatenate all args as text */
		json_t *args = json_object_get(params, "args");
		char textbuf[512] = {0};
		if (json_is_array(args)) {
			size_t idx;
			json_t *arg;
			size_t pos = 0;
			json_array_foreach(args, idx, arg) {
				const char *val = json_string_value(json_object_get(arg, "value"));
				if (!val) val = json_string_value(json_object_get(arg, "description"));
				if (!val) continue;
				int n = snprintf(textbuf + pos, sizeof(textbuf) - pos, "%s ", val);
				if (n > 0) pos += (size_t)n;
				if (pos >= sizeof(textbuf) - 1) break;
			}
		}
		/* Remove newlines */
		for (char *p = textbuf; *p; p++)
			if (*p == '\r' || *p == '\n') *p = ' ';

		int64_t one = 1;
		metric_add_labels2("chromecdp_console_messages_total",
		    &one, DATATYPE_INT, page->carg,
		    "resource", page->url,
		    "text",     textbuf);

	} else if (!strcmp(method, "Runtime.exceptionThrown") && params) {
		json_t *det = json_object_get(params, "exceptionDetails");
		const char *msg = json_string_value(json_object_get(det, "text"));
		if (!msg) msg = "error";

		char msgbuf[256];
		strncpy(msgbuf, msg, sizeof(msgbuf) - 1);
		msgbuf[sizeof(msgbuf) - 1] = '\0';
		/* Remove newlines */
		for (char *p = msgbuf; *p; p++)
			if (*p == '\r' || *p == '\n') *p = ' ';

		int64_t one = 1;
		metric_add_labels2("chromecdp_page_errors_total",
		    &one, DATATYPE_INT, page->carg,
		    "resource", page->url,
		    "text",     msgbuf);
	}
}

/* ------------------------------------------------------------------ */
/* Timer helpers — detach page pointer before uv_timer_stop/uv_close  */
/* ------------------------------------------------------------------ */
void cdp_page_stop_timers(cdp_page *page)
{
	if (!page)
		return;

	if (page->idle_timer) {
		uv_timer_t *t = page->idle_timer;
		page->idle_timer = NULL;
		t->data          = NULL;
		uv_timer_stop(t);
		if (!uv_is_closing((uv_handle_t *)t))
			uv_close((uv_handle_t *)t, idle_timer_closed);
		else
			free(t);
	}

	if (page->nav_timeout_timer) {
		uv_timer_t *t = page->nav_timeout_timer;
		page->nav_timeout_timer = NULL;
		t->data                 = NULL;
		uv_timer_stop(t);
		if (!uv_is_closing((uv_handle_t *)t))
			uv_close((uv_handle_t *)t, idle_timer_closed);
		else
			free(t);
	}
}

static void page_destroy_idle_cb(uv_idle_t *handle)
{
	cdp_page *page = (cdp_page *)handle->data;

	if (page && page->magic == PAGE_MAGIC && page->cdp_inflight > 0)
		return;

	uv_idle_stop(handle);
	uv_close((uv_handle_t *)handle, (uv_close_cb)free);
	if (page)
		cdp_page_free(page);
}

void cdp_page_destroy_async(cdp_page *page)
{
	uv_idle_t *idle;

	if (!page || !page->loop)
		return;

	idle = malloc(sizeof(*idle));
	if (!idle) {
		cdp_page_free(page);
		return;
	}

	uv_idle_init(page->loop, idle);
	idle->data = page;
	uv_idle_start(idle, page_destroy_idle_cb);
}

/* ------------------------------------------------------------------ */
/* Idle timer (networkidle2 implementation)                           */
/* ------------------------------------------------------------------ */
static void nav_timeout_cb(uv_timer_t *timer)
{
	cdp_page *page = (cdp_page *)timer->data;

	CDP_PAGE_GUARD(page);
	if (page->state != PAGE_STATE_WAIT_IDLE)
		return;

	cdp_page_stop_timers(page);
	step_get_perf_metrics(page);
}

static void idle_tick(uv_timer_t *timer)
{
	cdp_page *page = (cdp_page *)timer->data;

	CDP_PAGE_GUARD(page);

	if (page->in_flight <= CHROMECDP_MAX_INFLIGHT) {
		page->idle_ticks++;
		if (page->idle_ticks >= CHROMECDP_IDLE_TICKS) {
			cdp_page_stop_timers(page);
			step_get_perf_metrics(page);
			return;
		}
	} else {
		page->idle_ticks = 0;
	}
}

/* ------------------------------------------------------------------ */
/* Step 1: Create incognito browser context                           */
/* ------------------------------------------------------------------ */
static void on_context_created(cdp_session *cdp, json_t *result,
                                json_t *error, void *ud)
{
	cdp_page *page = (cdp_page *)ud;
	(void)cdp;

	page_cdp_rsp_done(page);
	CDP_PAGE_GUARD(page);

	if (error || !result) {
		const char *msg = error
		    ? json_string_value(json_object_get(error, "message"))
		    : NULL;
		page_error(page, msg ? msg : "createBrowserContext failed");
		return;
	}

	const char *ctx_id = json_string_value(
	    json_object_get(result, "browserContextId"));
	if (!ctx_id) { page_error(page, "no browserContextId"); return; }

	page->context_id = strdup(ctx_id);
	page->state = PAGE_STATE_CREATE_TARGET;
	step_create_target(page);
}

static void step_create_context(cdp_page *page)
{
	page->state = PAGE_STATE_CREATE_CONTEXT;
	page_cdp_send(page, page->cdp, NULL, "Target.createBrowserContext",
	              NULL, on_context_created, page);
}

/* ------------------------------------------------------------------ */
/* Step 2: Create a new page target                                   */
/* ------------------------------------------------------------------ */
static void on_target_created(cdp_session *cdp, json_t *result,
                               json_t *error, void *ud)
{
	cdp_page *page = (cdp_page *)ud;
	(void)cdp;

	page_cdp_rsp_done(page);
	CDP_PAGE_GUARD(page);

	if (error || !result) {
		const char *msg = error
		    ? json_string_value(json_object_get(error, "message"))
		    : NULL;
		page_error(page, msg ? msg : "createTarget failed");
		return;
	}

	const char *tid = json_string_value(json_object_get(result, "targetId"));
	if (!tid) { page_error(page, "no targetId"); return; }

	page->target_id = strdup(tid);
	page->state = PAGE_STATE_ATTACH;
	step_attach(page);
}

static void step_create_target(cdp_page *page)
{
	page->state = PAGE_STATE_CREATE_TARGET;
	json_t *params = json_object();
	json_object_set_new(params, "url",              json_string("about:blank"));
	json_object_set_new(params, "browserContextId", json_string(page->context_id));
	page_cdp_send(page, page->cdp, NULL, "Target.createTarget",
	              params, on_target_created, page);
	json_decref(params);
}

/* ------------------------------------------------------------------ */
/* Step 3: Attach to the target (flat session)                        */
/* ------------------------------------------------------------------ */
static void on_attached(cdp_session *cdp, json_t *result,
                        json_t *error, void *ud)
{
	cdp_page *page = (cdp_page *)ud;
	(void)cdp;

	page_cdp_rsp_done(page);
	CDP_PAGE_GUARD(page);

	if (error || !result) {
		const char *msg = error
		    ? json_string_value(json_object_get(error, "message"))
		    : NULL;
		page_error(page, msg ? msg : "attachToTarget failed");
		return;
	}

	const char *sid = json_string_value(json_object_get(result, "sessionId"));
	if (!sid) { page_error(page, "no sessionId"); return; }

	page->session_id = strdup(sid);
	page->state = PAGE_STATE_ENABLE_DOMAINS;
	step_enable_domains(page);
}

static void step_attach(cdp_page *page)
{
	page->state = PAGE_STATE_ATTACH;
	json_t *params = json_object();
	json_object_set_new(params, "targetId", json_string(page->target_id));
	json_object_set_new(params, "flatten",  json_true());
	page_cdp_send(page, page->cdp, NULL, "Target.attachToTarget",
	              params, on_attached, page);
	json_decref(params);
}

/* ------------------------------------------------------------------ */
/* Step 4: Enable domains + configure page                            */
/* ------------------------------------------------------------------ */

/* Per-page pending counter.  Each of the 3 "enable" responses
   decrements it; when it reaches 0 we proceed to navigation. */
typedef struct domain_cnt {
	cdp_page *page;
	int       remaining;
} domain_cnt;

static void on_domain_enabled(cdp_session *cdp, json_t *result,
                               json_t *error, void *ud)
{
	domain_cnt *dc = (domain_cnt *)ud;
	cdp_page *page;
	(void)cdp; (void)result; (void)error;

	page = dc ? dc->page : NULL;
	page_cdp_rsp_done(page);

	if (!dc)
		return;

	dc->remaining--;
	if (dc->remaining > 0)
		return;

	free(dc);
	CDP_PAGE_GUARD(page);
	page->state = PAGE_STATE_NAVIGATE;
	step_navigate(page);
}

static void step_enable_domains(cdp_page *page)
{
	page->state = PAGE_STATE_ENABLE_DOMAINS;
	const char *sid = page->session_id;

	/* Enable: Page, Network, Runtime, Performance — 4 async responses */
	domain_cnt *dc = calloc(1, sizeof(*dc));
	if (!dc) { page_error(page, "OOM in step_enable_domains"); return; }
	dc->page      = page;
	dc->remaining = 4;

	page_cdp_send(page, page->cdp, sid, "Page.enable",
	              NULL, on_domain_enabled, dc);
	page_cdp_send(page, page->cdp, sid, "Network.enable",
	              NULL, on_domain_enabled, dc);
	page_cdp_send(page, page->cdp, sid, "Runtime.enable",
	              NULL, on_domain_enabled, dc);
	page_cdp_send(page, page->cdp, sid, "Performance.enable",
	              NULL, on_domain_enabled, dc);

	/* Ancillary setup — fire and forget (no callback needed) */
	json_t *p;

	p = json_object();
	json_object_set_new(p, "cacheDisabled", json_true());
	cdp_send(page->cdp, sid, "Network.setCacheDisabled", p, NULL, NULL);
	json_decref(p);

	p = json_object();
	json_object_set_new(p, "width",             json_integer(640));
	json_object_set_new(p, "height",            json_integer(360));
	json_object_set_new(p, "deviceScaleFactor", json_real(1.0));
	json_object_set_new(p, "mobile",            json_false());
	cdp_send(page->cdp, sid, "Emulation.setDeviceMetricsOverride", p, NULL, NULL);
	json_decref(p);

	/* Optional: extra HTTP headers */
	json_t *hdrs = json_object_get(page->config, "headers");
	if (hdrs && json_is_object(hdrs)) {
		p = json_object();
		json_object_set(p, "headers", hdrs);
		cdp_send(page->cdp, sid, "Network.setExtraHTTPHeaders", p, NULL, NULL);
		json_decref(p);
	}
	json_t *env = json_object_get(page->config, "env");
	if (env && json_is_object(env)) {
		p = json_object();
		json_object_set(p, "headers", env);
		cdp_send(page->cdp, sid, "Network.setExtraHTTPHeaders", p, NULL, NULL);
		json_decref(p);
	}
}

/* ------------------------------------------------------------------ */
/* Metric family registration (HELP + TYPE lines in Prometheus output)*/
/* ------------------------------------------------------------------ */
static void register_metric_families(cdp_page *page)
{
	context_arg *c = page->carg;

	/* ---- Navigation / lifecycle ---------------------------------- */
	namespace_metric_family_set(NULL, c, "chromecdp_info",
	    METRIC_TYPE_GAUGE, "1 when a page crawl was initiated for this resource.");
	namespace_metric_family_set(NULL, c, "chromecdp_navigation_errors_total",
	    METRIC_TYPE_COUNTER, "Navigation-level fetch errors with error label.");

	/* ---- Performance.getMetrics — timestamps (Chrome monotonic clock) */
	namespace_metric_family_set(NULL, c, "chromecdp_timestamp_seconds",
	    METRIC_TYPE_GAUGE, "Chrome monotonic timestamp at metric collection time.");
	namespace_metric_family_set(NULL, c, "chromecdp_navigation_start_seconds",
	    METRIC_TYPE_GAUGE, "Chrome monotonic timestamp of the navigation start event.");
	namespace_metric_family_set(NULL, c, "chromecdp_first_meaningful_paint_seconds",
	    METRIC_TYPE_GAUGE, "Chrome monotonic timestamp of First Meaningful Paint.");
	namespace_metric_family_set(NULL, c, "chromecdp_dom_content_loaded_seconds",
	    METRIC_TYPE_GAUGE, "Chrome monotonic timestamp of the DOMContentLoaded event.");

	/* ---- Performance.getMetrics — DOM / JS gauges ---------------- */
	namespace_metric_family_set(NULL, c, "chromecdp_documents",
	    METRIC_TYPE_GAUGE, "Number of document objects in the frame tree.");
	namespace_metric_family_set(NULL, c, "chromecdp_frames",
	    METRIC_TYPE_GAUGE, "Number of frame elements in the page.");
	namespace_metric_family_set(NULL, c, "chromecdp_js_event_listeners",
	    METRIC_TYPE_GAUGE, "Total number of registered JavaScript event listeners.");
	namespace_metric_family_set(NULL, c, "chromecdp_nodes",
	    METRIC_TYPE_GAUGE, "Total number of DOM nodes in the document.");
	namespace_metric_family_set(NULL, c, "chromecdp_js_heap_used_bytes",
	    METRIC_TYPE_GAUGE, "Used JavaScript heap size in bytes.");
	namespace_metric_family_set(NULL, c, "chromecdp_js_heap_total_bytes",
	    METRIC_TYPE_GAUGE, "Total allocated JavaScript heap size in bytes.");
	namespace_metric_family_set(NULL, c, "chromecdp_audio_handlers",
	    METRIC_TYPE_GAUGE, "Number of live Web Audio API nodes.");
	namespace_metric_family_set(NULL, c, "chromecdp_audio_worklet_processors",
	    METRIC_TYPE_GAUGE, "Number of active AudioWorkletProcessor instances.");
	namespace_metric_family_set(NULL, c, "chromecdp_array_buffer_bytes",
	    METRIC_TYPE_GAUGE, "Bytes held by ArrayBuffer objects.");
	namespace_metric_family_set(NULL, c, "chromecdp_ad_subframes",
	    METRIC_TYPE_GAUGE, "Number of ad-tagged subframes.");
	namespace_metric_family_set(NULL, c, "chromecdp_context_lifecycle_observers",
	    METRIC_TYPE_GAUGE, "Number of context lifecycle state observers.");
	namespace_metric_family_set(NULL, c, "chromecdp_detached_script_states",
	    METRIC_TYPE_GAUGE, "Number of detached script execution contexts.");
	namespace_metric_family_set(NULL, c, "chromecdp_media_key_sessions",
	    METRIC_TYPE_GAUGE, "Number of active MediaKeySession objects.");
	namespace_metric_family_set(NULL, c, "chromecdp_media_keys",
	    METRIC_TYPE_GAUGE, "Number of active MediaKeys objects.");
	namespace_metric_family_set(NULL, c, "chromecdp_rtc_peer_connections",
	    METRIC_TYPE_GAUGE, "Number of active RTCPeerConnection objects.");
	namespace_metric_family_set(NULL, c, "chromecdp_resource_fetchers",
	    METRIC_TYPE_GAUGE, "Number of ResourceFetcher instances.");
	namespace_metric_family_set(NULL, c, "chromecdp_resources",
	    METRIC_TYPE_GAUGE, "Number of cached Resource objects.");
	namespace_metric_family_set(NULL, c, "chromecdp_ua_css_resources",
	    METRIC_TYPE_GAUGE, "Number of UA CSS resources.");
	namespace_metric_family_set(NULL, c, "chromecdp_v8_per_context_datas",
	    METRIC_TYPE_GAUGE, "Number of V8 per-context data objects.");
	namespace_metric_family_set(NULL, c, "chromecdp_worker_global_scopes",
	    METRIC_TYPE_GAUGE, "Number of active WorkerGlobalScope instances.");

	/* ---- Performance.getMetrics — layout/script counters --------- */
	namespace_metric_family_set(NULL, c, "chromecdp_layouts_total",
	    METRIC_TYPE_COUNTER, "Total number of full or partial page layout operations.");
	namespace_metric_family_set(NULL, c, "chromecdp_style_recalcs_total",
	    METRIC_TYPE_COUNTER, "Total number of page style recalculation operations.");

	/* ---- Performance.getMetrics — duration counters (seconds) ---- */
	namespace_metric_family_set(NULL, c, "chromecdp_layout_duration_seconds_total",
	    METRIC_TYPE_COUNTER, "Combined duration of all layout operations in seconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_style_recalc_duration_seconds_total",
	    METRIC_TYPE_COUNTER, "Combined duration of all style recalculation operations in seconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_script_duration_seconds_total",
	    METRIC_TYPE_COUNTER, "Combined duration of all JavaScript execution in seconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_v8_compile_duration_seconds_total",
	    METRIC_TYPE_COUNTER, "Combined duration of V8 per-context code compilation in seconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_task_duration_seconds_total",
	    METRIC_TYPE_COUNTER, "Combined duration of all renderer tasks in seconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_task_other_duration_seconds_total",
	    METRIC_TYPE_COUNTER, "Renderer task duration not attributed to other categories in seconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_thread_time_seconds_total",
	    METRIC_TYPE_COUNTER, "Total CPU time consumed by the renderer thread in seconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_process_time_seconds_total",
	    METRIC_TYPE_COUNTER, "Total CPU time consumed by the renderer process in seconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_devtools_command_duration_seconds_total",
	    METRIC_TYPE_COUNTER, "Time spent processing DevTools commands in the renderer in seconds.");

	/* ---- Network domain — per-resource metrics ------------------- */
	namespace_metric_family_set(NULL, c, "chromecdp_resource_http_status",
	    METRIC_TYPE_GAUGE, "HTTP response status code for each sub-resource loaded by the page.");
	namespace_metric_family_set(NULL, c, "chromecdp_resource_duration_milliseconds",
	    METRIC_TYPE_GAUGE, "Time from request start to loading complete for each sub-resource in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_resource_size_bytes",
	    METRIC_TYPE_GAUGE, "Total bytes transferred (headers + body) for each sub-resource.");
	namespace_metric_family_set(NULL, c, "chromecdp_resource_failures_total",
	    METRIC_TYPE_COUNTER, "Count of sub-requests that failed to load.");
	namespace_metric_family_set(NULL, c, "chromecdp_console_messages_total",
	    METRIC_TYPE_COUNTER, "Browser console messages emitted during page load (requires console_events: true).");
	namespace_metric_family_set(NULL, c, "chromecdp_page_errors_total",
	    METRIC_TYPE_COUNTER, "Uncaught JavaScript exceptions thrown during page load.");

	/* ---- Aggregate page weight ----------------------------------- */
	namespace_metric_family_set(NULL, c, "chromecdp_page_size_bytes",
	    METRIC_TYPE_GAUGE, "Total transfer size of all same-origin resources loaded by the page in bytes.");

	/* ---- Resource Timing API (window.performance.getEntries()) --- */
	namespace_metric_family_set(NULL, c, "chromecdp_rt_start_milliseconds",
	    METRIC_TYPE_GAUGE, "PerformanceEntry startTime relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_duration_milliseconds",
	    METRIC_TYPE_GAUGE, "PerformanceEntry total duration in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_worker_start_milliseconds",
	    METRIC_TYPE_GAUGE, "Time Service Worker started relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_redirect_start_milliseconds",
	    METRIC_TYPE_GAUGE, "Time of first redirect start relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_redirect_end_milliseconds",
	    METRIC_TYPE_GAUGE, "Time of last redirect end relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_fetch_start_milliseconds",
	    METRIC_TYPE_GAUGE, "Time fetch started relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_dns_start_milliseconds",
	    METRIC_TYPE_GAUGE, "Time DNS lookup started relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_dns_end_milliseconds",
	    METRIC_TYPE_GAUGE, "Time DNS lookup completed relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_ssl_start_milliseconds",
	    METRIC_TYPE_GAUGE, "Time TLS handshake started relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_connect_start_milliseconds",
	    METRIC_TYPE_GAUGE, "Time TCP connection started relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_connect_end_milliseconds",
	    METRIC_TYPE_GAUGE, "Time TCP connection completed relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_request_start_milliseconds",
	    METRIC_TYPE_GAUGE, "Time request was sent relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_response_start_milliseconds",
	    METRIC_TYPE_GAUGE, "Time to first byte received relative to navigationStart in milliseconds (TTFB).");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_response_end_milliseconds",
	    METRIC_TYPE_GAUGE, "Time response fully received relative to navigationStart in milliseconds.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_transfer_bytes",
	    METRIC_TYPE_GAUGE, "Bytes transferred over the network for this resource.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_encoded_body_bytes",
	    METRIC_TYPE_GAUGE, "Compressed response body size in bytes.");
	namespace_metric_family_set(NULL, c, "chromecdp_rt_decoded_body_bytes",
	    METRIC_TYPE_GAUGE, "Uncompressed response body size in bytes.");
}

/* ------------------------------------------------------------------ */
/* Step 5: Navigate                                                   */
/* ------------------------------------------------------------------ */
static void on_navigated(cdp_session *cdp, json_t *result,
                          json_t *error, void *ud)
{
	cdp_page *page = (cdp_page *)ud;
	(void)cdp; (void)result;

	page_cdp_rsp_done(page);
	CDP_PAGE_GUARD(page);

	if (error) {
		const char *msg = json_string_value(json_object_get(error, "message"));
		if (!msg) msg = "navigation error";
		int64_t one = 1;
		metric_add_labels2("chromecdp_navigation_errors_total",
		    &one, DATATYPE_INT, page->carg,
		    "resource", page->url,
		    "error",    (char *)msg);
	}

	/* Start networkidle2 wait regardless of navigation result */
	page->state = PAGE_STATE_WAIT_IDLE;
	step_wait_idle(page);
}

static void step_navigate(cdp_page *page)
{
	/* Register HELP/TYPE metadata once per crawl (idempotent) */
	register_metric_families(page);

	/* Emit Info=1 at navigation start */
	double one = 1.0;
	emit_double(page, "chromecdp_info", "resource", page->url, one);

	page->state = PAGE_STATE_NAVIGATE;
	json_t *params = json_object();
	json_object_set_new(params, "url", json_string(page->url));

	const char *post_data = json_string_value(
	    json_object_get(page->config, "post_data"));
	if (post_data) {
		/* Intercept all requests and override with POST */
		json_t *p = json_object();
		json_object_set_new(p, "patterns", json_array());
		cdp_send(page->cdp, page->session_id, "Fetch.enable", p, NULL, NULL);
		json_decref(p);
		/* NOTE: actual POST interception uses Fetch.requestPaused events.
		   For now, send as GET (full POST implementation requires Fetch domain
		   event handling — see chromecdp_event_dispatch in chromecdp.c). */
	}

	page_cdp_send(page, page->cdp, page->session_id, "Page.navigate",
	              params, on_navigated, page);
	json_decref(params);
}

/* ------------------------------------------------------------------ */
/* Step 6: networkidle2 wait                                          */
/* ------------------------------------------------------------------ */
static void step_wait_idle(cdp_page *page)
{
	page->state       = PAGE_STATE_WAIT_IDLE;
	page->idle_ticks  = 0;

	uint64_t timeout_ms = chromecdp_config_timeout_ms(page->config);

	page->idle_timer = malloc(sizeof(uv_timer_t));
	if (!page->idle_timer) {
		step_get_perf_metrics(page);
		return;
	}
	page->idle_timer->data = page;
	uv_timer_init(page->loop, page->idle_timer);
	uv_timer_start(page->idle_timer, idle_tick,
	               CHROMECDP_IDLE_INTERVAL_MS,
	               CHROMECDP_IDLE_INTERVAL_MS);

	page->nav_timeout_timer = malloc(sizeof(uv_timer_t));
	if (page->nav_timeout_timer) {
		page->nav_timeout_timer->data = page;
		uv_timer_init(page->loop, page->nav_timeout_timer);
		uv_timer_start(page->nav_timeout_timer, nav_timeout_cb,
		               timeout_ms, 0);
	}
}

/* ------------------------------------------------------------------ */
/* Step 7: Performance.getMetrics                                     */
/* ------------------------------------------------------------------ */
static void on_perf_metrics(cdp_session *cdp, json_t *result,
                             json_t *error, void *ud)
{
	cdp_page *page = (cdp_page *)ud;
	(void)cdp; (void)error;

	page_cdp_rsp_done(page);
	CDP_PAGE_GUARD(page);

	/* Map Chrome Performance.getMetrics names → Prometheus metric names */
	static const struct { const char *chrome; const char *prom; } perf_map[] = {
		{ "Timestamp",               "chromecdp_timestamp_seconds"                  },
		{ "NavigationStart",         "chromecdp_navigation_start_seconds"           },
		{ "FirstMeaningfulPaint",    "chromecdp_first_meaningful_paint_seconds"     },
		{ "DomContentLoaded",        "chromecdp_dom_content_loaded_seconds"         },
		{ "Documents",               "chromecdp_documents"                          },
		{ "Frames",                  "chromecdp_frames"                             },
		{ "JSEventListeners",        "chromecdp_js_event_listeners"                 },
		{ "Nodes",                   "chromecdp_nodes"                              },
		{ "LayoutCount",             "chromecdp_layouts_total"                      },
		{ "RecalcStyleCount",        "chromecdp_style_recalcs_total"                },
		{ "LayoutDuration",          "chromecdp_layout_duration_seconds_total"      },
		{ "RecalcStyleDuration",     "chromecdp_style_recalc_duration_seconds_total"},
		{ "ScriptDuration",          "chromecdp_script_duration_seconds_total"      },
		{ "V8CompileDuration",       "chromecdp_v8_compile_duration_seconds_total"  },
		{ "TaskDuration",            "chromecdp_task_duration_seconds_total"        },
		{ "TaskOtherDuration",       "chromecdp_task_other_duration_seconds_total"  },
		{ "ThreadTime",              "chromecdp_thread_time_seconds_total"          },
		{ "ProcessTime",             "chromecdp_process_time_seconds_total"         },
		{ "JSHeapUsedSize",          "chromecdp_js_heap_used_bytes"                 },
		{ "JSHeapTotalSize",         "chromecdp_js_heap_total_bytes"                },
		{ "DevToolsCommandDuration", "chromecdp_devtools_command_duration_seconds_total" },
		{ "AudioHandlers",           "chromecdp_audio_handlers"                     },
		{ "AudioWorkletProcessors",  "chromecdp_audio_worklet_processors"           },
		{ "ArrayBufferContents",     "chromecdp_array_buffer_bytes"                 },
		{ "AdSubframes",             "chromecdp_ad_subframes"                       },
		{ "ContextLifecycleStateObservers", "chromecdp_context_lifecycle_observers" },
		{ "DetachedScriptStates",    "chromecdp_detached_script_states"             },
		{ "MediaKeySessions",        "chromecdp_media_key_sessions"                 },
		{ "MediaKeys",               "chromecdp_media_keys"                         },
		{ "RTCPeerConnections",      "chromecdp_rtc_peer_connections"               },
		{ "ResourceFetchers",        "chromecdp_resource_fetchers"                  },
		{ "Resources",               "chromecdp_resources"                          },
		{ "UACSSResources",          "chromecdp_ua_css_resources"                   },
		{ "V8PerContextDatas",       "chromecdp_v8_per_context_datas"               },
		{ "WorkerGlobalScopes",      "chromecdp_worker_global_scopes"               },
		{ NULL, NULL }
	};

	if (result) {
		json_t *metrics = json_object_get(result, "metrics");
		size_t idx;
		json_t *entry;
		json_array_foreach(metrics, idx, entry) {
			const char *chrome_name = json_string_value(json_object_get(entry, "name"));
			double       val        = json_num(json_object_get(entry, "value"));
			if (!chrome_name) continue;

			/* Look up mapped name; fall back to chromecdp_<chrome_name> */
			const char *prom_name = NULL;
			for (int m = 0; perf_map[m].chrome; m++) {
				if (strcmp(perf_map[m].chrome, chrome_name) == 0) {
					prom_name = perf_map[m].prom;
					break;
				}
			}
			if (prom_name) {
				emit_double(page, prom_name, "resource", page->url, val);
			} else {
				/* Unknown future Chrome metric — emit with raw name */
				char fallback[128];
				snprintf(fallback, sizeof(fallback), "chromecdp_%s", chrome_name);
				emit_double(page, fallback, "resource", page->url, val);
			}
		}
	}

	page->state = PAGE_STATE_EVAL_ENTRIES;
	step_eval_entries(page);
}

static void step_get_perf_metrics(cdp_page *page)
{
	page->state = PAGE_STATE_GET_PERF;
	page_cdp_send(page, page->cdp, page->session_id,
	              "Performance.getMetrics", NULL, on_perf_metrics, page);
}

/* ------------------------------------------------------------------ */
/* Step 8: Runtime.evaluate — window.performance.getEntries()        */
/* ------------------------------------------------------------------ */

/* Maps JS PerformanceEntry property → Prometheus metric name */
static const struct { const char *js_key; const char *prom_name; } RT_METRICS[] = {
	{ "startTime",             "chromecdp_rt_start_milliseconds"         },
	{ "duration",              "chromecdp_rt_duration_milliseconds"      },
	{ "workerStart",           "chromecdp_rt_worker_start_milliseconds"  },
	{ "redirectStart",         "chromecdp_rt_redirect_start_milliseconds"},
	{ "redirectEnd",           "chromecdp_rt_redirect_end_milliseconds"  },
	{ "fetchStart",            "chromecdp_rt_fetch_start_milliseconds"   },
	{ "domainLookupStart",     "chromecdp_rt_dns_start_milliseconds"     },
	{ "domainLookupEnd",       "chromecdp_rt_dns_end_milliseconds"       },
	{ "secureConnectionStart", "chromecdp_rt_ssl_start_milliseconds"     },
	{ "connectStart",          "chromecdp_rt_connect_start_milliseconds" },
	{ "connectEnd",            "chromecdp_rt_connect_end_milliseconds"   },
	{ "requestStart",          "chromecdp_rt_request_start_milliseconds" },
	{ "responseStart",         "chromecdp_rt_response_start_milliseconds"},
	{ "responseEnd",           "chromecdp_rt_response_end_milliseconds"  },
	{ "transferSize",          "chromecdp_rt_transfer_bytes"             },
	{ "encodedBodySize",       "chromecdp_rt_encoded_body_bytes"         },
	{ "decodedBodySize",       "chromecdp_rt_decoded_body_bytes"         },
	{ NULL, NULL }
};

static void on_eval_entries(cdp_session *cdp, json_t *result,
                             json_t *error, void *ud)
{
	cdp_page *page = (cdp_page *)ud;
	(void)cdp; (void)error;

	page_cdp_rsp_done(page);
	CDP_PAGE_GUARD(page);

	if (result) {
		json_t *ret = json_object_get(result, "result");
		const char *val_s = json_string_value(json_object_get(ret, "value"));
		if (val_s) {
			json_error_t jerr;
			json_t *entries = json_loads(val_s, 0, &jerr);
			if (entries && json_is_array(entries)) {
				double total_size = 0.0;
				size_t idx;
				json_t *entry;
				json_array_foreach(entries, idx, entry) {
					double transfer = json_real_value(
					    json_object_get(entry, "transferSize"));
					if (transfer <= 0) continue;
					total_size += transfer;

					const char *name  = json_string_value(json_object_get(entry, "name"));
					const char *etype = json_string_value(json_object_get(entry, "entryType"));
					const char *itype = json_string_value(json_object_get(entry, "initiatorType"));
					const char *proto = json_string_value(json_object_get(entry, "nextHopProtocol"));
					if (!name)  name  = "";
					if (!etype) etype = "";
					if (!itype) itype = "";
					if (!proto) proto = "";

					char trunc_name[129]; /* 128 + NUL */
					strncpy(trunc_name, name, 128);
					trunc_name[128] = '\0';

					for (int m = 0; RT_METRICS[m].js_key; m++) {
						double mval = json_num(
						    json_object_get(entry, RT_METRICS[m].js_key));

						metric_add_labels5((char *)RT_METRICS[m].prom_name,
						    &mval, DATATYPE_DOUBLE, page->carg,
						    "resource",        page->url,
						    "source",          trunc_name,
						    "entryType",       (char *)etype,
						    "initiatorType",   (char *)itype,
						    "nextHopProtocol", (char *)proto);
					}
				}
				emit_double(page, "chromecdp_page_size_bytes", "resource", page->url, total_size);
				json_decref(entries);
			}
		}
	}

	page->state = PAGE_STATE_SCREENSHOT;
	step_screenshot(page);
}

static void step_eval_entries(cdp_page *page)
{
	page->state = PAGE_STATE_EVAL_ENTRIES;
	json_t *params = json_object();
	json_object_set_new(params, "expression",
	    json_string("JSON.stringify(performance.getEntries())"));
	json_object_set_new(params, "returnByValue", json_true());
	page_cdp_send(page, page->cdp, page->session_id,
	              "Runtime.evaluate", params, on_eval_entries, page);
	json_decref(params);
}

/* ------------------------------------------------------------------ */
/* Step 9: Optional screenshot                                        */
/* ------------------------------------------------------------------ */
static void on_screenshot(cdp_session *cdp, json_t *result,
                           json_t *error, void *ud)
{
	cdp_page *page = (cdp_page *)ud;
	(void)cdp; (void)error;

	page_cdp_rsp_done(page);
	CDP_PAGE_GUARD(page);

	if (result) {
		const char *b64 = json_string_value(json_object_get(result, "data"));
		if (b64) {
			json_t *ss_cfg   = json_object_get(page->config, "screenshot");
			const char *dir  = json_string_value(json_object_get(ss_cfg, "dir"));
			const char *type = json_string_value(json_object_get(ss_cfg, "type"));
			if (!dir)  dir  = "/var/lib/alligator/";
			if (!type) type = "png";

			/* Sanitise URL for filename */
			char safe_url[256];
			strncpy(safe_url, page->url, sizeof(safe_url) - 1);
			safe_url[sizeof(safe_url) - 1] = '\0';
			for (char *p = safe_url; *p; p++)
				if (*p == '/' || *p == ':' || *p == '?' || *p == '&') *p = '-';

			/* ISO timestamp */
			time_t now = time(NULL);
			struct tm *tm_info = gmtime(&now);
			char ts[32];
			strftime(ts, sizeof(ts), "%Y-%m-%dT%H-%M-%SZ", tm_info);

			char path[512];
			snprintf(path, sizeof(path), "%s/%s-%s.%s", dir, safe_url, ts, type);

			size_t outlen = 0;
			char *png = base64_decode(b64, strlen(b64), &outlen);
			if (png) {
				mkdirp((char *)dir);
				/* write_to_file takes ownership of png */
				write_to_file(path, png, outlen, NULL, NULL);
			}
		}
	}

	step_close_target(page);
}

static void step_screenshot(cdp_page *page)
{
	page->state = PAGE_STATE_SCREENSHOT;
	json_t *ss_cfg = json_object_get(page->config, "screenshot");
	if (!ss_cfg) {
		step_close_target(page);
		return;
	}

	int min_code = (int)json_integer_value(
	    json_object_get(ss_cfg, "minimum_code"));
	if (min_code > 0 && page->resp_code < min_code) {
		step_close_target(page);
		return;
	}

	json_t *params = json_object();
	json_object_set_new(params, "format", json_string("png"));
	int full_page = json_is_true(json_object_get(ss_cfg, "fullPage"));
	json_object_set_new(params, "captureBeyondViewport", full_page ? json_true() : json_false());

	page_cdp_send(page, page->cdp, page->session_id,
	              "Page.captureScreenshot", params, on_screenshot, page);
	json_decref(params);
}

/* ------------------------------------------------------------------ */
/* Step 10: Close target                                              */
/* ------------------------------------------------------------------ */
static void on_target_closed(cdp_session *cdp, json_t *result,
                              json_t *error, void *ud)
{
	cdp_page *page = (cdp_page *)ud;
	(void)cdp; (void)result; (void)error;

	page_cdp_rsp_done(page);
	CDP_PAGE_GUARD(page);
	step_dispose_context(page);
}

static void step_close_target(cdp_page *page)
{
	page->state = PAGE_STATE_CLOSE_TARGET;
	json_t *params = json_object();
	json_object_set_new(params, "targetId", json_string(page->target_id));
	page_cdp_send(page, page->cdp, NULL, "Target.closeTarget",
	              params, on_target_closed, page);
	json_decref(params);
}

/* ------------------------------------------------------------------ */
/* Step 11: Dispose browser context                                   */
/* ------------------------------------------------------------------ */
static void on_context_disposed(cdp_session *cdp, json_t *result,
                                 json_t *error, void *ud)
{
	cdp_page *page = (cdp_page *)ud;
	(void)cdp; (void)result; (void)error;

	page_cdp_rsp_done(page);
	CDP_PAGE_GUARD(page);
	page->state = PAGE_STATE_DONE;
	page_try_finish(page);
}

static void step_dispose_context(cdp_page *page)
{
	page->state = PAGE_STATE_DISPOSE_CTX;
	json_t *params = json_object();
	json_object_set_new(params, "browserContextId", json_string(page->context_id));
	page_cdp_send(page, page->cdp, NULL, "Target.disposeBrowserContext",
	              params, on_context_disposed, page);
	json_decref(params);
}

/* ------------------------------------------------------------------ */
/* Terminal states                                                     */
/* ------------------------------------------------------------------ */
static void page_try_finish(cdp_page *page)
{
	if (!page || page->magic != PAGE_MAGIC || page->finished)
		return;

	page->finished = 1;
	cdp_page_stop_timers(page);

	if (page->cdp_inflight > 0)
		return;

	page_emit_done(page);
}

static void page_error(cdp_page *page, const char *reason)
{
	CDP_PAGE_GUARD(page);

	if (page->carg && page->carg->log_level > 0)
		glog(L_WARN, "chromecdp: page error for %s: %s\n", page->url, reason);
	page->state = PAGE_STATE_ERROR;

	/* Best-effort cleanup — only while CDP is still connected */
	if (page->cdp && page->cdp->ws && page->cdp->ws->state == WS_STATE_OPEN &&
	    page->target_id) {
		json_t *params = json_object();
		json_object_set_new(params, "targetId", json_string(page->target_id));
		cdp_send(page->cdp, NULL, "Target.closeTarget", params, NULL, NULL);
		json_decref(params);
	}
	if (page->cdp && page->cdp->ws && page->cdp->ws->state == WS_STATE_OPEN &&
	    page->context_id) {
		json_t *params = json_object();
		json_object_set_new(params, "browserContextId",
		                    json_string(page->context_id));
		cdp_send(page->cdp, NULL, "Target.disposeBrowserContext", params, NULL, NULL);
		json_decref(params);
	}
	page_try_finish(page);
}

void cdp_page_abort(cdp_page *page, const char *reason)
{
	page_error(page, reason);
}

/* ------------------------------------------------------------------ */
/* Idle timer close callback                                          */
/* ------------------------------------------------------------------ */
static void idle_timer_closed(uv_handle_t *handle)
{
	free(handle);
}

/* ------------------------------------------------------------------ */
/* Public: start page navigation                                      */
/* ------------------------------------------------------------------ */
cdp_page *cdp_page_start(cdp_session *cdp,
                          uv_loop_t *loop,
                          const char *url,
                          json_t *config,
                          context_arg *carg,
                          page_done_cb done_cb,
                          void *done_userdata)
{
	cdp_page *page = calloc(1, sizeof(*page));
	if (!page) return NULL;

	page->cdp          = cdp;
	page->loop         = loop;
	page->url          = strdup(url);
	page->config       = config; /* borrowed */
	page->carg         = carg;   /* borrowed */
	page->done_cb      = done_cb;
	page->done_userdata = done_userdata;
	page->state        = PAGE_STATE_IDLE;
	page->magic        = PAGE_MAGIC;

	step_create_context(page);
	return page;
}

/* ------------------------------------------------------------------ */
/* Public: free page                                                   */
/* ------------------------------------------------------------------ */
void cdp_page_free(cdp_page *page)
{
	if (!page) return;

	page->magic = 0;
	cdp_page_stop_timers(page);

	/* Free any in-flight request tracking entries */
	req_track *rt = page->req_map;
	while (rt) {
		req_track *nxt = rt->next;
		free(rt);
		rt = nxt;
	}

	free(page->url);
	free(page->context_id);
	free(page->target_id);
	free(page->session_id);
	if (page->config)
		json_decref(page->config);
	free(page);
}
