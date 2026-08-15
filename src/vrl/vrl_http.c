/*
 * Alligator host builtin: http_request(url) -> { body, status } | null
 *
 * Same async suspend/resume model as dns_lookup (see vrl/type.h, vrl/vrl.c):
 *   - Cache HIT  → return { "body": "...", "status": <int> } immediately.
 *   - Cache MISS → aggregator_oneshot GET, pause stream, return null; resume
 *     timer replays the record once the body lands (or after timeout → null).
 *
 * Vector-shaped return (subset): object with string "body" and integer "status".
 * On fetch failure/timeout the next call yields null (VRL should treat as fail).
 */

#define _GNU_SOURCE
#include "vrl/type.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/url.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "main.h"
#include "parsers/multiparser.h"
#include "resolver/resolver.h"
#include "resolver/dns.h"
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern aconf *ac;

#define VRL_HTTP_URL_MAX 2048
#define VRL_HTTP_DEFAULT_TIMEOUT_MS 10000
#define VRL_HTTP_CACHE_MAX 4096

/* ---------------- request timing metrics ----------------
 *
 * Emitted against ac->system_carg (originating carg may be freed before the
 * oneshot completes):
 *   vrl_http_requests_total{result=success|failure|timeout, method=GET}
 *   vrl_http_request_duration_seconds_sum{result=..., method=GET}
 * Average latency = duration_sum / requests_total for a given result.
 */
void vrl_http_metric(const char *result, uint64_t start_ms, uint64_t now_ms)
{
	if (!ac || !ac->system_carg)
		return;
	if (!result)
		result = "unknown";

	uint64_t one = 1;
	metric_update_labels2("vrl_http_requests_total", &one, DATATYPE_UINT,
			      ac->system_carg, "result", (char *)result, "method", "GET");

	double dur = (now_ms >= start_ms) ? (double)(now_ms - start_ms) / 1000.0 : 0.0;
	metric_update_labels2("vrl_http_request_duration_seconds_sum", &dur, DATATYPE_DOUBLE,
			      ac->system_carg, "result", (char *)result, "method", "GET");
}
typedef struct vrl_http_entry {
	alligator_ht_node node;
	char *url;           /* owned */
	char *body;          /* owned; may be empty string */
	size_t body_len;
	int status;          /* HTTP status; 0 = transport failure / timeout sentinel */
	int ready;           /* 0 = in flight, 1 = complete */
	uint64_t start_ms;
} vrl_http_entry;

static alligator_ht *vrl_http_cache;
static uv_mutex_t vrl_http_lock;
static int vrl_http_ready;

static int vrl_http_compare(const void *arg, const void *obj)
{
	return strcmp((const char *)arg, ((const vrl_http_entry *)obj)->url);
}

static void vrl_http_cache_init(void)
{
	if (vrl_http_ready)
		return;
	vrl_http_cache = alligator_ht_init(NULL);
	uv_mutex_init(&vrl_http_lock);
	vrl_http_ready = 1;
}

static void vrl_http_entry_free(void *arg)
{
	vrl_http_entry *e = arg;
	if (!e)
		return;
	free(e->url);
	free(e->body);
	free(e);
}

typedef struct {
	vrl_http_entry *victim;
} vrl_http_evict_ctx;

static void vrl_http_evict_foreach(void *funcarg, void *arg)
{
	vrl_http_evict_ctx *ctx = funcarg;
	vrl_http_entry *e = arg;
	if (ctx->victim)
		return;
	if (e && e->ready)
		ctx->victim = e;
}

static void vrl_http_cache_maybe_evict_unlocked(void)
{
	if (!vrl_http_cache || alligator_ht_count(vrl_http_cache) < VRL_HTTP_CACHE_MAX)
		return;
	vrl_http_evict_ctx ctx = {0};
	alligator_ht_foreach_arg(vrl_http_cache, vrl_http_evict_foreach, &ctx);
	if (ctx.victim) {
		alligator_ht_remove_existing(vrl_http_cache, &ctx.victim->node);
		vrl_http_entry_free(ctx.victim);
	}
}

/* Returns owned clone of a ready entry, or NULL if missing / not ready. */
vrl_http_entry *vrl_http_cache_lookup_ready(const char *url)
{
	if (!vrl_http_ready || !url)
		return NULL;
	uint32_t h = tommy_strhash_u32(0, (char *)url);
	vrl_http_entry *out = NULL;

	uv_mutex_lock(&vrl_http_lock);
	vrl_http_entry *e = alligator_ht_search(vrl_http_cache, vrl_http_compare, url, h);
	if (e && e->ready) {
		out = calloc(1, sizeof(*out));
		if (out) {
			out->url = strdup(e->url);
			out->body = e->body_len ? strndup(e->body, e->body_len) : strdup("");
			out->body_len = e->body_len;
			out->status = e->status;
			out->ready = 1;
		}
	}
	uv_mutex_unlock(&vrl_http_lock);
	return out;
}

