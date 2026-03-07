#include <stdio.h>
#include <stdlib.h>
#include <libpq-fe.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <query/type.h>
#include "common/validator.h"
#include "common/logs.h"
#include "dstructures/queue.h"
#include <main.h>


#define PG_TYPE_PG 0
#define PG_TYPE_PGBOUNCER 1
#define PG_TYPE_ODYSSEY 2
#define PG_TYPE_PGPOOL 3

typedef struct pg_data {
	PGconn *conn;
	int type;
} pg_data;


postgresql_query_ctx* postgresql_query_ctx_init(context_arg *carg, char *query, void callback(PGresult*, query_node*, context_arg*, char*), char *database_class, query_node *qn) {
	postgresql_query_ctx *pqctx = calloc(1, sizeof(*pqctx));
	pqctx->callback = callback;
	pqctx->query = query;
	pqctx->qn = qn;
	pqctx->database_class = database_class;
	return pqctx;
}


void postgresql_query_init(PGconn *conn, char *query, query_node *qn, context_arg *carg, void callback(PGresult*, query_node*, context_arg*, char*), char *database_class)
{
	if (!carg->pg_queue)
		carg->pg_queue = queue_init();
	postgresql_query_ctx *pqctx = postgresql_query_ctx_init(carg, query, callback, database_class, qn);
	printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"postgresql query init\", \"query\": \"%s\"}\n", carg->fd, carg->key, query);

	queue_push(carg->pg_queue, pqctx);

}

void on_handle_closed(uv_handle_t* handle) {
    context_arg *carg = handle->data;

    pg_data *data = carg->data;
    if (data && data->conn) {
		if (carg->data_lock) {
			printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"pqfinish\", \"connection\": \"%p\"}\n", carg->fd, carg->key, data->conn);
			PQfinish(data->conn);
			data->conn = NULL;
		}
    }

	printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"closed socket\", \"connection\": \"%p\"}\n", carg->fd, carg->key, data->conn);

	if (carg->parental_carg) {
		if (carg->parental_carg->lock) {
			--carg->parental_carg->lock;
		}

	}

    if (carg->data_lock) {
        free(carg->url);
        free(carg->ns);
        free(carg->name);
        free(carg);
    }

    free(handle);
}

void run_close(context_arg *carg) {
	if (uv_is_closing((uv_handle_t*)carg->dynamic_socket)) {
		printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"run close\", \"function\": \"%s\"}\n", carg->fd, carg->key, "alreading closing");
		return;
	}

	uv_close((uv_handle_t*)carg->dynamic_socket, on_handle_closed);
}

void postgresql_run(void* arg);

void postgresql_write(PGresult* r, query_node *qn, context_arg *carg, char *database_class)
{
	if (carg->log_level > 1)
	{
		printf("pg status %d/%d\n", PQresultStatus(r), PGRES_TUPLES_OK);
		printf("pg tuples %d, field %d\n", PQntuples(r), PQnfields(r));
	}

	uint64_t i;
	uint64_t j;

	for (i=0; i<PQntuples(r); ++i)
	{
		alligator_ht *hash = alligator_ht_init(NULL);

		if (carg->ns)
			labels_hash_insert_nocache(hash, "dbname", carg->ns);

		for (j=0; j<PQnfields(r); ++j)
		{
			char *colname = PQfname(r, j);
			if (PQftype(r, j) == 16) // BOOLOID
			{
				char *res = PQgetvalue(r, i, j);
				query_field *qf = query_field_get(qn->qf_hash, colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tcolname: '%s', value '%s'\n", colname, res);
					if (!strncmp(res, "t", 0))
						qf->i = 1;
					else
						qf->i = 0;

					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", colname, res);
					labels_hash_insert_nocache(hash, colname, res);
				}
			}
			else if (PQftype(r, j) == 700 || PQftype(r, j) == 701) // FLOAT4OID FLOAT8OID
			{
				char *res = PQgetvalue(r, i, j);
				query_field *qf = query_field_get(qn->qf_hash, colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tcolname: '%s', value '%s'\n", colname, res);
					qf->d = strtod(res, NULL);

					qf->type = DATATYPE_DOUBLE;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", colname, res);
					labels_hash_insert_nocache(hash, colname, res);
				}
			}
			else
			{
				char *res = PQgetvalue(r, i, j);
				uint64_t res_len = PQgetlength(r, i, j);

				query_field *qf = query_field_get(qn->qf_hash, colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tcolname: '%s', value '%s'\n", colname, res);
					qf->i = strtoll(res, NULL, 10);
					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", colname, res);
					metric_label_value_validator_normalizer(res, res_len);
					labels_hash_insert_nocache(hash, colname, res);
				}
			}
		}
		qn->labels = hash;
		qn->carg = carg;
		query_set_values(qn);
		labels_hash_free(hash);
		qn->labels = NULL;
	}
}



