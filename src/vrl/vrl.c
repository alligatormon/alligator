#include "vrl/type.h"
#include "common/logs.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "resolver/resolver.h"
#include "metric/labels.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "main.h"
#include "json.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern aconf *ac;

/* ---------------- node registry ---------------- */

int vrl_node_compare(const void *arg, const void *obj)
{
	return strcmp((const char *)arg, ((const vrl_node *)obj)->name);
}

static void vrl_node_get_any_foreach(void *funcarg, void *arg)
{
	vrl_node **dst = funcarg;
	if (!*dst)
		*dst = arg;
}

vrl_node *vrl_node_get(char *name)
{
	if (!ac || !ac->vrl || !name)
		return NULL;
	return alligator_ht_search(ac->vrl, vrl_node_compare, name, tommy_strhash_u32(0, name));
}

vrl_node *vrl_node_get_any(void)
{
	vrl_node *vn = NULL;
	if (!ac || !ac->vrl)
		return NULL;
	alligator_ht_foreach_arg(ac->vrl, vrl_node_get_any_foreach, &vn);
	return vn;
}

void vrl_node_free(vrl_node *vn)
{
	if (!vn)
		return;
	if (vn->prog)
		vrl_program_free(vn->prog);
	free(vn->name);
	free(vn->key);
	free(vn->script);
	free(vn->program);
	free(vn->ml_start_pattern);
	free(vn->ml_condition_pattern);
	uv_mutex_destroy(&vn->lock);
	free(vn);
}

struct vrl_collect_ctx {
	vrl_node **buf;
	size_t n;
	size_t cap;
};

static void vrl_collect_ptr(void *funcarg, void *arg)
{
	struct vrl_collect_ctx *ctx = funcarg;
	if (ctx->n >= ctx->cap) {
		size_t ncap = ctx->cap ? ctx->cap * 2 : 8;
		vrl_node **nb = realloc(ctx->buf, ncap * sizeof(*nb));
		if (!nb)
			return;
		ctx->buf = nb;
		ctx->cap = ncap;
	}
	ctx->buf[ctx->n++] = arg;
}

int vrl_engine_init(void)
{
	if (!ac)
		return 0;
	if (!ac->vrl) {
		ac->vrl = calloc(1, sizeof(alligator_ht));
		alligator_ht_init(ac->vrl);
		vrl_stdlib_init();
		vrl_host_builtins_init();
	}
	return 1;
}

void vrl_engine_free(void)
{
	if (!ac || !ac->vrl)
		return;
	struct vrl_collect_ctx ctx = {NULL, 0, 0};
	alligator_ht_foreach_arg(ac->vrl, vrl_collect_ptr, &ctx);
	for (size_t i = 0; i < ctx.n; ++i) {
		alligator_ht_remove_existing(ac->vrl, &(ctx.buf[i]->node));
		vrl_node_free(ctx.buf[i]);
	}
	free(ctx.buf);
	alligator_ht_done(ac->vrl);
	free(ac->vrl);
	ac->vrl = NULL;
}

string *vrl_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, "", hi->host, "alligator",
							  hi->auth, NULL, env, proxy_settings, NULL));
	else if (hi->query)
		return string_init_alloc(hi->query, 0);
	else
		return NULL;
}

/* ---------------- metric export from transformed event ----------------
 *
 * Alligator extension (not part of Vector VRL): after the remapped event is
 * produced, alligator reads .metric / .metrics and exports Prometheus-style
 * series. Histogram observation via "buckets" is likewise alligator-only.
 */

static int metric_value_from_vrl(vrl_value *v, double *d_out, int64_t *i_out, int8_t *dtype)
{
	if (!v)
		return 0;
	if (v->type == VRL_INTEGER) {
		*i_out = v->u.integer;
		*dtype = DATATYPE_INT;
		return 1;
	}
	if (v->type == VRL_FLOAT) {
		*d_out = v->u.flt;
		*dtype = DATATYPE_DOUBLE;
		return 1;
	}
	if (v->type == VRL_BOOLEAN) {
		*i_out = v->u.boolean ? 1 : 0;
		*dtype = DATATYPE_INT;
		return 1;
	}
	return 0;
}