int vrl_http_cache_is_ready(const char *url)
{
	if (!vrl_http_ready || !url)
		return 0;
	uint32_t h = tommy_strhash_u32(0, (char *)url);
	int ready = 0;
	uv_mutex_lock(&vrl_http_lock);
	vrl_http_entry *e = alligator_ht_search(vrl_http_cache, vrl_http_compare, url, h);
	if (e)
		ready = e->ready;
	uv_mutex_unlock(&vrl_http_lock);
	return ready;
}

static void vrl_http_cache_store(const char *url, const char *body, size_t body_len, int status)
{
	if (!vrl_http_ready || !url)
		return;
	uint32_t h = tommy_strhash_u32(0, (char *)url);

	uv_mutex_lock(&vrl_http_lock);
	vrl_http_entry *e = alligator_ht_search(vrl_http_cache, vrl_http_compare, url, h);
	if (!e) {
		vrl_http_cache_maybe_evict_unlocked();
		e = calloc(1, sizeof(*e));
		if (!e) {
			uv_mutex_unlock(&vrl_http_lock);
			return;
		}
		e->url = strdup(url);
		alligator_ht_insert(vrl_http_cache, &e->node, e, h);
	} else {
		free(e->body);
		e->body = NULL;
		e->body_len = 0;
	}
	e->body = body_len ? strndup(body, body_len) : strdup("");
	e->body_len = e->body ? body_len : 0;
	e->status = status;
	e->ready = 1;
	uv_mutex_unlock(&vrl_http_lock);
}

/* Peek start_ms for an in-flight (or any) cache entry; 0 if missing. */
static uint64_t vrl_http_cache_start_ms(const char *url)
{
	if (!vrl_http_ready || !url)
		return 0;
	uint32_t h = tommy_strhash_u32(0, (char *)url);
	uint64_t start = 0;
	uv_mutex_lock(&vrl_http_lock);
	vrl_http_entry *e = alligator_ht_search(vrl_http_cache, vrl_http_compare, url, h);
	if (e)
		start = e->start_ms;
	uv_mutex_unlock(&vrl_http_lock);
	return start;
}

/* Mark URL as in-flight so concurrent requests do not spawn duplicate oneshots. */
static int vrl_http_cache_begin(const char *url, uint64_t start_ms)
{
	if (!vrl_http_ready || !url)
		return 0;
	uint32_t h = tommy_strhash_u32(0, (char *)url);
	int started = 0;

	uv_mutex_lock(&vrl_http_lock);
	vrl_http_entry *e = alligator_ht_search(vrl_http_cache, vrl_http_compare, url, h);
	if (e && !e->ready) {
		/* already in flight */
		uv_mutex_unlock(&vrl_http_lock);
		return 0;
	}
	if (e && e->ready) {
		uv_mutex_unlock(&vrl_http_lock);
		return 0;
	}
	vrl_http_cache_maybe_evict_unlocked();
	e = calloc(1, sizeof(*e));
	if (!e) {
		uv_mutex_unlock(&vrl_http_lock);
		return 0;
	}
	e->url = strdup(url);
	e->body = strdup("");
	e->body_len = 0;
	e->status = 0;
	e->ready = 0;
	e->start_ms = start_ms;
	alligator_ht_insert(vrl_http_cache, &e->node, e, h);
	started = 1;
	uv_mutex_unlock(&vrl_http_lock);
	return started;
}

void vrl_http_cache_force_ready_null(const char *url)
{
	uint64_t now = (ac && ac->loop) ? uv_now(ac->loop) : 0;
	uint64_t start = vrl_http_cache_start_ms(url);
	if (!start)
		start = now > VRL_HTTP_DEFAULT_TIMEOUT_MS ? now - VRL_HTTP_DEFAULT_TIMEOUT_MS : 0;
	vrl_http_metric("timeout", start, now);
	/* Timeout path: publish empty failure so resume can complete. */
	vrl_http_cache_store(url, "", 0, 0);
}

static void vrl_http_handler(char *body, size_t size, context_arg *carg)
{
	const char *url = carg && carg->data ? (const char *)carg->data : NULL;
	int status = carg ? (int)carg->last_http_code : 0;
	if (!url) {
		if (carg)
			carg->parser_status = 1;
		return;
	}
	carglog(carg, L_INFO, "vrl: http_request completed url='%s' status=%d body_len=%zu\n",
		url, status, size);

	uint64_t now = (ac && ac->loop) ? uv_now(ac->loop) : 0;
	uint64_t start = vrl_http_cache_start_ms(url);
	if (!start)
		start = now;
	const char *result = (status >= 200 && status < 400) ? "success" : "failure";
	vrl_http_metric(result, start, now);

	vrl_http_cache_store(url, body ? body : "", body ? size : 0, status ? status : 200);
	carg->parser_status = 1;
	/* data is owned strdup of url — free so carg_free does not leak */
	free(carg->data);
	carg->data = NULL;
}