void pg_poll_event(uv_poll_t* handle, int status, int events);
void postgresql_query_run(context_arg *carg);


void update_poll_state(context_arg *carg) {
    if (uv_is_closing((uv_handle_t*)carg->dynamic_socket)) return;

    pg_data *data = carg->data;
    PostgresPollingStatusType poll_status = PQconnectPoll(data->conn);
    int uv_events = 0;

    switch (poll_status) {
        case PGRES_POLLING_ACTIVE:
			printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"update_poll_state\", \"set\": \"active\"}\n", carg->fd, carg->key);
            return update_poll_state(carg); 
			//break;

        case PGRES_POLLING_READING:
            uv_events = UV_READABLE;
			printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"update_poll_state\", \"set\": \"reading\"}\n", carg->fd, carg->key);

            break;

        case PGRES_POLLING_WRITING:
            uv_events = UV_WRITABLE;
			printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"update_poll_state\", \"set\": \"writing\"}\n", carg->fd, carg->key);
            break;

        case PGRES_POLLING_OK:
            PQsetnonblocking(data->conn, 1);
			printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"update_poll_state\", \"set\": \"connection ok\"}\n", carg->fd, carg->key);

            carg->parser_status = 1;
            postgresql_query_run(carg);
            return;

        case PGRES_POLLING_FAILED:
            carg->parser_status = 0;
			char *errmsg = PQerrorMessage(data->conn);
			printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"update poll state PGRES_POLLING_FAILED\", \"function\": \"%s\"}\n", carg->fd, carg->key, errmsg);
			run_close(carg);
            return;
    }
    uv_poll_start(carg->dynamic_socket, uv_events, pg_poll_event);
}

void pg_poll_event(uv_poll_t* handle, int status, int events) {
    context_arg *carg = (context_arg*)handle->data;

    if (uv_is_closing((uv_handle_t*)handle)) {
        return;
    }
	pg_data *data = carg->data;

    if (PQstatus(data->conn) != CONNECTION_OK) {
        update_poll_state(carg);
        return;
    }

    if (!PQconsumeInput(data->conn)) {
		printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event PQconsumeInput error\", \"function\": \"%s\"}\n", carg->fd, carg->key, PQerrorMessage(data->conn));
		run_close(carg);
        return;
    }

    while (!PQisBusy(data->conn)) {
        PGresult *res = PQgetResult(data->conn);

		if (res == NULL) {
            postgresql_query_ctx *finished_ctx = queue_pop(carg->pg_queue);
            if (finished_ctx) free(finished_ctx);

            if (!queue_empty(carg->pg_queue)) {
				printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"pg_poll_event\", \"set\": \"connection ok\"}\n", carg->fd, carg->key);
                postgresql_query_run(carg);
            } else {

                if (!uv_is_closing((uv_handle_t*)handle)) {
					printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event ALL QUERIES DONE\", \"function\": \"%s\"}\n", carg->fd, carg->key, "");
					run_close(carg);
                }
            }
            return;
        }

        ExecStatusType res_status = PQresultStatus(res);
        if (res_status == PGRES_TUPLES_OK || res_status == PGRES_COMMAND_OK) {
            postgresql_query_ctx *pqctx = queue_front(carg->pg_queue);
            if (pqctx && pqctx->callback) {
                pqctx->callback(res, pqctx->qn, carg, pqctx->database_class);
				PQclear(res);
            } else {
				printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event NO CALLBACK\", \"function\": \"%s\"}\n", carg->fd, carg->key, "");
				run_close(carg);
			}
        } else if (res_status == PGRES_FATAL_ERROR) {
			printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event PGRES_FATAL_ERROR\", \"function\": \"%s\"}\n", carg->fd, carg->key, PQresultErrorMessage(res));
			run_close(carg);
        }
    }
	printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event finish\", \"function\": \"%s\"}\n", carg->fd, carg->key, "");
}