static alligator_ht *labels_from_vrl_object(vrl_value *labels)
{
	if (!labels || labels->type != VRL_OBJECT)
		return NULL;
	alligator_ht *ht = alligator_ht_init(NULL);
	for (size_t i = 0; i < labels->u.object.len; i++) {
		vrl_object_entry *e = &labels->u.object.entries[i];
		if (!e->val)
			continue;
		char *s = vrl_value_to_string(e->val, NULL);
		if (!s)
			continue;
		/* labels_hash_insert_nocache copies; free our temp */
		char *k = strndup(e->key, e->key_len);
		labels_hash_insert_nocache(ht, k, s);
		free(k);
		free(s);
	}
	return ht;
}

static int vrl_metric_wants_update(vrl_value *m)
{
	vrl_value *u = vrl_object_get(m, "update", 6);
	if (!u)
		return 0;
	if (u->type == VRL_BOOLEAN)
		return u->u.boolean != 0;
	if (u->type == VRL_INTEGER)
		return u->u.integer != 0;
	return 0;
}

static const char *vrl_metric_type_str(vrl_value *m)
{
	vrl_value *tv = vrl_object_get(m, "type", 4);
	if (tv && tv->type == VRL_BYTES && tv->u.bytes.data)
		return tv->u.bytes.data;
	return NULL;
}

static void vrl_maybe_set_prom_type(context_arg *carg, vrl_value *m, const char *name)
{
	const char *t = vrl_metric_type_str(m);
	if (t) {
		if (!strcmp(t, "histogram"))
			namespace_metric_family_set_prom_type(carg, name, METRIC_TYPE_HISTOGRAM);
		else if (!strcmp(t, "counter"))
			namespace_metric_family_set_prom_type(carg, name, METRIC_TYPE_COUNTER);
		else if (!strcmp(t, "gauge"))
			namespace_metric_family_set_prom_type(carg, name, METRIC_TYPE_GAUGE);
		else if (!strcmp(t, "summary"))
			namespace_metric_family_set_prom_type(carg, name, METRIC_TYPE_SUMMARY);
		return;
	}
	/* Prometheus histogram components → TYPE on base name */
	char base[256];
	if (prom_family_strip_histogram_suffix(name, base, sizeof(base)))
		namespace_metric_family_set_prom_type(carg, base, METRIC_TYPE_HISTOGRAM);
}

static int vrl_bucket_bound(vrl_value *v, double *out)
{
	if (!v || !out)
		return 0;
	if (v->type == VRL_FLOAT) {
		*out = v->u.flt;
		return 1;
	}
	if (v->type == VRL_INTEGER) {
		*out = (double)v->u.integer;
		return 1;
	}
	return 0;
}

/* Alligator extension (not Vector VRL): observe one sample into a Prometheus histogram. */
static int emit_histogram_observation(context_arg *carg, const char *name,
	double sample, alligator_ht *base_labels, vrl_value *buckets)
{
	if (!carg || !name || !buckets || buckets->type != VRL_ARRAY || !buckets->u.array.len)
		return 0;

	namespace_metric_family_set_prom_type(carg, name, METRIC_TYPE_HISTOGRAM);

	char metric_name[512];
	char le_buf[64];
	int64_t one = 1;

	snprintf(metric_name, sizeof(metric_name), "%s_bucket", name);
	for (size_t i = 0; i < buckets->u.array.len; i++) {
		double bound = 0;
		if (!vrl_bucket_bound(vrl_array_get(buckets, i), &bound)) {
			carglog(carg, L_INFO, "vrl: histogram '%s': buckets[%zu] not a number, skip\n",
				name, i);
			continue;
		}
		if (sample > bound)
			continue;
		snprintf(le_buf, sizeof(le_buf), "%g", bound);
		alligator_ht *lbl = base_labels ? labels_dup(base_labels) : alligator_ht_init(NULL);
		labels_hash_insert_nocache(lbl, "le", le_buf);
		metric_update(metric_name, lbl, &one, DATATYPE_INT, carg);
	}

	alligator_ht *inf = base_labels ? labels_dup(base_labels) : alligator_ht_init(NULL);
	labels_hash_insert_nocache(inf, "le", "+Inf");
	metric_update(metric_name, inf, &one, DATATYPE_INT, carg);

	snprintf(metric_name, sizeof(metric_name), "%s_sum", name);
	metric_update(metric_name, base_labels ? labels_dup(base_labels) : NULL,
		&sample, DATATYPE_DOUBLE, carg);

	snprintf(metric_name, sizeof(metric_name), "%s_count", name);
	metric_update(metric_name, base_labels ? labels_dup(base_labels) : NULL,
		&one, DATATYPE_INT, carg);

	carglog(carg, L_INFO, "vrl: histogram observe %s = %.17g (%zu buckets)\n",
		name, sample, buckets->u.array.len);
	if (base_labels)
		labels_hash_free(base_labels);
	return 1;
}

