#include <stdio.h>
#include <stdlib.h>
#include <libpq-fe.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <query/query.h>
#include <main.h>

void postgresql_run(void* arg);

void postgresql_free(pq_library* pglib)
{
	if (!pglib) return;

	if (pglib && pglib->PQclear) free(pglib->PQclear);
	if (pglib && pglib->PQconnectdb) free(pglib->PQconnectdb);
	if (pglib && pglib->PQerrorMessage) free(pglib->PQerrorMessage);
	if (pglib && pglib->PQexecParams) free(pglib->PQexecParams);
	if (pglib && pglib->PQfinish) free(pglib->PQfinish);
	if (pglib && pglib->PQfname) free(pglib->PQfname);
	if (pglib && pglib->PQgetlength) free(pglib->PQgetlength);
	if (pglib && pglib->PQgetvalue) free(pglib->PQgetvalue);
	if (pglib && pglib->PQnfields) free(pglib->PQnfields);
	if (pglib && pglib->PQntuples) free(pglib->PQntuples);
	if (pglib && pglib->PQresultStatus) free(pglib->PQresultStatus);
	if (pglib && pglib->PQstatus) free(pglib->PQstatus);
	if (pglib && pglib->PQftype) free(pglib->PQftype);
	if (pglib && pglib->PQflush) free(pglib->PQflush);
	if (pglib && pglib->PQgetResult) free(pglib->PQgetResult);
	if (pglib && pglib->PQresetPoll) free(pglib->PQresetPoll);
	if (pglib && pglib->PQconnectPoll) free(pglib->PQconnectPoll);
	if (pglib && pglib->PQresetStart) free(pglib->PQresetStart);
	if (pglib && pglib->PQsocket) free(pglib->PQsocket);
	if (pglib && pglib->PQconnectStart) free(pglib->PQconnectStart);
	if (pglib && pglib->PQsendQueryParams) free(pglib->PQsendQueryParams);
	if (pglib && pglib->PQsendPrepare) free(pglib->PQsendPrepare);
	if (pglib && pglib->PQsendQueryPrepared) free(pglib->PQsendQueryPrepared);
	if (pglib && pglib->PQconsumeInput) free(pglib->PQconsumeInput);
	if (pglib && pglib->PQisBusy) free(pglib->PQisBusy);
	if (pglib && pglib->PQconnectStartParams) free(pglib->PQconnectStartParams);
	if (pglib && pglib->PQescapeIdentifier) free(pglib->PQescapeIdentifier);
	if (pglib && pglib->PQsendQuery) free(pglib->PQsendQuery);
	if (pglib && pglib->PQfreemem) free(pglib->PQfreemem);
	if (pglib && pglib->PQnotifies) free(pglib->PQnotifies);
	if (pglib && pglib->PQexec) free(pglib->PQexec);

	free(pglib);
}

