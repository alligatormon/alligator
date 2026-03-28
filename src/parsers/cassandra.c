#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include "query/type.h"
#include "common/validator.h"
#include "common/logs.h"
#include "parsers/cassandra2.h"
#include "main.h"

typedef struct cassandra_data {
	cassandra_conn_t *conn;
} cassandra_data;

typedef struct cassandra_db_list_t {
	char **dbs;
	uint64_t count;
	uint64_t cap;
} cassandra_db_list_t;

typedef struct cassandra_exec_ctx_t {
	context_arg *carg;
	query_node *qn;
} cassandra_exec_ctx_t;

typedef struct cassandra_worker_arg_t {
	context_arg *carg;
	cassandra_data *data;
} cassandra_worker_arg_t;

enum {
	CASS_T_ASCII = 0x0001,
	CASS_T_BIGINT = 0x0002,
	CASS_T_BLOB = 0x0003,
	CASS_T_BOOLEAN = 0x0004,
	CASS_T_COUNTER = 0x0005,
	CASS_T_DECIMAL = 0x0006,
	CASS_T_DOUBLE = 0x0007,
	CASS_T_FLOAT = 0x0008,
	CASS_T_INT = 0x0009,
	CASS_T_TEXT = 0x000a,
	CASS_T_TIMESTAMP = 0x000b,
	CASS_T_UUID = 0x000c,
	CASS_T_VARCHAR = 0x000d,
	CASS_T_VARINT = 0x000e,
	CASS_T_SMALLINT = 0x0013,
	CASS_T_TINYINT = 0x0014,
};