static void emit_one_metric(context_arg *carg, vrl_value *m)
{
	if (!m || m->type != VRL_OBJECT) {
		carglog(carg, L_DEBUG, "vrl: skip metric emit: not an object\n");
		return;
	}
	vrl_value *namev = vrl_object_get(m, "name", 4);
	vrl_value *valv = vrl_object_get(m, "value", 5);
	if (!namev || namev->type != VRL_BYTES || !namev->u.bytes.data) {
		carglog(carg, L_INFO, "vrl: skip metric emit: missing string .name\n");
		return;
	}
	double d = 0;
	int64_t i = 0;
	int8_t dtype = DATATYPE_NONE;
	if (!metric_value_from_vrl(valv, &d, &i, &dtype)) {
		carglog(carg, L_INFO, "vrl: skip metric '%s': .value must be number/bool\n",
			namev->u.bytes.data);
		return;
	}
	if (dtype == DATATYPE_INT)
		d = (double)i;

	alligator_ht *labels = labels_from_vrl_object(vrl_object_get(m, "labels", 6));
	vrl_value *buckets = vrl_object_get(m, "buckets", 7);
	const char *t = vrl_metric_type_str(m);
	int want_hist = (buckets && buckets->type == VRL_ARRAY && buckets->u.array.len > 0) &&
		(!t || !strcmp(t, "histogram"));

	if (want_hist) {
		if (emit_histogram_observation(carg, namev->u.bytes.data, d, labels, buckets))
			return;
		/* fall through if buckets invalid */
	}

	void *vp = (dtype == DATATYPE_DOUBLE) ? (void *)&d : (void *)&i;
	int do_update = vrl_metric_wants_update(m);
	vrl_maybe_set_prom_type(carg, m, namev->u.bytes.data);
	if (dtype == DATATYPE_DOUBLE)
		carglog(carg, L_INFO, "vrl: metric_%s %s = %.17g\n",
			do_update ? "update" : "add", namev->u.bytes.data, d);
	else
		carglog(carg, L_INFO, "vrl: metric_%s %s = %" PRId64 "\n",
			do_update ? "update" : "add", namev->u.bytes.data, i);
	if (do_update)
		metric_update(namev->u.bytes.data, labels, vp, dtype, carg);
	else
		metric_add(namev->u.bytes.data, labels, vp, dtype, carg);
}

static void vrl_export_metrics(context_arg *carg, vrl_value *event)
{
	if (!carg || !event || event->type != VRL_OBJECT)
		return;
	vrl_value *metrics = vrl_object_get(event, "metrics", 7);
	vrl_value *metric = vrl_object_get(event, "metric", 6);
	if ((!metrics || metrics->type != VRL_ARRAY || !metrics->u.array.len) && !metric) {
		carglog(carg, L_DEBUG,
			"vrl: no .metric / .metrics on event after transform (nothing to export)\n");
		return;
	}
	if (metrics && metrics->type == VRL_ARRAY) {
		carglog(carg, L_DEBUG, "vrl: exporting %zu metrics from .metrics\n",
			metrics->u.array.len);
		for (size_t i = 0; i < metrics->u.array.len; i++)
			emit_one_metric(carg, vrl_array_get(metrics, i));
	}
	if (metric)
		emit_one_metric(carg, metric);
}

