#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/json_query.h"
#include "metric/metric_types.h"
#include "metric/labels.h"
#include "main.h"

/* Selective NATS monitoring parser (varz/connz/routez/subsz).
 * Blind json_query turned every string into a gauge with value 1
 * (tls_version, lang, server_id, …) and exploded per-connection series.
 * Align with prometheus-nats-exporter: numbers/bools only, identity strings
 * as label gauges, connz as aggregates, http_req_stats keyed by endpoint. */

#define NATS_NAME_SIZE 256

static inline void nats_metric_family(context_arg *carg, const char *name, const char *help)
{
	namespace_metric_family_set(NULL, carg, name, METRIC_TYPE_GAUGE, help);
}

static int nats_json_as_int64(json_t *v, int64_t *out)
{
	if (!v)
		return 0;
	if (json_is_integer(v)) {
		*out = json_integer_value(v);
		return 1;
	}
	if (json_is_real(v)) {
		*out = (int64_t)json_real_value(v);
		return 1;
	}
	return 0;
}

static void nats_emit_int(context_arg *carg, const char *name, int64_t val)
{
	nats_metric_family(carg, name, "NATS monitoring numeric metric.");
	metric_add_auto((char *)name, &val, DATATYPE_INT, carg);
}

static void nats_emit_double(context_arg *carg, const char *name, double val)
{
	nats_metric_family(carg, name, "NATS monitoring numeric metric.");
	metric_add_auto((char *)name, &val, DATATYPE_DOUBLE, carg);
}

static void nats_emit_int_label(context_arg *carg, const char *name, int64_t val, char *lname, char *lval)
{
	nats_metric_family(carg, name, "NATS monitoring numeric metric.");
	metric_add_labels((char *)name, &val, DATATYPE_INT, carg, lname, lval);
}

static void nats_emit_bool(context_arg *carg, const char *name, json_t *v)
{
	if (!v || (!json_is_true(v) && !json_is_false(v)))
		return;
	int64_t val = json_is_true(v) ? 1 : 0;
	nats_emit_int(carg, name, val);
}

static void nats_emit_bool_label(context_arg *carg, const char *name, json_t *v, char *lname, char *lval)
{
	if (!v || (!json_is_true(v) && !json_is_false(v)))
		return;
	int64_t val = json_is_true(v) ? 1 : 0;
	nats_emit_int_label(carg, name, val, lname, lval);
}

/* Identity string → gauge{value="<str>"} 1 (same idea as prometheus-nats-exporter). */
static void nats_emit_string_value(context_arg *carg, const char *name, json_t *v)
{
	if (!v || !json_is_string(v))
		return;
	const char *s = json_string_value(v);
	if (!s || !*s)
		return;
	uint64_t one = 1;
	nats_metric_family(carg, name, "NATS identity/info string as labeled gauge.");
	metric_add_labels((char *)name, &one, DATATYPE_UINT, carg, "value", (char *)s);
}

static int nats_is_identity_string_key(const char *key)
{
	return !strcmp(key, "server_id") || !strcmp(key, "server_name") ||
	       !strcmp(key, "version") || !strcmp(key, "domain") ||
	       !strcmp(key, "leader") || !strcmp(key, "name");
}

static int nats_skip_nested_object(const char *key)
{
	/* Opaque / rarely useful nested trees — keep cardinality low. */
	return !strcmp(key, "leaf") || !strcmp(key, "gateway") ||
	       !strcmp(key, "jetstream") || !strcmp(key, "mqtt") ||
	       !strcmp(key, "websocket") || !strcmp(key, "trusted_operators_claim");
}