void postgresql_received_databases(PGresult* res, query_node *qn, context_arg *carg, char *database_class)
{
	pg_data *data = carg->data;
	PGconn *conn = data->conn;

	if (carg->log_level > 1)
	{
		printf("pg status %d/%d\n", PQresultStatus(res), PGRES_TUPLES_OK);
		printf("pg tuples %d, field %d\n", PQntuples(res), PQnfields(res));
	}


	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		if (carg->log_level > 1)
			fprintf(stderr, "command get databases failed: %s", PQerrorMessage(conn));

		//char reason[255];
		//uint64_t reason_size = strlcpy(reason, PQerrorMessage(conn), 255);
		//uint64_t val = 1;
		//prometheus_metric_name_normalizer(reason, reason_size);
		//metric_add_labels2("postgresql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);
	}
	else
	{
		uint64_t i;
		uint64_t j;

		uint64_t children = 0;
		for (i=0; i<PQntuples(res); ++i)
		{
			context_arg *db_carg = calloc(1, sizeof(*db_carg));
			size_t url_len = strlen(carg->url) + 1024;
			char *url = malloc(url_len);
			size_t name_len = strlen(carg->name) + 1024;
			char *name = malloc(name_len);
			char *ns = malloc(1024);

			db_carg->parental_carg = carg;
			db_carg->data = NULL;
			db_carg->url = url;
			db_carg->key = carg->key;
			db_carg->name = name;
			db_carg->ns = ns;
			db_carg->log_level = carg->log_level;
			db_carg->data_lock = 1;
			db_carg->pg_queue = NULL;
			db_carg->threaded_loop_name = carg->threaded_loop_name;

			int wildcard = 0;
			snprintf(name, name_len - 1, "%s/*", carg->name);
			if (query_get(name))
				wildcard = 1;

			for (j=0; j<PQnfields(res); ++j)
			{
				char *resp = PQgetvalue(res, i, j);
				snprintf(db_carg->url, url_len - 1, "%s/%s", carg->url, resp);
				strlcpy(db_carg->ns, resp, 1024);

				snprintf(db_carg->name, name_len - 1, "%s/*", carg->name);
				if (wildcard)
				{
					if (carg->log_level > 1)
						printf("run wildcard queries\n");

					++children;
					postgresql_run(db_carg);
				}

				snprintf(db_carg->name, name_len - 1, "%s/%s", carg->name, resp);
				if (query_get(db_carg->name))
				{
					if (carg->log_level > 1)
						printf("exec queries database '%s'\n", resp);

					++children;
					postgresql_run(db_carg);
				}
			}
		}
		carg->lock = children;
		printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"stat amount of children\", \"amount\": %lu}\n", carg->fd, carg->key, children);

	}
}


void postgresql_get_databases(PGconn *conn, context_arg *carg)
{
	postgresql_query_init(conn, "SELECT datname FROM pg_catalog.pg_database", NULL, carg, postgresql_received_databases, "database");
}


void postgresql_query_run(context_arg *carg)
{
    if (uv_is_closing((uv_handle_t*)carg->dynamic_socket))
		return;

    postgresql_query_ctx *pqctx = queue_front(carg->pg_queue);
    if (!pqctx)
		return;

	pg_data *data = carg->data;

    if (PQsendQuery(data->conn, pqctx->query)) {
        uv_poll_start(carg->dynamic_socket, UV_READABLE, pg_poll_event);
		printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"postgresql query sent\"}\n", carg->fd, carg->key);
    } else {
		printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"PQsendQuery error\", \"message\": \"%s\"}\n", carg->fd, carg->key, PQerrorMessage(data->conn));
    }
}

