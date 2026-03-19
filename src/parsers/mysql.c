#include "query/type.h"
#include "common/validator.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "main.h"
#include "parsers/mysql2.h"
#define MY_TYPE_MYSQL 0
#define MY_TYPE_SPHINXSEARCH 1

typedef struct my_data {
	int type;   // MY_TYPE_MYSQL or MY_TYPE_SPHINXSEARCH
	mysql_conn_t *conn;
} my_data;

typedef struct sphinx_ctx_t {
	context_arg *carg;
	const char  *qtype;
} sphinx_ctx_t;

typedef struct mysql_db_list_t {
	char **dbs;
	uint64_t count;
	uint64_t cap;
} mysql_db_list_t;

typedef struct mysql_exec_ctx_t {
	context_arg *carg;
	query_node  *qn;
} mysql_exec_ctx_t;

void mysql_run(void* arg);
void mysql_run_all_await(context_arg *carg);
void sphinxsearch_run_all_await(context_arg *carg);

static uv_loop_t *mysql_timer_loop = NULL;

static void mysql_run_with_loop(void *arg)
{
		context_arg *carg = arg;
		if (mysql_timer_loop)
				carg->loop = mysql_timer_loop;
		mysql_run(carg);
}

void mysql_timer_without_thread(uv_timer_t* handle) {
		(void)handle;
		mysql_timer_loop = handle ? handle->loop : ac->loop;
		alligator_ht_foreach(ac->my_aggregator, mysql_run_with_loop);
}

void mysql_without_thread()
{
		uv_timer_init(ac->loop, &ac->my_timer);
		uv_timer_start(&ac->my_timer, mysql_timer_without_thread, ac->aggregator_startup, ac->aggregator_repeat);
}

void mysql_client_handler()
{
		mysql_without_thread();
}

static inline mysql_conn_t *mysql_conn_from_carg(context_arg *carg)
{
	my_data *data = carg ? (my_data*)carg->data : NULL;
	return data ? data->conn : NULL;
}

typedef struct mysql_worker_arg_t {
	context_arg *carg;
	my_data *data;
} mysql_worker_arg_t;

static void mysql_worker_thread(void *arg)
{
	mysql_worker_arg_t *wa = arg;
	context_arg *carg = wa->carg;
	my_data *data = wa->data;
	mysql_conn_t *conn = data->conn;

	while (conn && !mysql2_is_ready(conn) && !mysql2_is_failed(conn)) {
		uv_sleep(1000);
	}

	if (conn && mysql2_is_ready(conn)) {
		if (data->type == MY_TYPE_MYSQL)
			mysql_run_all_await(carg);
		else if (data->type == MY_TYPE_SPHINXSEARCH)
			sphinxsearch_run_all_await(carg);
	} else {
		carg->parser_status = 0;
	}

	carg->running = 0;
	free(wa);
}




static void mysql_db_list_cb(mysql_row_t *row, void *ud)
{
	mysql_db_list_t *st = ud;
	if (!row || row->column_count < 1)
		return;

	if (st->count == st->cap) {
		st->cap = st->cap ? st->cap * 2 : 8;
		st->dbs = realloc(st->dbs, st->cap * sizeof(char*));
	}

	uint64_t len = row->lengths[0];
	st->dbs[st->count] = strndup((const char*)row->fields[0], len);
	st->count++;
}


static void mysql_exec_cb(mysql_row_t *row, void *ud)
{
	mysql_exec_ctx_t *ctx = ud;
	context_arg *carg = ctx->carg;
	query_node *qn = ctx->qn;

	if (!row)
		return;

	alligator_ht *hash = alligator_ht_init(NULL);
	if (carg->ns)
		labels_hash_insert_nocache(hash, "dbname", carg->ns);

	for (int i = 0; i < row->column_count; i++) {
		if (!row->fields[i])
			continue;
		char *res = strndup((const char*)row->fields[i], row->lengths[i]);

		char *colname;
		char colname_fb[64];
		if (row->column_names && row->column_names[i] && row->column_names[i][0])
			colname = row->column_names[i];
		else {
			snprintf(colname_fb, sizeof(colname_fb), "col%d", i);
			colname = colname_fb;
		}

		query_field *qf = query_field_get(qn->qf_hash, colname);
		if (qf) {
			/* Привязка типа к метаданным MySQL, чтобы qf->type соответствовал значению */
			uint8_t t = row->column_types ? row->column_types[i] : 0;
			uint16_t fl = row->column_flags ? row->column_flags[i] : 0;
			const uint16_t MYSQL_UNSIGNED_FLAG = 0x0020;

			switch (t) {
				/* integer-ish (в т.ч. bool как TINYINT) */
				case 0x01: /* MYSQL_TYPE_TINY   */
				case 0x02: /* MYSQL_TYPE_SHORT  */
				case 0x03: /* MYSQL_TYPE_LONG   */
				case 0x08: /* MYSQL_TYPE_LONGLONG */
				case 0x09: /* MYSQL_TYPE_INT24  */
				case 0x0d: /* MYSQL_TYPE_YEAR   */
					if (fl & MYSQL_UNSIGNED_FLAG) {
						qf->u = strtoull(res, NULL, 10);
						qf->type = DATATYPE_UINT;
					} else {
						qf->i = strtoll(res, NULL, 10);
						qf->type = DATATYPE_INT;
					}
					break;

				/* float-ish */
				case 0x04: /* MYSQL_TYPE_FLOAT  */
				case 0x05: /* MYSQL_TYPE_DOUBLE */
				case 0x00: /* MYSQL_TYPE_DECIMAL */
				case 0xf6: /* MYSQL_TYPE_NEWDECIMAL */
					qf->d = strtod(res, NULL);
					qf->type = DATATYPE_DOUBLE;
					break;

				default:
					labels_hash_insert_nocache(hash, colname, res);
					break;
			}
		} else {
			labels_hash_insert_nocache(hash, colname, res);
		}
		if (carg->log_level > 2)
			printf("\tfield '%s': '%s'\n", colname, res);
		free(res);
	}

	qn->labels = hash;
	qn->carg = carg;
	query_set_values(qn);
	labels_hash_free(hash);
	qn->labels = NULL;
}