static void emit_one_log(context_arg *carg, vrl_value *v)
{
	if (!v)
		return;
	if (v->type == VRL_BYTES && v->u.bytes.data) {
		carg_emit_log(carg, v->u.bytes.data, v->u.bytes.len);
		return;
	}
	if (v->type == VRL_OBJECT) {
		json_t *doc = vrl_value_to_json(v);
		if (!doc)
			return;
		carg_emit_log_document(carg, doc);
		json_decref(doc);
		return;
	}
	/* numbers/bools/arrays → string body via write_raw */
	size_t len = 0;
	char *s = vrl_value_to_string(v, &len);
	if (!s)
		return;
	carg_emit_log(carg, s, len);
	free(s);
}

/* Alligator extension: explicit .log / .logs → log_channel_out (not Vector VRL). */
static void vrl_export_logs(context_arg *carg, vrl_value *event)
{
	if (!carg || !carg->log_ch_out || !event || event->type != VRL_OBJECT)
		return;

	vrl_value *logs = vrl_object_get(event, "logs", 4);
	vrl_value *logv = vrl_object_get(event, "log", 3);

	if ((!logs || logs->type != VRL_ARRAY || !logs->u.array.len) && !logv) {
		carglog(carg, L_DEBUG, "vrl: no .log / .logs (skip log_channel_out)\n");
		return;
	}

	if (logs && logs->type == VRL_ARRAY) {
		carglog(carg, L_DEBUG, "vrl: exporting %zu logs from .logs\n", logs->u.array.len);
		for (size_t i = 0; i < logs->u.array.len; i++)
			emit_one_log(carg, vrl_array_get(logs, i));
	}
	if (logv)
		emit_one_log(carg, logv);
}

/* ---------------- per-stream state ---------------- */
/* vrl_stream is defined in vrl/type.h (shared with vrl_dns.c). */

static void vrl_apply_node_multiline(context_arg *carg, vrl_node *vn)
{
	if (!carg || !vn || !vn->ml_enabled)
		return;
	if (carg->ml_enabled)
		return; /* aggregate/carg settings win */
	carg->ml_start_pattern = strdup(vn->ml_start_pattern);
	carg->ml_condition_pattern = strdup(vn->ml_condition_pattern);
	carg->ml_mode = vn->ml_mode;
	carg->ml_enabled = 1;
}

/* Release all resources held by a stream (DNS timer, buffered records, ctx). */
static void vrl_stream_destroy(vrl_stream *st);

static vrl_stream *vrl_stream_ensure(context_arg *carg, vrl_node *vn)
{
	vrl_stream *st = carg->vrl_stream;
	if (st && st->vn == vn)
		return st;

	if (st) {
		vrl_stream_destroy(st);
		carg->vrl_stream = NULL;
	}

	st = calloc(1, sizeof(*st));
	st->vn = vn;
	st->carg = carg;
	st->ctx = vrl_ctx_new(vn->ll);
	st->ok = 1;
	st->dns_timeout_ms = vn->dns_timeout_ms ? vn->dns_timeout_ms : VRL_DNS_DEFAULT_TIMEOUT_MS;
	st->dns_poll_ms = vn->dns_poll_ms ? vn->dns_poll_ms : VRL_DNS_DEFAULT_POLL_MS;
	st->dns_negative_ttl_ms = vn->dns_negative_ttl_ms; /* 0 = disabled */
	/* Reachable from dns_lookup/reverse_dns builtins as a->ctx->host. */
	vrl_ctx_set_host(st->ctx, st);
	vrl_apply_node_multiline(carg, vn);
	carg_linebuf_ensure(carg);
	carg->vrl_stream = st;
	return st;
}

static const char *vrl_source_hint(context_arg *carg)
{
	if (carg->path && carg->path[0])
		return carg->path;
	if (carg->host[0])
		return carg->host;
	return "alligator";
}

static void vrl_resume_timer_cb(uv_timer_t *timer);

