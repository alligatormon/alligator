#include <stdio.h>
#include <inttypes.h>
#include "common/logs.h"
#include <jansson.h>
#include "common/selector.h"
#include "common/validator.h"
#include "common/json_query.h"
#include "events/context_arg.h"
#include "query/type.h"
#include "parsers/mongodb_wire_bson.h"
#include "parsers/mongodb_wire_client.h"
#include "common/aggregator.h"
#include "main.h"

static alligator_ht *mongodb_clients = NULL;
static uv_timer_t mongodb_timer;
static int mongodb_timer_started = 0;

typedef struct mongodb_query_ctx_t {
	context_arg *carg;
	query_node *qn;
	mongodb_wire_client *client;
	char dbname[256];
	char collname[256];
	uint64_t docs_emitted;
} mongodb_query_ctx_t;

static mongodb_query_ctx_t *mongodb_foreach_ctx = NULL;
static void mongodb_query_foreach_cb(void *funcarg, void *arg);
static int mongodb_emit_query_doc(json_t *doc, void *arg)
{
	mongodb_query_ctx_t *ctx = arg;
	char *trace_doc = NULL;
	if (ctx->carg->log_level >= L_TRACE) {
		trace_doc = json_dumps(doc, JSON_COMPACT);
		if (trace_doc)
			carglog(ctx->carg, L_TRACE, "mongodb query result doc: db='%s' coll='%s' doc=%s\n", ctx->dbname, ctx->collname, trace_doc);
	}

	if (ctx->qn->pquery && ctx->qn->pquery_size) {
		char *doc_json = json_dumps(doc, JSON_COMPACT);
		if (doc_json) {
			context_arg qcarg = *ctx->carg;
			qcarg.ns = ctx->dbname;
			carglog(ctx->carg, L_TRACE, "mongodb json_query parse: make='%s' jpaths=%u\n", ctx->qn->make, ctx->qn->pquery_size);
			json_query(doc_json, NULL, ctx->qn->make, &qcarg, ctx->qn->pquery, ctx->qn->pquery_size);
			free(doc_json);
			ctx->docs_emitted++;
		}
		if (trace_doc)
			free(trace_doc);
		return 1;
	}

	alligator_ht *labels = alligator_ht_init(NULL);
	const char *key;
	json_t *val;

	labels_hash_insert_nocache(labels, "dbname", ctx->dbname);
	labels_hash_insert_nocache(labels, "collection", ctx->collname);

	json_object_foreach(doc, key, val) {
		query_field *qf = query_field_get(ctx->qn->qf_hash, (char*)key);
		if (json_is_real(val)) {
			if (qf) {
				qf->d = json_real_value(val);
				qf->type = DATATYPE_DOUBLE;
			}
		}
		else if (json_is_integer(val) || json_is_boolean(val)) {
			if (qf) {
				qf->i = json_is_boolean(val) ? json_is_true(val) : json_integer_value(val);
				qf->type = DATATYPE_INT;
			}
		}
		else if (json_is_string(val)) {
			const char *v = json_string_value(val);
			size_t len = v ? strlen(v) : 0;
			if (v && len) {
				char *dup = strndup(v, len);
				metric_label_value_validator_normalizer(dup, len);
				labels_hash_insert_nocache(labels, (char*)key, dup);
			}
		}
	}

	ctx->qn->labels = labels;
	ctx->qn->carg = ctx->carg;
	query_set_values(ctx->qn);
	labels_hash_free(labels);
	ctx->qn->labels = NULL;
	ctx->docs_emitted++;
	if (trace_doc)
		free(trace_doc);
	return 1;
}

static void mongodb_run_query(mongodb_query_ctx_t *ctx, const char *expr)
{
	mongodb_query_expr_t parsed = {{0}};
	char err[256] = {0};
	const char *target_coll = ctx->collname;

	if (!mongodb_parse_find_expr(expr, &parsed)) {
		carglog(ctx->carg, L_ERROR, "mongodb query parse failed: unsupported expr '%s'\n", expr);
		return;
	}
	if (parsed.has_collection && parsed.collection[0])
		target_coll = parsed.collection;

	carglog(ctx->carg, L_DEBUG, "mongodb query start: db='%s' coll='%s' expr='%s'\n", ctx->dbname, target_coll, expr);

	if (!mongodb_wire_find(ctx->client, ctx->dbname, target_coll, parsed.filter_json, mongodb_emit_query_doc, ctx, err, sizeof(err)))
		carglog(ctx->carg, L_ERROR, "mongodb find failed for %s.%s: %s\n", ctx->dbname, target_coll, err);
	else
		carglog(ctx->carg, L_DEBUG, "mongodb query done: db='%s' coll='%s' docs=%"u64"\n", ctx->dbname, target_coll, ctx->docs_emitted);
}

static void mongodb_execute_datasource(context_arg *carg, mongodb_wire_client *client, query_ds *qds, const char *db, const char *coll)
{
	carglog(carg, L_TRACE, "mongodb query_get result: db='%s' coll='%s' qds=%p\n", db, coll, (void*)qds);

	if (!qds)
		return;

	carglog(carg, L_TRACE, "mongodb query datasource matched: db='%s' coll='%s' queries=%"u64"\n", db, coll, (uint64_t)alligator_ht_count(qds->hash));

	mongodb_query_ctx_t qctx = {0};
	qctx.carg = carg;
	qctx.client = client;
	strlcpy(qctx.dbname, db, sizeof(qctx.dbname));
	strlcpy(qctx.collname, coll, sizeof(qctx.collname));
	qctx.qn = NULL;
	qctx.docs_emitted = 0;
	mongodb_foreach_ctx = &qctx;

	alligator_ht_foreach_arg(qds->hash, mongodb_query_foreach_cb, NULL);
	mongodb_foreach_ctx = NULL;
}