void postgresql_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
    carglog(carg, L_INFO, "postgresql_queries_foreach\n");
	pg_data *data = carg->data;

	query_node *qn = arg;
	carglog(carg, L_INFO, "+-+-+-+-+-+-+-+\nrun datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	postgresql_query_init(data->conn, qn->expr, qn, carg, postgresql_write, "database");
}

int postgresql_set_params(PGconn *conn, context_arg *carg)
{
	PQsetnonblocking(conn, 1);

	int socket_fd = PQsocket(conn);
	if (socket_fd < 0) {
		if (carg->log_level > 0)
			printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"invalid socket descriptor\", \"message\": \"%s\"}\n", carg->fd, carg->key, PQerrorMessage(conn));

		return 0;
	}


	carg->dynamic_socket = calloc(1, sizeof(uv_poll_t));
	printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"initiated dynamic socket\", \"message\": \"%p->%p\"}\n", carg->fd, carg->key, carg, carg->dynamic_socket);

	carg->fd = socket_fd;
	uv_poll_init(carg->loop, carg->dynamic_socket, socket_fd);

	carg->dynamic_socket->data = carg;
//
	int sec = carg->timeout / 1000;
	int msec = carg->timeout % 1000;
	struct timeval timeout = { sec, msec };

	int setopt_result_1 = setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	int setopt_result_2 = setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));


	if (setopt_result_1 < 0 || setopt_result_2 < 0)
		if (carg->log_level > 0)
			printf("failed to set timeout\n");
//
	return 1;
}