static void vrl_http_kickoff(context_arg *parent, const char *url)
{
	(void)parent;
	if (!url || !url[0] || !ac || !ac->loop)
		return;

	uint64_t now = uv_now(ac->loop);
	if (!vrl_http_cache_begin(url, now)) {
		/* already pending or ready */
		return;
	}

	host_aggregator_info *hi = parse_url((char *)url, strlen(url));
	if (!hi) {
		vrl_http_cache_store(url, "", 0, 0);
		return;
	}

	/* tcp_client_connect() requires an A-record cache hit before connecting.
	 * Seed IP literals so oneshots to http://127.0.0.1:... do not stall on DNS. */
	if (hi->host) {
		struct in_addr a4;
		if (inet_pton(AF_INET, hi->host, &a4) == 1)
			dns_record_rule_push(hi->host, DNS_TYPE_A, NULL, 0,
					     hi->host, strlen(hi->host), 3600);
	}

	char *query = NULL;
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		query = gen_http_query(0, hi->query, "", hi->host, "alligator", hi->auth,
				       NULL, NULL, NULL, NULL);
	else {
		vrl_http_cache_store(url, "", 0, 0);
		url_free(hi);
		return;
	}

	char *url_copy = strdup(url);
	char *override_key = malloc(512);
	if (override_key)
		snprintf(override_key, 512, "vrl_http:%s", url);

	context_arg *shot = aggregator_oneshot(
		NULL, (char *)url, strlen(url), query, query ? strlen(query) : 0,
		vrl_http_handler, "vrl_http", NULL, override_key, 1 /* follow redirects */,
		url_copy, NULL, 0, NULL, NULL);

	if (!shot) {
		/* aggregator_oneshot already carg_free'd on smart_aggregator failure,
		 * which frees key (override_key) and mesg (query). data (url_copy) is
		 * NOT freed by carg_free — free it here. */
		glog(L_ERROR, "vrl: http_request oneshot failed for '%s'\n", url);
		free(url_copy);
		vrl_http_cache_store(url, "", 0, 0);
	} else {
		carglog(shot, L_INFO, "vrl: http_request oneshot started key='%s' host='%s' port='%s'\n",
			shot->key ? shot->key : "?", shot->host, shot->port);
		/* Do not wait for aggregator_startup crawl — connect immediately. */
		extern void for_tcp_client_connect(void *arg);
		for_tcp_client_connect(shot);
	}
	url_free(hi);
}

static vrl_value *vrl_http_result_object(vrl_http_entry *e)
{
	vrl_value *obj = vrl_object_new();
	vrl_object_set_cstr(obj, "body",
			    vrl_bytes(e->body ? e->body : "", e->body_len));
	vrl_object_set_cstr(obj, "status", vrl_integer(e->status));
	return obj;
}

static vrl_status fn_http_request(vrl_call_args *a, vrl_value **out, char **err)
{
	vrl_stream *st = (a && a->ctx) ? (vrl_stream *)a->ctx->host : NULL;

	if (!st || !st->carg) {
		*err = vrl_errf("http_request: not available outside an alligator vrl stream");
		return VRL_ERR;
	}

	vrl_value *arg = vrl_arg(a, NULL, 0);
	if (!arg || arg->type != VRL_BYTES || !arg->u.bytes.data) {
		*err = vrl_errf("http_request: expects a string URL argument");
		return VRL_ERR;
	}

	const char *url = arg->u.bytes.data;
	if (strlen(url) >= VRL_HTTP_URL_MAX) {
		*err = vrl_errf("http_request: URL too long");
		return VRL_ERR;
	}

	if (st->http_force_null && st->http_url[0] && !strcmp(url, st->http_url)) {
		st->http_force_null = 0;
		*out = vrl_null();
		return VRL_OK;
	}

	vrl_http_entry *hit = vrl_http_cache_lookup_ready(url);
	if (hit) {
		/* status 0 sentinel → treat as null (fetch failed) */
		if (hit->status == 0 && hit->body_len == 0) {
			vrl_http_entry_free(hit);
			*out = vrl_null();
			return VRL_OK;
		}
		*out = vrl_http_result_object(hit);
		vrl_http_entry_free(hit);
		return VRL_OK;
	}

	if (!st->dns_suspended) {
		st->dns_suspended = 1;
		st->await_http = 1;
		snprintf(st->http_url, sizeof(st->http_url), "%s", url);
		/* Prefer a longer deadline for HTTP than DNS. */
		if (st->dns_timeout_ms < VRL_HTTP_DEFAULT_TIMEOUT_MS)
			st->dns_timeout_ms = VRL_HTTP_DEFAULT_TIMEOUT_MS;
		vrl_http_kickoff(st->carg, url);
		carglog(st->carg, L_INFO, "vrl: http_request('%s') miss, fetching async (paused)\n",
			url);
	}

	*out = vrl_null();
	return VRL_OK;
}

void vrl_http_builtins_init(void)
{
	vrl_http_cache_init();
	vrl_register("http_request", fn_http_request);
}