static void vrl_run_record(vrl_stream *st, const char *record, size_t len)
{
	context_arg *carg = st->carg;
	vrl_node *vn = st->vn;
	carglog(carg, L_INFO, "vrl: run record (%zu bytes) program='%s': '%.*s'\n",
		len, vn->name ? vn->name : "?", (int)(len > 200 ? 200 : len), record);
	vrl_stream_clear_secrets(st);
	vrl_ctx_reset(st->ctx);
	vrl_ctx_set_event(st->ctx, vrl_event_from_message(record, len, vrl_source_hint(carg)));
	vrl_status stt = vrl_exec(st->ctx, vn->prog->root);

	/* A dns_lookup()/reverse_dns()/http_request() first-sight cache miss
	 * paused the stream. The record is incomplete and MUST NOT be exported
	 * (side effects would duplicate on replay). The resume timer re-runs it
	 * once resolved. */
	if (st->dns_suspended) {
		if (st->await_http)
			carglog(carg, L_INFO, "vrl: record paused awaiting HTTP '%s'\n",
				st->http_url);
		else
			carglog(carg, L_INFO, "vrl: record paused awaiting DNS '%s' (rrtype %hu)\n",
				st->dns_name, st->dns_rrtype);
		return;
	}

	if (stt == VRL_ABORT) {
		carglog(carg, L_INFO, "vrl: abort: %s\n",
			st->ctx->abort_msg ? st->ctx->abort_msg : "(no message)");
		return;
	}
	if (stt != VRL_OK) {
		carglog(carg, L_ERROR, "vrl: error: %s\n",
			st->ctx->error ? st->ctx->error : "unknown");
		st->ok = 0;
		return;
	}
	if (carg->log_level >= L_DEBUG && st->ctx->event) {
		char *js = vrl_json_encode(st->ctx->event);
		carglog(carg, L_DEBUG, "vrl: event after transform: %s\n", js ? js : "(null)");
		free(js);
	}
	vrl_export_metrics(carg, st->ctx->event);
	vrl_export_logs(carg, st->ctx->event);
}

/* ---------------- DNS suspend/resume: record buffering ---------------- */

static void vrl_stream_enqueue(vrl_stream *st, const char *record, size_t len)
{
	if (st->q_len == st->q_cap) {
		/* Reclaim already-consumed slots before growing. */
		if (st->q_head) {
			size_t live = st->q_len - st->q_head;
			if (live)
				memmove(st->queue, st->queue + st->q_head,
					live * sizeof(*st->queue));
			st->q_len = live;
			st->q_head = 0;
		}
		if (st->q_len == st->q_cap) {
			size_t ncap = st->q_cap ? st->q_cap * 2 : 16;
			vrl_record_item *nq = realloc(st->queue, ncap * sizeof(*nq));
			if (!nq) {
				carglog(st->carg, L_ERROR,
					"vrl: dropping record while paused (OOM queue)\n");
				return;
			}
			st->queue = nq;
			st->q_cap = ncap;
		}
	}
	char *copy = malloc(len + 1);
	if (!copy) {
		carglog(st->carg, L_ERROR, "vrl: dropping record while paused (OOM)\n");
		return;
	}
	memcpy(copy, record, len);
	copy[len] = '\0';
	st->queue[st->q_len].data = copy;
	st->queue[st->q_len].len = len;
	st->q_len++;
}

static int vrl_stream_dequeue(vrl_stream *st, char **data, size_t *len)
{
	if (st->q_head >= st->q_len)
		return 0;
	*data = st->queue[st->q_head].data;
	*len = st->queue[st->q_head].len;
	st->queue[st->q_head].data = NULL;
	st->q_head++;
	if (st->q_head >= st->q_len) {
		st->q_head = 0;
		st->q_len = 0;
	}
	return 1;
}

static void vrl_stream_set_pending(vrl_stream *st, const char *record, size_t len)
{
	free(st->pending_record);
	st->pending_record = malloc(len + 1);
	if (!st->pending_record) {
		st->pending_len = 0;
		return;
	}
	memcpy(st->pending_record, record, len);
	st->pending_record[len] = '\0';
	st->pending_len = len;
}

static void vrl_resume_timer_close_cb(uv_handle_t *h)
{
	free(h);
}