void pgbouncer_callback(PGresult* r, query_node *arg, context_arg *carg, char *database_class)
{
	char pooler_name[20];

	pg_data *data = carg->data;
	if (data->type == PG_TYPE_PGBOUNCER)
		strlcpy(pooler_name, "pgbouncer", 20);
	else if (data->type == PG_TYPE_ODYSSEY)
		strlcpy(pooler_name, "odyssey", 20);
	else if (data->type == PG_TYPE_PGPOOL)
		strlcpy(pooler_name, "pgpool", 20);

	char *prefix = (char*)arg;
	if (carg->log_level > 1)
	{
		printf("pg status %d/%d\n", PQresultStatus(r), PGRES_TUPLES_OK);
		printf("pg tuples %d, field %d\n", PQntuples(r), PQnfields(r));
	}

	uint64_t i;
	uint64_t j;

	for (i=0; i<PQntuples(r); ++i)
	{
		char metric_name[255];
		char dbname[255];
		char user[255];
		char name[255];
		char pool_mode[255];
		char host[255];
		char port[255];
		char state[255];
		char type[255];
		char addr[255];
		char local_addr[255];
		char local_port[255];
		char tls[255];
		*metric_name = 0;
		*dbname = 0;
		*user = 0;
		*name = 0;
		*pool_mode = 0;
		*host = 0;
		*port = 0;
		*state = 0;
		*type = 0;
		*addr = 0;
		*local_addr = 0;
		*local_port = 0;
		*tls = 0;

		for (j=0; j<PQnfields(r); ++j)
		{
			char *colname = PQfname(r, j);
			//printf("colname: '%s', type: %d\n", colname, PQftype(r, j));
			if (PQftype(r, j) == 700 || PQftype(r, j) == 701) // FLOAT4OID FLOAT8OID
			{
				char *res = PQgetvalue(r, i, j);
				double val = strtod(res, NULL);

				if (prefix)
					snprintf(metric_name, 254, "%s_%s_%s", pooler_name, prefix, colname);
				else
					snprintf(metric_name, 254, "%s_%s", pooler_name, colname);

				alligator_ht *hash = alligator_ht_init(NULL);
				if (*dbname)
					labels_hash_insert_nocache(hash, database_class, dbname);
				if (*user)
					labels_hash_insert_nocache(hash, "user", user);
				if (*name)
					labels_hash_insert_nocache(hash, "name", name);
				if (*pool_mode)
					labels_hash_insert_nocache(hash, "pool_mode", pool_mode);
				if (*host)
					labels_hash_insert_nocache(hash, "host", host);
				if (*port)
					labels_hash_insert_nocache(hash, "port", port);
				if (*state)
					labels_hash_insert_nocache(hash, "state", state);
				if (*type)
					labels_hash_insert_nocache(hash, "type", type);
				if (*addr)
					labels_hash_insert_nocache(hash, "addr", addr);
				if (*local_addr)
					labels_hash_insert_nocache(hash, "local_addr", local_addr);
				if (*local_addr)
					labels_hash_insert_nocache(hash, "local_port", local_port);
				if (*tls)
					labels_hash_insert_nocache(hash, "tls", tls);

				metric_add(metric_name, hash, &val, DATATYPE_DOUBLE, carg);
			}
			if (PQftype(r, j) == 25) // string
			{
				char *res = PQgetvalue(r, i, j);
				if (!strcmp(colname, "database"))
					strlcpy(dbname, res, 255);
				else if (!strcmp(colname, "user"))
					strlcpy(user, res, 255);
				else if (!strcmp(colname, "name"))
					strlcpy(name, res, 255);
				else if (!strcmp(colname, "list"))
					strlcpy(name, res, 255);
				else if (!strcmp(colname, "host"))
					strlcpy(host, res, 255);
				else if (!strcmp(colname, "pool_mode"))
					strlcpy(pool_mode, res, 255);
				else if (!strcmp(colname, "port"))
					strlcpy(port, res, 255);
				else if (!strcmp(colname, "state"))
					strlcpy(state, res, 255);
				else if (!strcmp(colname, "type"))
					strlcpy(type, res, 255);
				else if (!strcmp(colname, "addr"))
					strlcpy(addr, res, 255);
				else if (!strcmp(colname, "local_addr"))
					strlcpy(local_addr, res, 255);
				else if (!strcmp(colname, "local_port"))
					strlcpy(local_port, res, 255);
				else if (!strcmp(colname, "tls"))
					strlcpy(tls, res, 255);
				else if (!strcmp(colname, "force_user"))
					strlcpy(user, res, 255);
			}
			else
			{
				char *res = PQgetvalue(r, i, j);
				int64_t val = strtoll(res, NULL, 10);

				if (prefix)
					snprintf(metric_name, 254, "%s_%s_%s", pooler_name, prefix, colname);
				else
					snprintf(metric_name, 254, "%s_%s", pooler_name, colname);


				if (!strcmp(colname, "wait") || !strcmp(colname, "maxwait")) { }
				else if (!strcmp(colname, "wait_us") || !strcmp(colname, "maxwait_us"))
				{
					alligator_ht *hash = alligator_ht_init(NULL);
					if (*dbname)
						labels_hash_insert_nocache(hash, database_class, dbname);
					if (*user)
						labels_hash_insert_nocache(hash, "user", user);
					if (*name)
						labels_hash_insert_nocache(hash, "name", name);
					if (*pool_mode)
						labels_hash_insert_nocache(hash, "pool_mode", pool_mode);
					if (*host)
						labels_hash_insert_nocache(hash, "host", host);
					if (*port)
						labels_hash_insert_nocache(hash, "port", port);
					if (*state)
						labels_hash_insert_nocache(hash, "state", state);
					if (*type)
						labels_hash_insert_nocache(hash, "type", type);
					if (*addr)
						labels_hash_insert_nocache(hash, "addr", addr);
					if (*local_addr)
						labels_hash_insert_nocache(hash, "local_addr", local_addr);
					if (*local_addr)
						labels_hash_insert_nocache(hash, "local_port", local_port);
					if (*tls)
						labels_hash_insert_nocache(hash, "tls", tls);

					metric_add(metric_name, hash, &val, DATATYPE_INT, carg);
				}
				else
				{
					alligator_ht *hash = alligator_ht_init(NULL);
					if (*dbname)
						labels_hash_insert_nocache(hash, database_class, dbname);
					if (*user)
						labels_hash_insert_nocache(hash, "user", user);
					if (*name)
						labels_hash_insert_nocache(hash, "name", name);
					if (*pool_mode)
						labels_hash_insert_nocache(hash, "pool_mode", pool_mode);
					if (*host)
						labels_hash_insert_nocache(hash, "host", host);
					if (*port)
						labels_hash_insert_nocache(hash, "port", port);
					if (*state)
						labels_hash_insert_nocache(hash, "state", state);
					if (*type)
						labels_hash_insert_nocache(hash, "type", type);
					if (*addr)
						labels_hash_insert_nocache(hash, "addr", addr);
					if (*local_addr)
						labels_hash_insert_nocache(hash, "local_addr", local_addr);
					if (*local_addr)
						labels_hash_insert_nocache(hash, "local_port", local_port);
					if (*tls)
						labels_hash_insert_nocache(hash, "tls", tls);

					//query_0.99 | transaction_0.99
					if (!strncmp(colname, "query_0", 7))
					{
						labels_hash_insert_nocache(hash, "percentile", metric_name+19);
						metric_name[18] = 0;
					}
					else if (!strncmp(colname, "transaction_0", 13))
					{
						labels_hash_insert_nocache(hash, "percentile", metric_name+25);
						metric_name[24] = 0;
					}
					metric_add(metric_name, hash, &val, DATATYPE_INT, carg);
				}
			}
		}
	}
}

