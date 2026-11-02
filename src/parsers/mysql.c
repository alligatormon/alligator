#include "query/type.h"
#include "common/validator.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "metric/metric_types.h"
#include "metric/namespace.h"
#include "main.h"
#include "events/metrics.h"
#include "parsers/mysql2.h"
#include "common/rtime.h"
#include <ctype.h>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
#include <uv.h>
#define MY_TYPE_MYSQL 0
#define MY_TYPE_SPHINXSEARCH 1
#define MY_TYPE_PROXYSQL 2

typedef struct my_data {
	int type;   // MY_TYPE_MYSQL, MY_TYPE_SPHINXSEARCH or MY_TYPE_PROXYSQL
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
void proxysql_run_all_await(context_arg *carg);

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
		carg->exec_time = setrtime();
		if (data->type == MY_TYPE_MYSQL)
			mysql_run_all_await(carg);
		else if (data->type == MY_TYPE_SPHINXSEARCH)
			sphinxsearch_run_all_await(carg);
		else if (data->type == MY_TYPE_PROXYSQL)
			proxysql_run_all_await(carg);
		carg->exec_time_finish = setrtime();
	} else {
		carg->parser_status = 0;
		if (data->type == MY_TYPE_PROXYSQL) {
			double up = 0;
			namespace_metric_family_set(NULL, carg, "proxysql_up", METRIC_TYPE_GAUGE,
				"1 if the last ProxySQL admin scrape connected successfully, 0 otherwise.");
			metric_add_auto("proxysql_up", &up, DATATYPE_DOUBLE, carg);
		}
	}

	carg->close_time = setrtime();
	alligator_parser_ok_set(carg, carg->parser_status, "tcp", carg->host);
	aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);
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
		char **dbs = realloc(st->dbs, st->cap * sizeof(char*));
		st->dbs = dbs;
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
					metric_label_value_validator_normalizer(res, strlen(res));
					labels_hash_insert_nocache(hash, colname, res);
					break;
			}
		} else {
			metric_label_value_validator_normalizer(res, strlen(res));
			labels_hash_insert_nocache(hash, colname, res);
		}
		carglog(carg, L_TRACE, "\tmysql: '%s' field '%s': '%s'\n", carg->host, colname, res);
		free(res);
	}

	if (carg->ns)
		labels_hash_insert_nocache(hash, "dbname", carg->ns);

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
	carglog(carg, L_DEBUG, "mysql_queries_foreach\n");

	query_node *qn = arg;
	if (query_except_match(qn, carg->ns))
	{
		carglog(carg, L_DEBUG, "mysql: skip query datasource '%s', make '%s': except database '%s'\n", qn->datasource, qn->make, carg->ns ? carg->ns : "");
		return;
	}
	carglog(carg, L_DEBUG, "+-+-+-+-+-+-+-+\ninit query datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
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
			carglog(carg, L_DEBUG, "mysql: '%s' run queries for namespace '%s'\n", carg->host, carg->name);
		
			alligator_ht_foreach_arg(qds->hash, mysql_queries_foreach, carg);
		}
	}

	for (uint64_t i = 0; i < dbl.count; ++i) {
		const char *dbname = dbl.dbs[i];

		char name[1024];
		snprintf(name, sizeof(name), "%s/*", carg->name);
		query_ds *qds_wild = query_get(name);
		query_ds *qds_spec = NULL;
		if (carg->name) {
			snprintf(name, sizeof(name), "%s/%s", carg->name, dbname);
			qds_spec = query_get(name);
		}
		if (query_ds_except_db(qds_wild, dbname) && !qds_spec) {
			carglog(carg, L_DEBUG, "mysql: '%s' skip excepted database '%s'\n", carg->host, dbname);
			continue;
		}

		/* USE db */
		char use_query[256];
		snprintf(use_query, sizeof(use_query), "USE `%s`", dbname);
		mysql2_await_query(mysql_conn_from_carg(carg), use_query, NULL, NULL);

		context_arg db_carg = *carg;
		char url[1024];
		char ns[256];

		db_carg.url = url;
		db_carg.name = name;
		db_carg.ns = ns;
		db_carg.key = carg->key;
		db_carg.log_level = carg->log_level;
		db_carg.log_ch = carg->log_ch;

		snprintf(db_carg.url, sizeof(url), "%s/%s", carg->url, dbname);
		strlcpy(db_carg.ns, dbname, sizeof(ns));

		/* wildcard namespace: name */
		snprintf(name, sizeof(name), "%s/*", carg->name);
		query_ds *qds = query_get(name);
		if (qds) {
			carglog(carg, L_DEBUG, "mysql: '%s' run wildcard queries for namespace '%s'\n", carg->host, name);

			alligator_ht_foreach_arg(qds->hash, mysql_queries_foreach, &db_carg);
		}

		/* specific datasource: name/dbname */
		if (carg->name) {
			snprintf(db_carg.name, sizeof(name), "%s/%s", carg->name, dbname);
			qds = query_get(db_carg.name);
			if (qds) {
				carglog(carg, L_DEBUG, "mysql: '%s' exec queries for namespace '%s'\n", carg->host, db_carg.name);

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
		if (!row->fields[0] || !row->fields[1])
			return;
		/* fields point into the packet buffer and are not NUL-terminated */
		char Counter[255], Value[64];
		size_t cl = row->lengths[0] < sizeof(Counter) - 1 ? row->lengths[0] : sizeof(Counter) - 1;
		size_t vl = row->lengths[1] < sizeof(Value) - 1 ? row->lengths[1] : sizeof(Value) - 1;
		memcpy(Counter, row->fields[0], cl); Counter[cl] = 0;
		memcpy(Value, row->fields[1], vl); Value[vl] = 0;

		snprintf(mname, sizeof(mname), "sphinxsearch_%s_%s", qtype, Counter);
		if (isdigit((unsigned char)*Value)) {
			if (strstr(Value, ".")) {
				double val = strtod(Value, NULL);
				namespace_metric_family_set(NULL, carg, mname, METRIC_TYPE_GAUGE, "Sphinxsearch exported metric value.");
				metric_add_auto(mname, &val, DATATYPE_DOUBLE, carg);
			} else {
				int64_t val = strtoll(Value, NULL, 10);
				namespace_metric_family_set(NULL, carg, mname, METRIC_TYPE_GAUGE, "Sphinxsearch exported metric value.");
				metric_add_auto(mname, &val, DATATYPE_INT, carg);
			}
		}
		return;
	}

	/* THREADS / PROFILE (упрощённо: только числовые поля с метками) */
	for (unsigned int i = 0; i < num_fields; i++) {
		if (!row->fields[i])
			continue;
		/* fields point into the packet buffer and are not NUL-terminated */
		char res[64];
		size_t rl = row->lengths[i] < sizeof(res) - 1 ? row->lengths[i] : sizeof(res) - 1;
		memcpy(res, row->fields[i], rl); res[rl] = 0;

		snprintf(mname, sizeof(mname), "sphinxsearch_%s_col%u", qtype, i);
		if (isdigit((unsigned char)*res)) {
			if (strstr(res, ".")) {
				double val = strtod(res, NULL);
				namespace_metric_family_set(NULL, carg, mname, METRIC_TYPE_GAUGE, "Sphinxsearch exported metric value.");
				metric_add_auto(mname, &val, DATATYPE_DOUBLE, carg);
			} else if (strstr(res, "ms")) {
				double val = strtod(res, NULL) / 1000;
				namespace_metric_family_set(NULL, carg, mname, METRIC_TYPE_GAUGE, "Sphinxsearch exported metric value.");
				metric_add_auto(mname, &val, DATATYPE_DOUBLE, carg);
			} else if (strstr(res, "us")) {
				double val = strtod(res, NULL) / 1000000;
				namespace_metric_family_set(NULL, carg, mname, METRIC_TYPE_GAUGE, "Sphinxsearch exported metric value.");
				metric_add_auto(mname, &val, DATATYPE_DOUBLE, carg);
			} else {
				int64_t val = strtoll(res, NULL, 10);
				namespace_metric_family_set(NULL, carg, mname, METRIC_TYPE_GAUGE, "Sphinxsearch exported metric value.");
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

/* ProxySQL admin metrics (compatible with Percona proxysql_exporter names). */
typedef struct proxysql_ctx_t {
	context_arg *carg;
	const char *prefix;
	uint64_t rows;
} proxysql_ctx_t;

extern int env_struct_compare(const void *arg, const void *obj);

static int proxysql_env_flag(context_arg *carg, const char *key)
{
	if (!carg || !carg->env || !key)
		return 0;
	env_struct *es = alligator_ht_search(carg->env, env_struct_compare, (char *)key, tommy_strhash_u32(0, key));
	if (!es || !es->v || !es->v[0])
		return 0;
	const char *v = es->v;
	return (v[0] == '1' && v[1] == 0) || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on");
}

static size_t proxysql_copy_field(char *dst, size_t dstsz, mysql_row_t *row, int i)
{
	if (!dst || !dstsz || !row || i < 0 || i >= row->column_count || !row->fields[i]) {
		if (dst && dstsz)
			dst[0] = 0;
		return 0;
	}
	size_t n = row->lengths[i] < dstsz - 1 ? row->lengths[i] : dstsz - 1;
	memcpy(dst, row->fields[i], n);
	dst[n] = 0;
	metric_label_value_validator_normalizer(dst, n);
	return n;
}

static int proxysql_col_idx(mysql_row_t *row, const char *name)
{
	int i;
	if (!row || !row->column_names || !name)
		return -1;
	for (i = 0; i < row->column_count; i++) {
		if (row->column_names[i] && !strcasecmp(row->column_names[i], name))
			return i;
	}
	return -1;
}

static void proxysql_lower_inplace(char *s)
{
	for (; *s; s++)
		*s = (char)tolower((unsigned char)*s);
}

static int8_t proxysql_global_metric_type(const char *name)
{
	if (!strcmp(name, "client_connections_aborted") ||
	    !strcmp(name, "client_connections_created") ||
	    !strcmp(name, "proxysql_uptime") ||
	    !strcmp(name, "questions") ||
	    !strcmp(name, "slow_queries"))
		return METRIC_TYPE_COUNTER;
	if (!strcmp(name, "active_transactions") ||
	    !strcmp(name, "client_connections_connected") ||
	    !strcmp(name, "client_connections_non_idle"))
		return METRIC_TYPE_GAUGE;
	return METRIC_TYPE_UNTYPED;
}

static const char *proxysql_pool_metric_suffix(const char *col, char *buf, size_t bufsz)
{
	if (!strcmp(col, "connused"))
		return "conn_used";
	if (!strcmp(col, "connfree"))
		return "conn_free";
	if (!strcmp(col, "connok"))
		return "conn_ok";
	if (!strcmp(col, "connerr"))
		return "conn_err";
	if (!strcmp(col, "latency_ms") || !strcmp(col, "latency_us"))
		return "latency_us";
	snprintf(buf, bufsz, "%s", col);
	prometheus_metric_name_normalizer(buf, strlen(buf));
	return buf;
}

static int8_t proxysql_pool_metric_type(const char *suffix)
{
	if (!strcmp(suffix, "conn_ok") ||
	    !strcmp(suffix, "conn_err") ||
	    !strcmp(suffix, "queries") ||
	    !strcmp(suffix, "bytes_data_sent") ||
	    !strcmp(suffix, "bytes_data_recv"))
		return METRIC_TYPE_COUNTER;
	if (!strcmp(suffix, "status") ||
	    !strcmp(suffix, "conn_used") ||
	    !strcmp(suffix, "conn_free") ||
	    !strcmp(suffix, "latency_us"))
		return METRIC_TYPE_GAUGE;
	return METRIC_TYPE_UNTYPED;
}

static int proxysql_status_value(const char *status, int with_repl_lag)
{
	if (!strcasecmp(status, "ONLINE"))
		return 1;
	if (!strcasecmp(status, "SHUNNED"))
		return 2;
	if (!strcasecmp(status, "OFFLINE_SOFT"))
		return 3;
	if (!strcasecmp(status, "OFFLINE_HARD"))
		return 4;
	if (with_repl_lag && !strcasecmp(status, "SHUNNED_REPLICATION_LAG"))
		return 5;
	return 0;
}

static void proxysql_emit_double(context_arg *carg, const char *mname, int8_t mtype, const char *help, double val)
{
	namespace_metric_family_set(NULL, carg, (char *)mname, mtype, (char *)help);
	metric_add_auto((char *)mname, &val, DATATYPE_DOUBLE, carg);
}

static void proxysql_kv_cb(mysql_row_t *row, void *ud)
{
	proxysql_ctx_t *ctx = ud;
	context_arg *carg = ctx->carg;
	char name[255], value[128], mname[512];
	char *end = NULL;
	double val;
	int8_t mtype;

	if (!row || row->column_count < 2)
		return;
	if (!proxysql_copy_field(name, sizeof(name), row, 0))
		return;
	if (!proxysql_copy_field(value, sizeof(value), row, 1))
		return;

	proxysql_lower_inplace(name);
	prometheus_metric_name_normalizer(name, strlen(name));
	val = strtod(value, &end);
	if (end == value)
		return;

	ctx->rows++;
	snprintf(mname, sizeof(mname), "proxysql_%s_%s", ctx->prefix, name);
	if (!strcmp(ctx->prefix, "mysql_status"))
		mtype = proxysql_global_metric_type(name);
	else
		mtype = METRIC_TYPE_GAUGE;

	proxysql_emit_double(carg, mname, mtype, "ProxySQL admin stats metric.", val);
}

static void proxysql_pool_cb(mysql_row_t *row, void *ud)
{
	proxysql_ctx_t *ctx = ud;
	context_arg *carg = ctx->carg;
	char hostgroup[64], srv_host[256], srv_port[32], endpoint[320];
	char col[128], valbuf[128], suffix_buf[128], mname[512];
	const char *suffix;
	int i, hg_i, host_i, port_i;
	double val;
	char *end = NULL;

	if (!row)
		return;

	hg_i = proxysql_col_idx(row, "hostgroup");
	host_i = proxysql_col_idx(row, "srv_host");
	port_i = proxysql_col_idx(row, "srv_port");
	if (hg_i < 0 || host_i < 0 || port_i < 0)
		return;

	proxysql_copy_field(hostgroup, sizeof(hostgroup), row, hg_i);
	proxysql_copy_field(srv_host, sizeof(srv_host), row, host_i);
	proxysql_copy_field(srv_port, sizeof(srv_port), row, port_i);
	if (!hostgroup[0] || !srv_host[0] || !srv_port[0])
		return;
	snprintf(endpoint, sizeof(endpoint), "%s:%s", srv_host, srv_port);

	for (i = 0; i < row->column_count; i++) {
		if (i == hg_i || i == host_i || i == port_i)
			continue;
		if (!row->column_names || !row->column_names[i])
			continue;
		strlcpy(col, row->column_names[i], sizeof(col));
		proxysql_lower_inplace(col);
		if (!strcmp(col, "hostgroup") || !strcmp(col, "srv_host") || !strcmp(col, "srv_port"))
			continue;
		if (!proxysql_copy_field(valbuf, sizeof(valbuf), row, i))
			continue;

		if (!strcmp(col, "status")) {
			val = (double)proxysql_status_value(valbuf, 1);
		} else {
			val = strtod(valbuf, &end);
			if (end == valbuf)
				continue;
		}

		ctx->rows++;
		suffix = proxysql_pool_metric_suffix(col, suffix_buf, sizeof(suffix_buf));
		snprintf(mname, sizeof(mname), "proxysql_connection_pool_%s", suffix);
		namespace_metric_family_set(NULL, carg, mname, proxysql_pool_metric_type(suffix),
			"ProxySQL stats_mysql_connection_pool metric.");
		metric_add_labels2(mname, &val, DATATYPE_DOUBLE, carg, "hostgroup", hostgroup, "endpoint", endpoint);
	}
}

static void proxysql_connlist_cb(mysql_row_t *row, void *ud)
{
	proxysql_ctx_t *ctx = ud;
	context_arg *carg = ctx->carg;
	char cli_host[256], countbuf[64];
	int count_i, host_i;
	double val;
	char *end = NULL;

	if (!row)
		return;
	count_i = proxysql_col_idx(row, "connection_count");
	host_i = proxysql_col_idx(row, "cli_host");
	if (count_i < 0)
		count_i = 0;
	if (host_i < 0)
		host_i = 1;
	if (!proxysql_copy_field(countbuf, sizeof(countbuf), row, count_i))
		return;
	proxysql_copy_field(cli_host, sizeof(cli_host), row, host_i);
	if (!cli_host[0])
		return;
	val = strtod(countbuf, &end);
	if (end == countbuf)
		return;

	ctx->rows++;
	namespace_metric_family_set(NULL, carg, "proxysql_processlist_client_connection_list",
		METRIC_TYPE_GAUGE, "Total number of frontend connections per client host.");
	metric_add_labels("proxysql_processlist_client_connection_list", &val, DATATYPE_DOUBLE, carg,
		"client_host", cli_host);
}

static void proxysql_detailed_processlist_cb(mysql_row_t *row, void *ud)
{
	proxysql_ctx_t *ctx = ud;
	context_arg *carg = ctx->carg;
	char user[128], db[128], cli_host[256], hostgroup[64], countbuf[64];
	int user_i, db_i, host_i, hg_i, count_i;
	double val;
	char *end = NULL;

	if (!row)
		return;
	user_i = proxysql_col_idx(row, "user");
	db_i = proxysql_col_idx(row, "db");
	host_i = proxysql_col_idx(row, "cli_host");
	hg_i = proxysql_col_idx(row, "hostgroup");
	count_i = proxysql_col_idx(row, "count");
	if (user_i < 0 || db_i < 0 || host_i < 0 || hg_i < 0 || count_i < 0)
		return;

	proxysql_copy_field(user, sizeof(user), row, user_i);
	proxysql_copy_field(db, sizeof(db), row, db_i);
	proxysql_copy_field(cli_host, sizeof(cli_host), row, host_i);
	proxysql_copy_field(hostgroup, sizeof(hostgroup), row, hg_i);
	if (!user[0] || !cli_host[0] || !hostgroup[0])
		return;
	if (!proxysql_copy_field(countbuf, sizeof(countbuf), row, count_i))
		return;
	val = strtod(countbuf, &end);
	if (end == countbuf)
		return;

	ctx->rows++;
	namespace_metric_family_set(NULL, carg, "proxysql_processlist_detailed_client_connection_count",
		METRIC_TYPE_GAUGE, "Number of client connections per user, db, host and hostgroup.");
	metric_add_labels4("proxysql_processlist_detailed_client_connection_count", &val, DATATYPE_DOUBLE, carg,
		"user", user, "db", db[0] ? db : "", "client_host", cli_host, "hostgroup", hostgroup);
}

static void proxysql_runtime_cb(mysql_row_t *row, void *ud)
{
	proxysql_ctx_t *ctx = ud;
	context_arg *carg = ctx->carg;
	char hostgroup[64], hostname[256], port[32], gtid_port[32], endpoint[320];
	char col[128], valbuf[128], mname[512];
	int i, hg_i, host_i, port_i, gtid_i;
	double val;
	char *end = NULL;
	int8_t mtype;

	if (!row)
		return;

	hg_i = proxysql_col_idx(row, "hostgroup_id");
	host_i = proxysql_col_idx(row, "hostname");
	port_i = proxysql_col_idx(row, "port");
	gtid_i = proxysql_col_idx(row, "gtid_port");
	if (hg_i < 0 || host_i < 0 || port_i < 0)
		return;

	proxysql_copy_field(hostgroup, sizeof(hostgroup), row, hg_i);
	proxysql_copy_field(hostname, sizeof(hostname), row, host_i);
	proxysql_copy_field(port, sizeof(port), row, port_i);
	if (!hostgroup[0] || !hostname[0] || !port[0])
		return;
	if (gtid_i >= 0)
		proxysql_copy_field(gtid_port, sizeof(gtid_port), row, gtid_i);
	else
		gtid_port[0] = 0;
	snprintf(endpoint, sizeof(endpoint), "%s:%s", hostname, port);

	for (i = 0; i < row->column_count; i++) {
		if (i == hg_i || i == host_i || i == port_i || i == gtid_i)
			continue;
		if (!row->column_names || !row->column_names[i])
			continue;
		strlcpy(col, row->column_names[i], sizeof(col));
		proxysql_lower_inplace(col);
		if (!strcmp(col, "hostgroup_id") || !strcmp(col, "hostname") ||
		    !strcmp(col, "port") || !strcmp(col, "gtid_port"))
			continue;
		if (!proxysql_copy_field(valbuf, sizeof(valbuf), row, i))
			continue;

		if (!strcmp(col, "status")) {
			val = (double)proxysql_status_value(valbuf, 0);
			mtype = METRIC_TYPE_GAUGE;
		} else {
			val = strtod(valbuf, &end);
			if (end == valbuf)
				continue;
			if (!strcmp(col, "weight") || !strcmp(col, "compression") ||
			    !strcmp(col, "max_connections") || !strcmp(col, "max_replication_lag") ||
			    !strcmp(col, "use_ssl") || !strcmp(col, "max_latency_ms"))
				mtype = METRIC_TYPE_GAUGE;
			else
				mtype = METRIC_TYPE_UNTYPED;
		}

		ctx->rows++;
		prometheus_metric_name_normalizer(col, strlen(col));
		snprintf(mname, sizeof(mname), "proxysql_runtime_servers_%s", col);
		namespace_metric_family_set(NULL, carg, mname, mtype, "ProxySQL runtime_mysql_servers metric.");
		metric_add_labels3(mname, &val, DATATYPE_DOUBLE, carg,
			"hostgroup", hostgroup, "endpoint", endpoint, "gtid_port", gtid_port);
	}
}

static void proxysql_info_cb(mysql_row_t *row, void *ud)
{
	proxysql_ctx_t *ctx = ud;
	context_arg *carg = ctx->carg;
	char version[128];
	double zero = 0;

	if (!row || row->column_count < 1)
		return;
	if (!proxysql_copy_field(version, sizeof(version), row, 0))
		return;
	if (!version[0])
		return;

	ctx->rows++;
	namespace_metric_family_set(NULL, carg, "proxysql_info", METRIC_TYPE_GAUGE, "ProxySQL info.");
	metric_add_labels("proxysql_info", &zero, DATATYPE_DOUBLE, carg, "version", version);
}

void proxysql_run_all_await(context_arg *carg)
{
	proxysql_ctx_t ctx = { .carg = carg, .prefix = NULL, .rows = 0 };
	double up = 1;
	int ok = 1;

	namespace_metric_family_set(NULL, carg, "proxysql_up", METRIC_TYPE_GAUGE,
		"1 if the last ProxySQL admin scrape connected successfully, 0 otherwise.");
	metric_add_auto("proxysql_up", &up, DATATYPE_DOUBLE, carg);

	/* Defaults match proxysql_exporter: mysql_status, connection_pool, connection_list (+ info). */
	ctx.prefix = "mysql_status";
	ctx.rows = 0;
	mysql2_await_query(mysql_conn_from_carg(carg),
		"SELECT Variable_Name, Variable_Value FROM stats_mysql_global",
		proxysql_kv_cb, &ctx);
	if (!ctx.rows)
		ok = 0;

	ctx.prefix = "connection_pool";
	mysql2_await_query(mysql_conn_from_carg(carg),
		"SELECT hostgroup, srv_host, srv_port, * FROM stats_mysql_connection_pool",
		proxysql_pool_cb, &ctx);

	ctx.prefix = "processlist";
	mysql2_await_query(mysql_conn_from_carg(carg),
		"SELECT COUNT(cli_host) as connection_count, cli_host FROM stats_mysql_processlist GROUP BY cli_host",
		proxysql_connlist_cb, &ctx);

	/* Optional collectors (exporter defaults off). Enable with env=collect.<name>:1 */
	if (proxysql_env_flag(carg, "collect.detailed_processlist") ||
	    proxysql_env_flag(carg, "collect.detailed.stats_mysql_processlist")) {
		mysql2_await_query(mysql_conn_from_carg(carg),
			"SELECT user, db, cli_host, hostgroup, COUNT(*) as count FROM stats_mysql_processlist GROUP BY user, db, cli_host, hostgroup",
			proxysql_detailed_processlist_cb, &ctx);
	}

	if (proxysql_env_flag(carg, "collect.runtime_mysql_servers")) {
		mysql2_await_query(mysql_conn_from_carg(carg),
			"SELECT hostgroup_id, hostname, port, gtid_port, * FROM runtime_mysql_servers",
			proxysql_runtime_cb, &ctx);
	}

	if (proxysql_env_flag(carg, "collect.stats_memory_metrics")) {
		ctx.prefix = "stats_memory";
		mysql2_await_query(mysql_conn_from_carg(carg),
			"SELECT Variable_Name, Variable_Value FROM stats_memory_metrics",
			proxysql_kv_cb, &ctx);
	}

	ctx.rows = 0;
	mysql2_await_query(mysql_conn_from_carg(carg),
		"SELECT variable_value FROM global_variables WHERE variable_name = 'admin-version'",
		proxysql_info_cb, &ctx);
	if (!ctx.rows)
		ok = 0;

	carg->parser_status = ok ? 1 : 0;
}

void mysql_run(void* arg)
{
	uint64_t unval = 0;
	context_arg *carg = arg;
	my_data *data = carg->data;

	if (!data)
		return;

	/* ProxySQL admin listens on 6032; generic mysql:// default is 3306. */
	if (data->type == MY_TYPE_PROXYSQL && !carg->port[0])
		strlcpy(carg->port, "6032", sizeof(carg->port));

	/* Start connect on loop thread, run await in worker thread */
	if (!mysql2_start_connect(&data->conn, carg)) {
		carglog(carg, L_DEBUG, "go away from mysql_run\n");
		namespace_metric_family_set(NULL, carg, "alligator_session_connect_ok", METRIC_TYPE_GAUGE, "1 if the last backend connection attempt succeeded, 0 otherwise.");
		namespace_metric_family_set(NULL, carg, "alligator_parser_ok", METRIC_TYPE_GAUGE, "1 if the last parser run succeeded, 0 otherwise.");
		if (data->type == MY_TYPE_PROXYSQL) {
			double up = 0;
			namespace_metric_family_set(NULL, carg, "proxysql_up", METRIC_TYPE_GAUGE,
				"1 if the last ProxySQL admin scrape connected successfully, 0 otherwise.");
			metric_add_auto("proxysql_up", &up, DATATYPE_DOUBLE, carg);
		}
		alligator_parser_ok_set(carg, unval, "tcp", carg->host);
		aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);
		return;
	}

	namespace_metric_family_set(NULL, carg, "alligator_session_connect_ok", METRIC_TYPE_GAUGE, "1 if the last backend connection attempt succeeded, 0 otherwise.");
	carg->parser_status = 1;

	if (carg->running)
		return;
	carg->running = 1;

	mysql_worker_arg_t *wa = calloc(1, sizeof(*wa));
	wa->carg = carg;
	wa->data = data;
	uv_thread_t th;
	if (uv_thread_create(&th, mysql_worker_thread, wa)) {
		carg->running = 0;
		free(wa);
		return;
	}
	pthread_detach(th);

	namespace_metric_family_set(NULL, carg, "alligator_parser_ok", METRIC_TYPE_GAUGE, "1 if the last parser run succeeded, 0 otherwise.");
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




static my_data *my_data_new(int type)
{
	my_data *data = calloc(1, sizeof(*data));
	if (!data)
		return NULL;
	data->type = type;
	data->conn = NULL;
	return data;
}

char* mysql_client(context_arg* carg)
{
	if (!carg)
		return NULL;

	/* Keep smart_aggregator key (includes port/path) when already set. */
	if (!carg->key) {
		carg->key = malloc(255);
		snprintf(carg->key, 255, "%s", carg->host);
	}

	/* Per-carg state so multiple mysql/proxysql/sphinx targets do not share one TCP conn. */
	{
		my_data *shared = carg->data;
		my_data *own = my_data_new(shared ? shared->type : MY_TYPE_MYSQL);
		if (own)
			carg->data = own;
	}

	alligator_ht_insert(ac->my_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "mysql";
}

void mysql_client_del(context_arg* carg)
{
	if (!carg)
		return;

	aggregators_ht_unlink(carg);
	alligator_ht_remove_existing(ac->my_aggregator, &(carg->node));
	/* Do not free my_data/conn here: a worker thread may still hold them. */
	carg->data = NULL;
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

void proxysql_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("proxysql");
	actx->data = calloc(1, sizeof(my_data));
	my_data *data = actx->data;
	data->type = MY_TYPE_PROXYSQL;
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "proxysql", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