static void vrl_stream_arm_resume(vrl_stream *st)
{
	context_arg *carg = st->carg;
	uint64_t now = carg->loop ? uv_now(carg->loop) : 0;
	st->dns_deadline_ms = now + st->dns_timeout_ms;

	if (!st->resume_timer) {
		st->resume_timer = calloc(1, sizeof(*st->resume_timer));
		if (!st->resume_timer) {
			carglog(carg, L_ERROR, "vrl: cannot allocate DNS resume timer\n");
			return;
		}
		uv_timer_init(carg->loop, st->resume_timer);
		st->resume_timer->data = st;
	}
	if (!uv_is_active((uv_handle_t *)st->resume_timer)) {
		uint64_t poll = st->dns_poll_ms ? st->dns_poll_ms : VRL_DNS_DEFAULT_POLL_MS;
		uv_timer_start(st->resume_timer, vrl_resume_timer_cb, poll, poll);
	}
}

/* Replay the paused record, then any records buffered while paused, in order.
 * If a replayed record suspends again (different name), it becomes the new
 * pending record and draining stops until that resolves. */
static void vrl_stream_drain(vrl_stream *st)
{
	if (st->pending_record) {
		char *rec = st->pending_record;
		size_t len = st->pending_len;
		st->pending_record = NULL;
		st->pending_len = 0;
		vrl_run_record(st, rec, len);
		if (st->dns_suspended) {
			vrl_stream_set_pending(st, rec, len);
			free(rec);
			vrl_stream_arm_resume(st);
			return;
		}
		free(rec);
	}

	char *data;
	size_t len;
	while (vrl_stream_dequeue(st, &data, &len)) {
		vrl_run_record(st, data, len);
		if (st->dns_suspended) {
			vrl_stream_set_pending(st, data, len);
			free(data);
			vrl_stream_arm_resume(st);
			return;
		}
		free(data);
	}
}

static void vrl_resume_timer_cb(uv_timer_t *timer)
{
	vrl_stream *st = timer->data;
	if (!st || !st->carg || !st->vn)
		return;
	context_arg *carg = st->carg;
	vrl_node *vn = st->vn;

	uv_mutex_lock(&vn->lock);

	if (!st->dns_suspended) {
		uv_timer_stop(timer);
		uv_mutex_unlock(&vn->lock);
		return;
	}

	uint64_t now = carg->loop ? uv_now(carg->loop) : 0;
	int ready = 0;

	if (st->await_http) {
		ready = vrl_http_cache_is_ready(st->http_url);
		if (!ready && now < st->dns_deadline_ms) {
			/* DNS may have landed since kickoff — retry oneshot connect. */
			vrl_http_try_connect(st->http_url);
			uv_mutex_unlock(&vn->lock);
			return;
		}
		if (!ready) {
			carglog(carg, L_ERROR,
				"vrl: HTTP request timeout after %" PRIu64 "ms for '%s'; "
				"continuing with null\n",
				st->dns_timeout_ms, st->http_url);
			vrl_http_cache_force_ready_null(st->http_url);
			st->http_force_null = 1;
		}
	} else {
		string *v = resolver_cache_lookup(st->dns_name, st->dns_rrtype);
		ready = (v != NULL);
		if (!ready && now < st->dns_deadline_ms) {
			uv_mutex_unlock(&vn->lock);
			return; /* keep polling */
		}
		if (!ready) {
			carglog(carg, L_ERROR,
				"vrl: DNS resolution timeout after %" PRIu64 "ms for '%s' (rrtype %hu); "
				"continuing with null\n",
				st->dns_timeout_ms, st->dns_name, st->dns_rrtype);
			vrl_dns_metric(get_str_by_rrtype(st->dns_rrtype), "timeout",
				       now > st->dns_timeout_ms ? now - st->dns_timeout_ms : 0, now);
			if (st->dns_negative_ttl_ms) {
				char key[VRL_DNS_KEY_MAX];
				snprintf(key, sizeof(key), "%s:%hu", st->dns_name, st->dns_rrtype);
				vrl_dns_neg_add(key, now, st->dns_negative_ttl_ms,
						st->vn ? st->vn->dns_negative_cache_max : 0);
			}
			st->dns_force_null = 1;
		}
	}

	st->dns_suspended = 0;
	st->await_http = 0;
	vrl_stream_drain(st);

	if (!st->dns_suspended) {
		/* Fully caught up: retire the timer and clear any timeout latch. */
		uv_timer_stop(timer);
		st->dns_force_null = 0;
		st->http_force_null = 0;
	}
	/* else: vrl_stream_drain() re-armed the timer for a new resolution. */

	uv_mutex_unlock(&vn->lock);
}