void pgbouncer_queries(context_arg *carg)
{
	pg_data *data = carg->data;
	PGconn *conn = data->conn;
	postgresql_query_init(conn, "SHOW STATS", NULL, carg, pgbouncer_callback, "database_server");
	postgresql_query_init(conn, "SHOW POOLS", (query_node *)"pool", carg, pgbouncer_callback, "database_server");
	postgresql_query_init(conn, "SHOW DATABASES", NULL, carg, pgbouncer_callback, "database_client");
	postgresql_query_init(conn, "SHOW LISTS", (query_node *)"lists", carg, pgbouncer_callback, "database");
	postgresql_query_init(conn, "SHOW MEM", (query_node *)"mem", carg, pgbouncer_callback, "database");
	postgresql_query_init(conn, "SHOW CLIENTS", (query_node *)"client", carg, pgbouncer_callback, "database_client");
	postgresql_query_init(conn, "SHOW SERVERS", (query_node *)"server", carg, pgbouncer_callback, "database_server");
}

void odyssey_queries(context_arg *carg)
{
	pg_data *data = carg->data;
	PGconn *conn = data->conn;
	postgresql_query_init(conn, "SHOW STATS", NULL, carg, pgbouncer_callback, "database_server");
	postgresql_query_init(conn, "SHOW POOLS_EXTENDED", (query_node *)"pool", carg, pgbouncer_callback, "database_server");
	postgresql_query_init(conn, "SHOW DATABASES", NULL, carg, pgbouncer_callback, "database_client");
	postgresql_query_init(conn, "SHOW LISTS", (query_node *)"lists", carg, pgbouncer_callback, "database");
	postgresql_query_init(conn, "SHOW MEM", (query_node *)"mem", carg, pgbouncer_callback, "database");
	postgresql_query_init(conn, "SHOW CLIENTS", (query_node *)"client", carg, pgbouncer_callback, "database_client");
	postgresql_query_init(conn, "SHOW SERVERS", (query_node *)"server", carg, pgbouncer_callback, "database_server");
}

void pgpool_queries(context_arg *carg)
{
	pg_data *data = carg->data;
	PGconn *conn = data->conn;
	postgresql_query_init(conn, "SHOW POOL_NODES", (query_node *)"node", carg, pgbouncer_callback, "database");
	postgresql_query_init(conn, "SHOW POOL_PROCESSES", (query_node *)"process", carg, pgbouncer_callback, "database");
	postgresql_query_init(conn, "SHOW POOL_POOLS", (query_node *)"pool", carg, pgbouncer_callback, "database");
	postgresql_query_init(conn, "SHOW POOL_CACHE", (query_node *)"cache", carg, pgbouncer_callback, "database");
}