static void mysql_run_single_query_await(context_arg *carg, query_node *qn)
{
	mysql_exec_ctx_t ctx = { carg, qn };
	mysql2_await_query(mysql_conn_from_carg(carg), qn->expr, mysql_exec_cb, &ctx);
}

void mysql_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	carglog(carg, L_INFO, "mysql_queries_foreach\n");

	query_node *qn = arg;
	carglog(carg, L_INFO, "+-+-+-+-+-+-+-+\ninit query datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	mysql_run_single_query_await(carg, qn);
}   

void mysql_run_all_await(context_arg *carg)
{
	mysql_db_list_t dbl = {0};
	if (carg->name) {
		mysql2_await_query(mysql_conn_from_carg(carg), "SHOW DATABASES", mysql_db_list_cb, &dbl);
	}

	/* specific namespace: name */
	if (carg->name) {
		query_ds *qds = query_get(carg->name);
		if (qds) {
			if (carg->log_level > 1)
				printf("run queries for namespace '%s'\n", carg->name);
		
			alligator_ht_foreach_arg(qds->hash, mysql_queries_foreach, carg);
		}
	}

	for (uint64_t i = 0; i < dbl.count; ++i) {
		const char *dbname = dbl.dbs[i];

		/* USE db */
		char use_query[256];
		snprintf(use_query, sizeof(use_query), "USE `%s`", dbname);
		mysql2_await_query(mysql_conn_from_carg(carg), use_query, NULL, NULL);

		context_arg db_carg = *carg;
		char url[1024];
		char name[1024];
		char ns[256];

		db_carg.url = url;
		db_carg.name = name;
		db_carg.ns = ns;
		db_carg.key = carg->key;
		db_carg.log_level = carg->log_level;

		snprintf(db_carg.url, sizeof(url), "%s/%s", carg->url, dbname);
		strlcpy(db_carg.ns, dbname, sizeof(ns));

		/* wildcard namespace: name */
		snprintf(name, sizeof(name), "%s/*", carg->name);
		query_ds *qds = query_get(name);
		if (qds) {
			if (carg->log_level > 1)
				printf("run wildcard queries for namespace '%s'\n", name);

			alligator_ht_foreach_arg(qds->hash, mysql_queries_foreach, &db_carg);
		}

		/* specific datasource: name/dbname */
		if (carg->name) {
			snprintf(db_carg.name, sizeof(name), "%s/%s", carg->name, dbname);
			qds = query_get(db_carg.name);
			if (qds) {
				if (carg->log_level > 1)
					printf("exec queries for namespace '%s'\n", db_carg.name);

				alligator_ht_foreach_arg(qds->hash, mysql_queries_foreach, &db_carg);
			}
		}



	}

	for (uint64_t i = 0; i < dbl.count; ++i)
		free(dbl.dbs[i]);
	free(dbl.dbs);
}