static uint32_t cass_be32u(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t cass_be64u(const uint8_t *p) {
	return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
		   ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
		   ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
		   ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

static int32_t cass_be32s(const uint8_t *p) {
	return (int32_t)cass_be32u(p);
}

static int64_t cass_be64s(const uint8_t *p) {
	return (int64_t)cass_be64u(p);
}

static double cass_ieee_double_be(const uint8_t *p) {
	uint64_t u = cass_be64u(p);
	double d;
	memcpy(&d, &u, sizeof(d));
	return d;
}

static float cass_ieee_float_be(const uint8_t *p) {
	uint32_t u = cass_be32u(p);
	float f;
	memcpy(&f, &u, sizeof(f));
	return f;
}

static int64_t cass_be_signed_n(const uint8_t *vp, int nbytes) {
	int64_t v = 0;
	for (int i = 0; i < nbytes; i++)
		v = (v << 8) | vp[i];
	int const bits = nbytes * 8;
	if (bits > 0 && bits < 64)
		v = (v << (64 - bits)) >> (64 - bits);
	return v;
}

static double cass_decimal_to_double(const uint8_t *p, uint64_t len) {
	if (len < 8)
		return 0.0;
	int32_t scale = cass_be32s(p);
	int32_t nbytes = cass_be32s(p + 4);
	if (nbytes < 0 || nbytes > 20 || (uint64_t)nbytes + 8 > len)
		return 0.0;
	int64_t unscaled = cass_be_signed_n(p + 8, nbytes);
	return (double)unscaled * pow(10.0, -(double)scale);
}

static char *cassandra_cell_to_label_str(const uint8_t *data, uint64_t len, uint16_t t) {
	char buf[96];

	if (!data || len == 0)
		return strdup("");

	switch (t) {
	case CASS_T_DOUBLE:
		if (len == 8) {
			snprintf(buf, sizeof(buf), "%.17g", cass_ieee_double_be(data));
			return strdup(buf);
		}
		break;
	case CASS_T_FLOAT:
		if (len == 4) {
			snprintf(buf, sizeof(buf), "%.9g", (double)cass_ieee_float_be(data));
			return strdup(buf);
		}
		break;
	case CASS_T_INT:
		if (len == 4) {
			snprintf(buf, sizeof(buf), "%" PRId32, cass_be32s(data));
			return strdup(buf);
		}
		break;
	case CASS_T_SMALLINT:
		if (len == 2) {
			int16_t v = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
			snprintf(buf, sizeof(buf), "%d", (int)v);
			return strdup(buf);
		}
		break;
	case CASS_T_TINYINT:
		if (len == 1) {
			snprintf(buf, sizeof(buf), "%d", (int8_t)data[0]);
			return strdup(buf);
		}
		break;
	case CASS_T_BIGINT:
	case CASS_T_COUNTER:
	case CASS_T_TIMESTAMP:
		if (len == 8) {
			snprintf(buf, sizeof(buf), "%" PRId64, cass_be64s(data));
			return strdup(buf);
		}
		break;
	case CASS_T_BOOLEAN:
		if (len == 1)
			return strdup(data[0] ? "true" : "false");
		break;
	case CASS_T_DECIMAL:
		snprintf(buf, sizeof(buf), "%.17g", cass_decimal_to_double(data, len));
		return strdup(buf);
	default:
		break;
	}
	return strndup((const char *)data, len);
}

static void cassandra_qf_set_from_cell(query_field *qf, const uint8_t *data, uint64_t len, uint16_t t) {
	if (!data || !len) {
		qf->type = DATATYPE_NONE;
		return;
	}

	switch (t) {
	case CASS_T_DOUBLE:
		if (len == 8) {
			qf->d = cass_ieee_double_be(data);
			qf->type = DATATYPE_DOUBLE;
		}
		return;
	case CASS_T_FLOAT:
		if (len == 4) {
			qf->d = (double)cass_ieee_float_be(data);
			qf->type = DATATYPE_DOUBLE;
		}
		return;
	case CASS_T_DECIMAL:
		qf->d = cass_decimal_to_double(data, len);
		qf->type = DATATYPE_DOUBLE;
		return;
	case CASS_T_INT:
	case CASS_T_SMALLINT:
	case CASS_T_TINYINT:
	case CASS_T_BOOLEAN: {
		int64_t v = 0;
		if (t == CASS_T_INT && len == 4)
			v = cass_be32s(data);
		else if (t == CASS_T_SMALLINT && len == 2)
			v = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
		else if (t == CASS_T_TINYINT && len == 1)
			v = (int8_t)data[0];
		else if (t == CASS_T_BOOLEAN && len >= 1)
			v = data[0] ? 1 : 0;
		else
			break;
		qf->i = v;
		qf->type = DATATYPE_INT;
		return;
	}
	case CASS_T_BIGINT:
	case CASS_T_COUNTER:
	case CASS_T_TIMESTAMP:
		if (len == 8) {
			qf->i = cass_be64s(data);
			qf->type = DATATYPE_INT;
		}
		return;
	default: {
		char *tmp = strndup((const char *)data, len);
		if (!tmp)
			return;
		qf->i = strtoll(tmp, NULL, 10);
		qf->type = DATATYPE_INT;
		free(tmp);
		return;
	}
	}
}

static inline cassandra_conn_t *cassandra_conn_from_carg(context_arg *carg) {
	cassandra_data *data = carg ? (cassandra_data*)carg->data : NULL;
	return data ? data->conn : NULL;
}

static void cassandra_db_list_cb(cassandra_row_t *row, void *ud) {
	cassandra_db_list_t *st = ud;
	if (!row || row->column_count < 1 || !row->fields[0])
		return;
	if (st->count == st->cap) {
		st->cap = st->cap ? st->cap * 2 : 8;
		st->dbs = realloc(st->dbs, st->cap * sizeof(char*));
	}
	st->dbs[st->count++] = strndup((const char*)row->fields[0], row->lengths[0]);
}

static void cassandra_exec_cb(cassandra_row_t *row, void *ud) {
	cassandra_exec_ctx_t *ctx = ud;
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
		char *colname = NULL;
		char colname_fb[64];

		if (row->column_names && row->column_names[i] && row->column_names[i][0])
			colname = row->column_names[i];
		else {
			snprintf(colname_fb, sizeof(colname_fb), "col%d", i);
			colname = colname_fb;
		}
		uint16_t t = row->column_types ? row->column_types[i] : 0;

		query_field *qf = query_field_get(qn->qf_hash, colname);
		if (qf) {
			cassandra_qf_set_from_cell(qf, row->fields[i], row->lengths[i], t);
		} else {
			char *res = cassandra_cell_to_label_str(row->fields[i], row->lengths[i], t);
			if (!res)
				continue;
			carglog(carg, L_DEBUG, "cassandra_exec_cb colname %s res %s\n", colname, res);
			metric_label_value_validator_normalizer(res, strlen(res));
			labels_hash_insert_nocache(hash, colname, res);
			free(res);
		}
	}

	qn->labels = hash;
	qn->carg = carg;
	query_set_values(qn);
	labels_hash_free(hash);
	qn->labels = NULL;
}

static void cassandra_run_single_query(context_arg *carg, query_node *qn) {
	cassandra_exec_ctx_t ctx = { carg, qn };
	cassandra2_await_query(cassandra_conn_from_carg(carg), qn->expr, cassandra_exec_cb, &ctx);
}

void cassandra_queries_foreach(void *funcarg, void* arg) {
	context_arg *carg = (context_arg*)funcarg;
	query_node *qn = arg;
	cassandra_run_single_query(carg, qn);
}

void cassandra_run_all_await(context_arg *carg) {
	cassandra_db_list_t dbl = {0};
	cassandra2_await_query(cassandra_conn_from_carg(carg), "SELECT keyspace_name FROM system_schema.keyspaces", cassandra_db_list_cb, &dbl);

	if (carg->name) {
		query_ds *qds = query_get(carg->name);
		if (qds)
			alligator_ht_foreach_arg(qds->hash, cassandra_queries_foreach, carg);
	}

	for (uint64_t i = 0; i < dbl.count; ++i) {
		const char *dbname = dbl.dbs[i];
		if (!cassandra2_use_keyspace(cassandra_conn_from_carg(carg), dbname))
			continue;

		context_arg db_carg = *carg;
		char name[1024];
		char ns[256];
		db_carg.name = name;
		db_carg.ns = ns;
		db_carg.key = carg->key;
		db_carg.data = carg->data;
		strlcpy(ns, dbname, sizeof(ns));

		snprintf(name, sizeof(name), "%s/*", carg->name);
		query_ds *qds = query_get(name);
		if (qds)
			alligator_ht_foreach_arg(qds->hash, cassandra_queries_foreach, &db_carg);

		snprintf(name, sizeof(name), "%s/%s", carg->name, dbname);
		qds = query_get(name);
		if (qds)
			alligator_ht_foreach_arg(qds->hash, cassandra_queries_foreach, &db_carg);
	}

	for (uint64_t i = 0; i < dbl.count; ++i)
		free(dbl.dbs[i]);
	free(dbl.dbs);
}

static void cassandra_worker_thread(void *arg) {
	cassandra_worker_arg_t *wa = arg;
	context_arg *carg = wa->carg;
	cassandra_data *data = wa->data;
	cassandra_conn_t *conn = data->conn;

	while (conn && !cassandra2_is_ready(conn) && !cassandra2_is_failed(conn))
		uv_sleep(100);

	if (conn && cassandra2_is_ready(conn)) {
		cassandra_run_all_await(carg);
	}
	else if (conn && cassandra2_is_failed(conn)) {
		// downgrades v5->v4 after failure.
		if (cassandra2_start_connect(&data->conn, carg)) {
			conn = data->conn;
			while (conn && !cassandra2_is_ready(conn) && !cassandra2_is_failed(conn))
				uv_sleep(100);
			if (conn && cassandra2_is_ready(conn))
				cassandra_run_all_await(carg);
			else
				carg->parser_status = 0;
		}
		else {
			carg->parser_status = 0;
		}
	}
	else {
		carg->parser_status = 0;
	}

	carg->running = 0;
	free(wa);
}

void cassandra_run(void* arg) {
	uint64_t unval = 0;
	uint64_t val = 1;
	context_arg *carg = arg;
	cassandra_data *data = carg->data;

	if (!cassandra2_start_connect(&data->conn, carg)) {
		metric_add_labels5("alligator_connect_ok", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "cassandra");
		metric_add_labels5("alligator_parser_status", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "cassandra");
		return;
	}

	metric_add_labels5("alligator_connect_ok", &val, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "cassandra");
	carg->parser_status = 1;

	if (carg->running)
		return;
	carg->running = 1;

	cassandra_worker_arg_t *wa = calloc(1, sizeof(*wa));
	wa->carg = carg;
	wa->data = data;
	uv_thread_t th;
	if (uv_thread_create(&th, cassandra_worker_thread, wa)) {
		carg->running = 0;
		free(wa);
		return;
	}
	pthread_detach(th);
	metric_add_labels5("alligator_parser_status", &carg->parser_status, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "cassandra");
}

void cassandra_timer(uv_timer_t* handle) {
	(void)handle;
	alligator_ht_foreach(ac->cass_aggregator, cassandra_run);
}

void cassandra_client_handler() {
	uv_timer_init(ac->loop, &ac->cass_timer);
	uv_timer_start(&ac->cass_timer, cassandra_timer, ac->aggregator_startup, ac->aggregator_repeat);
}

char* cassandra_client(context_arg* carg) {
	if (!carg)
		return NULL;
	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);
	alligator_ht_insert(ac->cass_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "cassandra";
}

void cassandra_client_del(context_arg* carg) {
	if (!carg)
		return;
	alligator_ht_remove_existing(ac->cass_aggregator, &(carg->node));
	carg_free(carg);
}

void cassandra_parser_push() {
	aggregate_context *actx = calloc(1, sizeof(*actx));
	actx->key = strdup("cassandra");
	actx->data = calloc(1, sizeof(cassandra_data));
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);
	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "cassandra", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