static void vrl_record_cb(void *ud, const char *record, size_t len)
{
	vrl_stream *st = (vrl_stream *)ud;

	/* Stream is paused awaiting a DNS answer: buffer this record so output
	 * order is preserved, and run it later on resume. */
	if (st->dns_suspended) {
		vrl_stream_enqueue(st, record, len);
		return;
	}

	vrl_run_record(st, record, len);

	/* This record triggered a first-sight cache miss and paused the stream. */
	if (st->dns_suspended) {
		vrl_stream_set_pending(st, record, len);
		vrl_stream_arm_resume(st);
	}
}

static vrl_node *vrl_node_load_for_carg(context_arg *carg)
{
	if (!carg || !carg->script)
		return NULL;
	char *name = carg->name ? carg->name : carg->script;
	vrl_node *vn = vrl_node_get(name);
	if (vn)
		return vn;
	json_t *tmp = json_object();
	json_object_set_new(tmp, "name", json_string(name));
	json_object_set_new(tmp, "script", json_string(carg->script));
	if (carg->key)
		json_object_set_new(tmp, "key", json_string(carg->key));
	int rc = vrl_push(tmp);
	json_decref(tmp);
	return rc ? vrl_node_get(name) : NULL;
}

void vrl_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!metrics || !size || !carg) {
		if (carg)
			carg->parser_status = 0;
		return;
	}

	carglog(carg, L_INFO, "vrl: handler got %zu bytes (name=%s)\n",
		size, carg->name ? carg->name : "(none)");

	vrl_node *vn = NULL;
	if (carg->name)
		vn = vrl_node_get(carg->name);
	if (!vn)
		vn = vrl_node_load_for_carg(carg);
	if (!vn)
		vn = vrl_node_get_any();
	if (!vn || !vn->prog) {
		carglog(carg, L_ERROR,
			"vrl: no compiled program (set name=... / vrl=... and push first)\n");
		carg->parser_status = 0;
		return;
	}

	uv_mutex_lock(&vn->lock);
	vrl_stream *st = vrl_stream_ensure(carg, vn);
	st->ok = 1;

	alligator_linebuf_feed(&carg->ml_lb, metrics, size, vrl_record_cb, st);

	carglog(carg, L_INFO, "vrl: processed chunk via linebuf (multiline=%s)\n",
		carg->ml_enabled ? "on" : "off");

	uv_mutex_unlock(&vn->lock);
	carg->parser_status = st->ok ? 1 : 0;
}

static void vrl_stream_destroy(vrl_stream *st)
{
	if (!st)
		return;

	/* Stop polling for DNS and retire the timer handle asynchronously. */
	if (st->resume_timer) {
		uv_timer_stop(st->resume_timer);
		st->resume_timer->data = NULL;
		if (!uv_is_closing((uv_handle_t *)st->resume_timer))
			uv_close((uv_handle_t *)st->resume_timer, vrl_resume_timer_close_cb);
		st->resume_timer = NULL;
	}

	free(st->pending_record);
	for (size_t i = st->q_head; i < st->q_len; i++)
		free(st->queue[i].data);
	free(st->queue);

	vrl_stream_free_secrets(st);

	if (st->ctx)
		vrl_ctx_free(st->ctx);
	free(st);
}

void vrl_stream_free(context_arg *carg)
{
	if (!carg || !carg->vrl_stream)
		return;
	vrl_stream *st = carg->vrl_stream;

	/* Only flush the multiline tail if we are not mid-pause (a paused stream
	 * cannot complete records anyway; buffered work is discarded on free). */
	if (carg->ml_lb_ready && !st->dns_suspended)
		alligator_linebuf_flush(&carg->ml_lb, vrl_record_cb, st);

	vrl_stream_destroy(st);
	carg->vrl_stream = NULL;
}

void vrl_parser_push(void)
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	actx->key = strdup("vrl");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = vrl_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = vrl_mesg;
	strlcpy(actx->handler[0].key, "vrl", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