static void sphinxsearch_row_cb(mysql_row_t *row, void *ud)
{
	sphinx_ctx_t *sctx = ud;
	context_arg *carg = sctx->carg;
	const char *qtype = sctx->qtype;

	if (!row)
		return;

	unsigned int num_fields = row->column_count;
	char mname[255];

	/* status / variables / plan */
	if (!strcmp(qtype, "status") || !strcmp(qtype, "variables") || !strcmp(qtype, "plan")) {
		if (num_fields < 2)
			return;
		char *Counter = (char*)row->fields[0];
		char *Value   = (char*)row->fields[1];
		if (!Counter || !Value)
			return;

		snprintf(mname, sizeof(mname), "sphinxsearch_%s_%s", qtype, Counter);
		if (isdigit((unsigned char)*Value)) {
			if (strstr(Value, ".")) {
				double val = strtod(Value, NULL);
				metric_add_auto(mname, &val, DATATYPE_DOUBLE, carg);
			} else {
				int64_t val = strtoll(Value, NULL, 10);
				metric_add_auto(mname, &val, DATATYPE_INT, carg);
			}
		}
		return;
	}

	/* THREADS / PROFILE (упрощённо: только числовые поля с метками) */
	for (unsigned int i = 0; i < num_fields; i++) {
		char *res = (char*)row->fields[i];
		if (!res)
			continue;

		snprintf(mname, sizeof(mname), "sphinxsearch_%s_col%u", qtype, i);
		if (isdigit((unsigned char)*res)) {
			if (strstr(res, ".")) {
				double val = strtod(res, NULL);
				metric_add_auto(mname, &val, DATATYPE_DOUBLE, carg);
			} else if (strstr(res, "ms")) {
				double val = strtod(res, NULL) / 1000;
				metric_add_auto(mname, &val, DATATYPE_DOUBLE, carg);
			} else if (strstr(res, "us")) {
				double val = strtod(res, NULL) / 1000000;
				metric_add_auto(mname, &val, DATATYPE_DOUBLE, carg);
			} else {
				int64_t val = strtoll(res, NULL, 10);
				metric_add_auto(mname, &val, DATATYPE_INT, carg);
			}
		}
	}
}



void sphinxsearch_run_all_await(context_arg *carg)
{
	sphinx_ctx_t ctx = { .carg = carg, .qtype = NULL };

	ctx.qtype = "variables";
	mysql2_await_query(mysql_conn_from_carg(carg), "SHOW VARIABLES", sphinxsearch_row_cb, &ctx);

	ctx.qtype = "status";
	mysql2_await_query(mysql_conn_from_carg(carg), "SHOW STATUS", sphinxsearch_row_cb, &ctx);

	ctx.qtype = "plan";
	mysql2_await_query(mysql_conn_from_carg(carg), "SHOW PLAN", sphinxsearch_row_cb, &ctx);

	ctx.qtype = "profile";
	mysql2_await_query(mysql_conn_from_carg(carg), "SHOW PROFILE", sphinxsearch_row_cb, &ctx);

	ctx.qtype = "threads";
	mysql2_await_query(mysql_conn_from_carg(carg), "SHOW THREADS", sphinxsearch_row_cb, &ctx);
}

void mysql_run(void* arg)
{
	uint64_t unval = 0;
	uint64_t val = 1;
	context_arg *carg = arg;
	my_data *data = carg->data;

	/* Start connect on loop thread, run await in worker thread */
	if (!mysql2_start_connect(&data->conn, carg)) {
		puts("go away from mysql_run");
		metric_add_labels5("alligator_connect_ok", &unval, DATATYPE_UINT, carg,
						   "proto", "tcp", "type", "aggregator",
						   "host", carg->host, "key", carg->key, "parser", "mysql");
		metric_add_labels5("alligator_parser_status", &unval, DATATYPE_UINT, carg,
						   "proto", "tcp", "type", "aggregator",
						   "host", carg->host, "key", carg->key, "parser", "mysql");
		return;
	}

	metric_add_labels5("alligator_connect_ok", &val, DATATYPE_UINT, carg,
					   "proto", "tcp", "type", "aggregator",
					   "host", carg->host, "key", carg->key, "parser", "mysql");
	carg->parser_status = 1;

	if (carg->running)
		return;
	carg->running = 1;

	mysql_worker_arg_t *wa = calloc(1, sizeof(*wa));
	wa->carg = carg;
	wa->data = data;
	uv_thread_t th;
	uv_thread_create(&th, mysql_worker_thread, wa);

	metric_add_labels5("alligator_parser_status", &carg->parser_status, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql");
}



void mysql_timer(void *arg) {
	extern aconf* ac;
	usleep(ac->aggregator_startup * 1000);
	while ( 1 )
	{
		alligator_ht_foreach(ac->my_aggregator, mysql_run);
		usleep(ac->aggregator_repeat * 1000);
	}
}




char* mysql_client(context_arg* carg)
{
	if (!carg)
		return NULL;

	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);

	alligator_ht_insert(ac->my_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "mysql";
}

void mysql_client_del(context_arg* carg)
{
	if (!carg)
		return;

	alligator_ht_remove_existing(ac->my_aggregator, &(carg->node));
	carg_free(carg);
}

void mysql_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("mysql");
	actx->data = calloc(1, sizeof(my_data));
	my_data *data = actx->data;
	data->type = MY_TYPE_MYSQL;
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "mysql", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void sphinxsearch_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("sphinxsearch");
	actx->data = calloc(1, sizeof(my_data));
	my_data *data = actx->data;
	data->type = MY_TYPE_SPHINXSEARCH;
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "sphinxsearch", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