pq_library* postgresql_init()
{
	module_t *libpq = tommy_hashdyn_search(ac->modules, module_compare, "postgresql", tommy_strhash_u32(0, "postgresql"));
	if (!libpq)
	{
		printf("No defined postgresql library in configuration\n");
		return NULL;
	}


	pq_library* pglib = calloc(1, sizeof(*pglib));

	pglib->PQclear = module_load(libpq->path, "PQclear", &pglib->PQclear_lib);
	if (!pglib->PQclear)
	{
		printf("Cannot get PQclear from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQconnectdb = (void*)module_load(libpq->path, "PQconnectdb", &pglib->PQconnectdb_lib);
	if (!pglib->PQconnectdb)
	{
		printf("Cannot get PQconnectdb from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQerrorMessage = (void*)module_load(libpq->path, "PQerrorMessage", &pglib->PQerrorMessage_lib);
	if (!pglib->PQerrorMessage)
	{
		printf("Cannot get PQerrorMessage from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQexecParams = (void*)module_load(libpq->path, "PQexecParams", &pglib->PQexecParams_lib);
	if (!pglib->PQexecParams)
	{
		printf("Cannot get PQexecParams from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQfinish = (void*)module_load(libpq->path, "PQfinish", &pglib->PQfinish_lib);
	if (!pglib->PQfinish)
	{
		printf("Cannot get PQfinish from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQfname = (void*)module_load(libpq->path, "PQfname", &pglib->PQfname_lib);
	if (!pglib->PQfname)
	{
		printf("Cannot get PQfname from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQgetlength = (void*)module_load(libpq->path, "PQgetlength", &pglib->PQgetlength_lib);
	if (!pglib->PQgetlength)
	{
		printf("Cannot get PQgetlength from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQgetvalue = (void*)module_load(libpq->path, "PQgetvalue", &pglib->PQgetvalue_lib);
	if (!pglib->PQgetvalue)
	{
		printf("Cannot get PQgetvalue from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQnfields = (void*)module_load(libpq->path, "PQnfields", &pglib->PQnfields_lib);
	if (!pglib->PQnfields)
	{
		printf("Cannot get PQnfields from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQntuples = (void*)module_load(libpq->path, "PQntuples", &pglib->PQntuples_lib);
	if (!pglib->PQntuples)
	{
		printf("Cannot get PQntuples from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQresultStatus = (void*)module_load(libpq->path, "PQresultStatus", &pglib->PQresultStatus_lib);
	if (!pglib->PQresultStatus)
	{
		printf("Cannot get PQresultStatus from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQstatus = (void*)module_load(libpq->path, "PQstatus", &pglib->PQstatus_lib);
	if (!pglib->PQstatus)
	{
		printf("Cannot get PQstatus from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQftype = (void*)module_load(libpq->path, "PQftype", &pglib->PQftype_lib);
	if (!pglib->PQftype)
	{
		printf("Cannot get PQftype from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQflush = (void*)module_load(libpq->path, "PQflush", &pglib->PQflush_lib);
	if (!pglib->PQflush)
	{
		printf("Cannot get PQflush from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQgetResult = (void*)module_load(libpq->path, "PQgetResult", &pglib->PQgetResult_lib);
	if (!pglib->PQgetResult)
	{
		printf("Cannot get PQgetResult from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQresetPoll = (void*)module_load(libpq->path, "PQresetPoll", &pglib->PQresetPoll_lib);
	if (!pglib->PQresetPoll)
	{
		printf("Cannot get PQresetPoll from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQconnectPoll = (void*)module_load(libpq->path, "PQconnectPoll", &pglib->PQconnectPoll_lib);
	if (!pglib->PQconnectPoll)
	{
		printf("Cannot get PQconnectPoll from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQresetStart = (void*)module_load(libpq->path, "PQresetStart", &pglib->PQresetStart_lib);
	if (!pglib->PQresetStart)
	{
		printf("Cannot get PQresetStart from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQsocket = (void*)module_load(libpq->path, "PQsocket", &pglib->PQsocket_lib);
	if (!pglib->PQsocket)
	{
		printf("Cannot get PQsocket from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQconnectStart = (void*)module_load(libpq->path, "PQconnectStart", &pglib->PQconnectStart_lib);
	if (!pglib->PQconnectStart)
	{
		printf("Cannot get PQconnectStart from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQsendQueryParams = (void*)module_load(libpq->path, "PQsendQueryParams", &pglib->PQsendQueryParams_lib);
	if (!pglib->PQsendQueryParams)
	{
		printf("Cannot get PQsendQueryParams from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQsendPrepare = (void*)module_load(libpq->path, "PQsendPrepare", &pglib->PQsendPrepare_lib);
	if (!pglib->PQsendPrepare)
	{
		printf("Cannot get PQsendPrepare from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQsendQueryPrepared = (void*)module_load(libpq->path, "PQsendQueryPrepared", &pglib->PQsendQueryPrepared_lib);
	if (!pglib->PQsendQueryPrepared)
	{
		printf("Cannot get PQsendQueryPrepared from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQconsumeInput = (void*)module_load(libpq->path, "PQconsumeInput", &pglib->PQconsumeInput_lib);
	if (!pglib->PQconsumeInput)
	{
		printf("Cannot get PQconsumeInput from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQisBusy = (void*)module_load(libpq->path, "PQisBusy", &pglib->PQisBusy_lib);
	if (!pglib->PQisBusy)
	{
		printf("Cannot get PQisBusy from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQconnectStartParams = (void*)module_load(libpq->path, "PQconnectStartParams", &pglib->PQconnectStartParams_lib);
	if (!pglib->PQconnectStartParams)
	{
		printf("Cannot get PQconnectStartParams from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQescapeIdentifier = (void*)module_load(libpq->path, "PQescapeIdentifier", &pglib->PQescapeIdentifier_lib);
	if (!pglib->PQescapeIdentifier)
	{
		printf("Cannot get PQescapeIdentifier from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQsendQuery = (void*)module_load(libpq->path, "PQsendQuery", &pglib->PQsendQuery_lib);
	if (!pglib->PQsendQuery)
	{
		printf("Cannot get PQsendQuery from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQfreemem = (void*)module_load(libpq->path, "PQfreemem", &pglib->PQfreemem_lib);
	if (!pglib->PQfreemem)
	{
		printf("Cannot get PQfreemem from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQnotifies = (void*)module_load(libpq->path, "PQnotifies", &pglib->PQnotifies_lib);
	if (!pglib->PQnotifies)
	{
		printf("Cannot get PQnotifies from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	pglib->PQexec = (void*)module_load(libpq->path, "PQexec", &pglib->PQexec_lib);
	if (!pglib->PQexec)
	{
		printf("Cannot get PQexec from libpq\n");
		postgresql_free(pglib);
		return NULL;
	}

	return pglib;
}

int postgres_init_module()
{
	if (!ac->pqlib)
	{
		pq_library* pqlib = postgresql_init();
		if (pqlib)
			ac->pqlib = pqlib;
		else
		{
			puts("Cannot initialize postgresql library");
			return 0;
		}
	}

	return 1;
}

void postgresql_write(PGresult* r, query_node *qn, context_arg *carg)
{
	if (carg->log_level > 1)
	{
		printf("pg status %d/%d\n", ac->pqlib->PQresultStatus(r), PGRES_TUPLES_OK);
		printf("pg tuples %d, field %d\n", ac->pqlib->PQntuples(r), ac->pqlib->PQnfields(r));
	}

	uint64_t i;
	uint64_t j;

	for (i=0; i<ac->pqlib->PQntuples(r); ++i)
	{
		tommy_hashdyn *hash = malloc(sizeof(*hash));
		tommy_hashdyn_init(hash);

		if (carg->ns)
			labels_hash_insert_nocache(hash, "dbname", carg->ns);

		for (j=0; j<ac->pqlib->PQnfields(r); ++j)
		{
			char *colname = ac->pqlib->PQfname(r, j);
			if (ac->pqlib->PQftype(r, j) == 16) // BOOLOID
			{
				char *res = ac->pqlib->PQgetvalue(r, i, j);
				query_field *qf = query_field_get(qn->qf_hash, colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%s'\n", res);
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
			else if (ac->pqlib->PQftype(r, j) == 700 || ac->pqlib->PQftype(r, j) == 701) // FLOAT4OID FLOAT8OID
			{
				char *res = ac->pqlib->PQgetvalue(r, i, j);
				query_field *qf = query_field_get(qn->qf_hash, colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%s'\n", res);
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
				char *res = ac->pqlib->PQgetvalue(r, i, j);
				query_field *qf = query_field_get(qn->qf_hash, colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%s'\n", res);
					qf->i = strtoll(res, NULL, 10);
					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", colname, res);
					labels_hash_insert_nocache(hash, colname, res);
				}
			}
		}
		qn->labels = hash;
		qn->carg = carg;
		query_set_values(qn);
		labels_hash_free(hash);
		//metric_add(qn->make, hash, &, DATATYPE_UINT, ac->system_carg);
	}
}

void postgresql_query(PGconn *conn, char *query, query_node *qn, context_arg *carg)
{
	PGresult *res = ac->pqlib->PQexec(conn, query);
	if (ac->pqlib->PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		if (carg->log_level > 1)
			fprintf(stderr, "%s command failed: %s", query, ac->pqlib->PQerrorMessage(conn));
	}
	else
		postgresql_write(res, qn, carg);

	if (res)
		ac->pqlib->PQclear(res);
}

void postgresql_get_databases(PGconn *conn, context_arg *carg)
{
	PGresult *res = ac->pqlib->PQexec(conn, "SELECT datname FROM pg_catalog.pg_database");
	context_arg *db_carg = malloc(sizeof(*db_carg));
	size_t url_len = strlen(carg->url) + 1024;
	char *url = malloc(url_len);
	size_t name_len = strlen(carg->name) + 1024;
	char *name = malloc(name_len);
	char *ns = malloc(1024);

	memcpy(db_carg, carg, sizeof(*db_carg));
	db_carg->url = url;
	db_carg->name = name;
	db_carg->ns = ns;
	db_carg->data_lock = 1;

	int wildcard = 0;
	snprintf(name, name_len - 1, "%s/*", carg->name);
	if (query_get(name))
		wildcard = 1;

	if (ac->pqlib->PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		if (carg->log_level > 1)
			fprintf(stderr, "command get databases failed: %s", ac->pqlib->PQerrorMessage(conn));
	}
	else
	{
		uint64_t i;
		uint64_t j;

		for (i=0; i<ac->pqlib->PQntuples(res); ++i)
		{

			for (j=0; j<ac->pqlib->PQnfields(res); ++j)
			{
				char *resp = ac->pqlib->PQgetvalue(res, i, j);
				snprintf(db_carg->url, url_len - 1, "%s/%s", carg->url, resp);
				strlcpy(db_carg->ns, resp, 1024);

				snprintf(db_carg->name, name_len - 1, "%s/*", carg->name);
				if (wildcard)
				{
					if (carg->log_level > 1)
						printf("run wildcard queries\n");
					postgresql_run(db_carg);
				}

				snprintf(db_carg->name, name_len - 1, "%s/%s", carg->name, resp);
				if (query_get(db_carg->name))
				{
					if (carg->log_level > 1)
						printf("exec queries database '%s'\n", resp);

					postgresql_run(db_carg);
				}
			}
		}
	}

	if (res)
		ac->pqlib->PQclear(res);


	free(db_carg);
	free(ns);
	free(url);
	free(name);
}

void postgresql_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	PGconn *conn = carg->data;
	query_node *qn = arg;
	if (carg->log_level > 1)
	{
		puts("+-+-+-+-+-+-+-+");
		printf("run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	}
	postgresql_query(conn, qn->expr, qn, carg);
}

void postgresql_set_params(PGconn *conn, context_arg *carg)
{
	int socket_fd = ac->pqlib->PQsocket(conn);
	if (socket_fd < 0) {
		if (carg->log_level > 0)
			printf("Invalid socket descriptor: %d\n", socket_fd);

		return;
	}

	int sec = carg->timeout / 1000;
	int msec = carg->timeout % 1000;
	struct timeval timeout = { sec, msec };

	int setopt_result_1 = setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	int setopt_result_2 = setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

	if (setopt_result_1 < 0 || setopt_result_2 < 0)
		if (carg->log_level > 0)
			printf("failed to set timeout\n");
}

void postgresql_run(void* arg)
{
	if (!postgres_init_module())
		return;

	PGconn	 *conn;

	context_arg *carg = arg;
	conn = ac->pqlib->PQconnectdb(carg->url);
	if (ac->pqlib->PQstatus(conn) != CONNECTION_OK)
	{
		if (carg->log_level > 0)
			fprintf(stderr, "Connection to database failed: %s", ac->pqlib->PQerrorMessage(conn));
		ac->pqlib->PQfinish(conn);
		return;
	}

	postgresql_set_params(conn, carg);

	if (carg->name)
	{
		query_ds *qds = query_get(carg->name);
		if (carg->log_level > 1)
			printf("found queries for datasource: %s: %p\n", carg->name, qds);
		if (qds)
		{
			carg->data = conn;
			tommy_hashdyn_foreach_arg(qds->hash, postgresql_queries_foreach, carg);
		}
	}

	if (!carg->data_lock)
		postgresql_get_databases(conn, carg);

	ac->pqlib->PQfinish(conn);
}

void postgresql_timer(void *arg) {
	extern aconf* ac;
	usleep(ac->aggregator_startup * 1000);
	while ( 1 )
	{
		tommy_hashdyn_foreach(ac->pg_aggregator, postgresql_run);
		usleep(ac->aggregator_repeat * 1000);
	}
}

void postgresql_timer_without_thread(uv_timer_t* handle) {
	(void)handle;
	tommy_hashdyn_foreach(ac->pg_aggregator, postgresql_run);
}

void postgresql_without_thread()
{
	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(ac->loop, timer1);
	uv_timer_start(timer1, postgresql_timer_without_thread, ac->aggregator_startup, ac->aggregator_repeat);
}

void postgresql_client_handler()
{
	// uncomment for thread
	//extern aconf* ac;

	//uv_thread_t th;
	//uv_thread_create(&th, postgresql_timer, NULL);
	postgresql_without_thread();
}

void postgresql_client(context_arg* carg)
{
	if (!carg)
		return;

	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);

	tommy_hashdyn_insert(ac->pg_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
}

void pg_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("postgresql");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"postgresql", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