void postgresql_run(void* arg)
{
	context_arg *carg = arg;

	//printf("check for lock: %d\n", carg->lock);
    if (carg->lock) {
		printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"carg locked!\"}\n", carg->fd, carg->key);
		return;
	}

	if (carg->parental_carg) {
		//printf("check for parental lock: %d\n", carg->parental_carg->lock);
		printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"carg parental locked!\"}\n", carg->fd, carg->key);
		if (carg->parental_carg->lock) {
			carg_free(carg);
			return;
		}
	}



	carg->timeout = 1000;
	carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);

	pg_data *data = carg->data;
	if (!data) {
		carg->data = data = calloc(1, sizeof(*data));
		data->type = PG_TYPE_PG;
	} else {
		PQfinish(data->conn);
	}

	printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"connection starting!\", \"arg\": \"%s\"}\n", carg->fd, carg->key, carg->url);

	PGconn *conn = PQconnectStart(carg->url);
	printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"connection start!\", \"arg\": \"%s\", \"res\": \"%p\"}\n", carg->fd, carg->key, carg->url, conn);

	if (!conn) {
		carglog(carg, L_ERROR, "Connection to database failed: '%d' error: %s", PQstatus(conn), PQerrorMessage(conn));
		return;
	}

	if (!postgresql_set_params(conn, carg))
		return;

	data->conn = conn;
	printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"created socket\"}\n", carg->fd, carg->key);
	if (data->type == PG_TYPE_PG)
	{
		if (carg->name)
		{
			query_ds *qds = query_get(carg->name);
			carglog(carg, L_INFO, "found queries for datasource: %s: %p\n", carg->name, qds);
			if (qds)
			{
				alligator_ht_foreach_arg(qds->hash, postgresql_queries_foreach, carg);
			}
		}

		if (!carg->data_lock)
			postgresql_get_databases(conn, carg);
	}

	if (data->type == PG_TYPE_PGBOUNCER)
	{
		pgbouncer_queries(carg);
	}
	else if (data->type == PG_TYPE_ODYSSEY)
	{
		odyssey_queries(carg);
	}
	else if (data->type == PG_TYPE_PGPOOL)
	{
		pgpool_queries(carg);
	}

	printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"uv_poll_start\"}\n", carg->fd, carg->key);
	uv_poll_start(carg->dynamic_socket, UV_WRITABLE, pg_poll_event);
}

void postgresql_timer(uv_timer_t* handle) {
	(void)handle;
	printf("{\"fd\": %d, \"conn\": \"%s\", \"action\": \"run timer\", \"count\": %zu}\n", 0, "", alligator_ht_count(ac->pg_aggregator));
	alligator_ht_foreach(ac->pg_aggregator, postgresql_run);
}

void postgresql_client_handler()
{
	uv_timer_init(ac->loop, &ac->pg_timer);
	uv_timer_start(&ac->pg_timer, postgresql_timer, ac->aggregator_startup, ac->aggregator_repeat);
}

char* postgresql_client(context_arg* carg)
{
	if (!carg)
		return NULL;

	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);

	alligator_ht_insert(ac->pg_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "postgresql";
}

void postgresql_client_del(context_arg* carg)
{
	if (!carg)
		return;

	alligator_ht_remove_existing(ac->pg_aggregator, &(carg->node));
	carg_free(carg);
}

void pg_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	pg_data *data = calloc(1, sizeof(*data));

	actx->key = strdup("postgresql");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);
	actx->data = data;

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"postgresql", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void pgbouncer_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	pg_data *data = calloc(1, sizeof(*data));
	data->type = PG_TYPE_PGBOUNCER;

	actx->key = strdup("pgbouncer");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);
	actx->data = data;

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"pgbouncer", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void odyssey_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	pg_data *data = calloc(1, sizeof(*data));
	data->type = PG_TYPE_ODYSSEY;

	actx->key = strdup("odyssey");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);
	actx->data = data;

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"odyssey", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void pgpool_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	pg_data *data = calloc(1, sizeof(*data));
	data->type = PG_TYPE_PGPOOL;

	actx->key = strdup("pgpool");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);
	actx->data = data;

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"pgpool", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