static void mongodb_query_foreach_cb(void *funcarg, void *arg)
{
	(void)funcarg;
	query_node *qn = arg;
	if (!mongodb_foreach_ctx || !qn)
		return;
	mongodb_foreach_ctx->qn = qn;
	mongodb_run_query(mongodb_foreach_ctx, qn->expr);
}

static void mongodb_collect_queries_for_collection(context_arg *carg, mongodb_wire_client *client, const char *db, const char *coll)
{
	if (!carg->name)
		return;

	char ds[1024];
	query_ds *qds;

	carglog(carg, L_TRACE, "mongodb query_get fetch: datasource='%s'\n", carg->name);
	qds = query_get(carg->name);
	mongodb_execute_datasource(carg, client, qds, db, coll);

	snprintf(ds, sizeof(ds), "%s/*", carg->name);
	carglog(carg, L_TRACE, "mongodb query_get fetch: datasource='%s'\n", ds);
	qds = query_get(ds);
	mongodb_execute_datasource(carg, client, qds, db, coll);

	snprintf(ds, sizeof(ds), "%s/%s", carg->name, db);
	carglog(carg, L_TRACE, "mongodb query_get fetch: datasource='%s'\n", ds);
	qds = query_get(ds);
	mongodb_execute_datasource(carg, client, qds, db, coll);

	snprintf(ds, sizeof(ds), "%s/%s/%s", carg->name, db, coll);
	carglog(carg, L_TRACE, "mongodb query_get fetch: datasource='%s'\n", ds);
	qds = query_get(ds);
	mongodb_execute_datasource(carg, client, qds, db, coll);
}

static void mongodb_run_single(context_arg *carg)
{
	uint64_t ok = 1, bad = 0;
	mongodb_wire_client *client = NULL;
	char err[256] = {0};
	uint64_t total_collections = 0;

	carglog(carg, L_INFO, "mongodb cycle start: host='%s' datasource='%s'\n", carg->host, carg->name ? carg->name : "");

	if (!mongodb_wire_client_open(&client, carg->url, err, sizeof(err))) {
		carglog(carg, L_ERROR, "mongodb connect failed: %s\n", err);
		metric_add_labels5("alligator_connect_ok", &bad, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mongodb");
		return;
	}
	if (!client) {
		metric_add_labels5("alligator_connect_ok", &bad, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mongodb");
		return;
	}

	metric_add_labels5("alligator_connect_ok", &ok, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mongodb");

	char **dbs = NULL, **cols = NULL;
	size_t db_count = 0, coll_count = 0;
	if (mongodb_wire_list_databases(client, &dbs, &db_count, err, sizeof(err))) {
		carglog(carg, L_INFO, "mongodb listDatabases ok: count=%"u64"\n", (uint64_t)db_count);

		for (size_t i = 0; i < db_count; i++) {
			cols = NULL;
			coll_count = 0;
			if (!mongodb_wire_list_collections(client, dbs[i], &cols, &coll_count, err, sizeof(err))) {
				carglog(carg, L_ERROR, "mongodb listCollections failed for '%s': %s\n", dbs[i], err);
				continue;
			}
			total_collections += coll_count;
			carglog(carg, L_DEBUG, "mongodb listCollections ok: db='%s' count=%"u64"\n", dbs[i], (uint64_t)coll_count);

			for (size_t j = 0; j < coll_count; j++)
				mongodb_collect_queries_for_collection(carg, client, dbs[i], cols[j]);
			mongodb_wire_free_string_list(cols, coll_count);
		}
	}
	else {
		carglog(carg, L_ERROR, "mongodb listDatabases failed: %s\n", err);
	}

	mongodb_wire_free_string_list(dbs, db_count);
	mongodb_wire_client_close(client);

	carglog(carg, L_INFO, "mongodb cycle done: dbs=%"u64" collections=%"u64"\n", (uint64_t)db_count, total_collections);
}

static void mongodb_run_single_foreach(void *arg)
{
	mongodb_run_single(arg);
}

static void mongodb_timer_cb(uv_timer_t *handle)
{
	(void)handle;
	if (mongodb_clients)
		alligator_ht_foreach(mongodb_clients, mongodb_run_single_foreach);
}

int mongodb_aggregator(context_arg *carg)
{
	if (!mongodb_clients)
		mongodb_clients = alligator_ht_init(NULL);

	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);
	alligator_ht_insert(mongodb_clients, &(carg->node), carg, tommy_strhash_u32(0, carg->key));

	if (!mongodb_timer_started) {
		uv_timer_init(ac->loop, &mongodb_timer);
		uv_timer_start(&mongodb_timer, mongodb_timer_cb, ac->aggregator_startup, ac->aggregator_repeat);
		mongodb_timer_started = 1;
	}

	return 1;
}

void* mongodb_data(host_aggregator_info *hi, void *arg, void *data)
{
	json_t *aggregate = (json_t*)data;

	char *cstr = json_dumps(aggregate, JSON_INDENT(2));
	return cstr;
}

void mongodb_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("mongodb");
	actx->handlers = 1;
	actx->data_func = mongodb_data;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].smart_aggregator_replace = mongodb_aggregator;
	strlcpy(actx->handler[0].key, "mongodb", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