static void nats_emit_object_numbers(context_arg *carg, const char *prefix, json_t *obj, int depth)
{
	const char *key;
	json_t *value;
	char name[NATS_NAME_SIZE];

	if (!obj || !json_is_object(obj) || depth > 3)
		return;

	json_object_foreach(obj, key, value) {
		int t = json_typeof(value);

		if (t == JSON_OBJECT) {
			if (nats_skip_nested_object(key))
				continue;
			/* http_req_stats: map path → counter with endpoint label */
			if (!strcmp(key, "http_req_stats")) {
				const char *ep;
				json_t *cnt;
				json_object_foreach(value, ep, cnt) {
					int64_t iv;
					if (!nats_json_as_int64(cnt, &iv))
						continue;
					snprintf(name, sizeof(name), "%s_http_req_stats", prefix);
					nats_emit_int_label(carg, name, iv, "endpoint", (char *)ep);
				}
				continue;
			}
			snprintf(name, sizeof(name), "%s_%s", prefix, key);
			nats_emit_object_numbers(carg, name, value, depth + 1);
			continue;
		}

		if (t == JSON_ARRAY)
			continue;

		if (t == JSON_STRING) {
			if (nats_is_identity_string_key(key)) {
				snprintf(name, sizeof(name), "%s_%s", prefix, key);
				nats_emit_string_value(carg, name, value);
			}
			/* uptime/now/start/host/go/… — skip (not useful as value=1) */
			continue;
		}

		if (t == JSON_TRUE || t == JSON_FALSE) {
			snprintf(name, sizeof(name), "%s_%s", prefix, key);
			nats_emit_bool(carg, name, value);
			continue;
		}

		if (t == JSON_INTEGER) {
			int64_t iv = json_integer_value(value);
			snprintf(name, sizeof(name), "%s_%s", prefix, key);
			nats_emit_int(carg, name, iv);
			continue;
		}

		if (t == JSON_REAL) {
			double dv = json_real_value(value);
			snprintf(name, sizeof(name), "%s_%s", prefix, key);
			nats_emit_double(carg, name, dv);
			continue;
		}
	}
}

static json_t *nats_load_json(char *metrics, size_t size, context_arg *carg)
{
	(void)size;
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root) {
		carglog(carg, L_ERROR, "nats json error on line %d: %s\n", error.line, error.text);
		return NULL;
	}
	return root;
}

void nats_varz_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = nats_load_json(metrics, size, carg);
	if (!root) {
		carg->parser_status = 0;
		return;
	}
	nats_emit_object_numbers(carg, "nats_varz", root, 0);
	json_decref(root);
	carg->parser_status = 1;
}

void nats_subsz_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = nats_load_json(metrics, size, carg);
	if (!root) {
		carg->parser_status = 0;
		return;
	}
	/* Historical prefix is nats_subz (not nats_subsz). */
	nats_emit_object_numbers(carg, "nats_subz", root, 0);
	json_decref(root);
	carg->parser_status = 1;
}

void nats_connz_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = nats_load_json(metrics, size, carg);
	if (!root) {
		carg->parser_status = 0;
		return;
	}

	/* Summary mode (prometheus-nats-exporter -connz): no per-cid series. */
	static const char *top_keys[] = {
		"num_connections", "total", "offset", "limit", NULL
	};
	for (int i = 0; top_keys[i]; i++) {
		int64_t iv;
		if (!nats_json_as_int64(json_object_get(root, top_keys[i]), &iv))
			continue;
		char name[NATS_NAME_SIZE];
		snprintf(name, sizeof(name), "nats_connz_%s", top_keys[i]);
		nats_emit_int(carg, name, iv);
	}

	nats_emit_string_value(carg, "nats_connz_server_id", json_object_get(root, "server_id"));

	int64_t pending_bytes = 0, subscriptions = 0;
	int64_t in_bytes = 0, out_bytes = 0, in_msgs = 0, out_msgs = 0;

	json_t *connections = json_object_get(root, "connections");
	if (connections && json_is_array(connections)) {
		size_t n = json_array_size(connections);
		for (size_t i = 0; i < n; i++) {
			json_t *conn = json_array_get(connections, i);
			int64_t v;
			if (nats_json_as_int64(json_object_get(conn, "pending_bytes"), &v))
				pending_bytes += v;
			if (nats_json_as_int64(json_object_get(conn, "subscriptions"), &v))
				subscriptions += v;
			if (nats_json_as_int64(json_object_get(conn, "in_bytes"), &v))
				in_bytes += v;
			if (nats_json_as_int64(json_object_get(conn, "out_bytes"), &v))
				out_bytes += v;
			if (nats_json_as_int64(json_object_get(conn, "in_msgs"), &v))
				in_msgs += v;
			if (nats_json_as_int64(json_object_get(conn, "out_msgs"), &v))
				out_msgs += v;
		}
	}

	nats_emit_int(carg, "nats_connz_pending_bytes", pending_bytes);
	nats_emit_int(carg, "nats_connz_subscriptions", subscriptions);
	nats_emit_int(carg, "nats_connz_in_bytes", in_bytes);
	nats_emit_int(carg, "nats_connz_out_bytes", out_bytes);
	nats_emit_int(carg, "nats_connz_in_msgs", in_msgs);
	nats_emit_int(carg, "nats_connz_out_msgs", out_msgs);

	json_decref(root);
	carg->parser_status = 1;
}

void nats_routez_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = nats_load_json(metrics, size, carg);
	if (!root) {
		carg->parser_status = 0;
		return;
	}

	int64_t num_routes;
	if (nats_json_as_int64(json_object_get(root, "num_routes"), &num_routes))
		nats_emit_int(carg, "nats_routez_num_routes", num_routes);

	nats_emit_string_value(carg, "nats_routez_server_id", json_object_get(root, "server_id"));

	json_t *routes = json_object_get(root, "routes");
	if (routes && json_is_array(routes)) {
		size_t n = json_array_size(routes);
		for (size_t i = 0; i < n; i++) {
			json_t *route = json_array_get(routes, i);
			char rid_buf[32];
			json_t *rid_j = json_object_get(route, "rid");
			if (json_is_integer(rid_j))
				snprintf(rid_buf, sizeof(rid_buf), "%" PRId64, (int64_t)json_integer_value(rid_j));
			else if (json_is_string(rid_j))
				snprintf(rid_buf, sizeof(rid_buf), "%s", json_string_value(rid_j));
			else
				continue;

			static const char *num_keys[] = {
				"port", "pending_size", "in_msgs", "out_msgs",
				"in_bytes", "out_bytes", "subscriptions", NULL
			};
			for (int k = 0; num_keys[k]; k++) {
				int64_t iv;
				if (!nats_json_as_int64(json_object_get(route, num_keys[k]), &iv))
					continue;
				char name[NATS_NAME_SIZE];
				snprintf(name, sizeof(name), "nats_routez_routes_%s", num_keys[k]);
				nats_emit_int_label(carg, name, iv, "rid", rid_buf);
			}

			nats_emit_bool_label(carg, "nats_routez_routes_did_solicit",
					     json_object_get(route, "did_solicit"), "rid", rid_buf);
			nats_emit_bool_label(carg, "nats_routez_routes_is_configured",
					     json_object_get(route, "is_configured"), "rid", rid_buf);

			/* remote_id / ip as labeled identity gauges, not value=1 without labels */
			json_t *remote_id = json_object_get(route, "remote_id");
			if (remote_id && json_is_string(remote_id)) {
				uint64_t one = 1;
				nats_metric_family(carg, "nats_routez_routes_remote_id",
						   "NATS route remote server id.");
				metric_add_labels2("nats_routez_routes_remote_id", &one, DATATYPE_UINT, carg,
						   "rid", rid_buf, "value", (char *)json_string_value(remote_id));
			}
			json_t *ip = json_object_get(route, "ip");
			if (ip && json_is_string(ip)) {
				uint64_t one = 1;
				nats_metric_family(carg, "nats_routez_routes_ip", "NATS route peer IP.");
				metric_add_labels2("nats_routez_routes_ip", &one, DATATYPE_UINT, carg,
						   "rid", rid_buf, "value", (char *)json_string_value(ip));
			}
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

string *nats_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

string* nats_varz_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return nats_gen_url(hi, "/varz", env, proxy_settings); }
string* nats_connz_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return nats_gen_url(hi, "/connz", env, proxy_settings); }
string* nats_routez_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return nats_gen_url(hi, "/routez", env, proxy_settings); }
string* nats_subsz_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return nats_gen_url(hi, "/subsz", env, proxy_settings); }

void nats_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("nats");
	actx->handlers = 4;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = nats_varz_handler;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = nats_varz_mesg;
	strlcpy(actx->handler[0].key,"nats_varz", 255);

	actx->handler[1].name = nats_connz_handler;
	actx->handler[1].validator = json_validator;
	actx->handler[1].mesg_func = nats_connz_mesg;
	strlcpy(actx->handler[1].key,"nats_connz", 255);

	actx->handler[2].name = nats_routez_handler;
	actx->handler[2].validator = json_validator;
	actx->handler[2].mesg_func = nats_routez_mesg;
	strlcpy(actx->handler[2].key,"nats_routez", 255);

	actx->handler[3].name = nats_subsz_handler;
	actx->handler[3].validator = json_validator;
	actx->handler[3].mesg_func = nats_subsz_mesg;
	strlcpy(actx->handler[3].key,"nats_subsz", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
