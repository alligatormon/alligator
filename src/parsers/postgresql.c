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
#include <main.h>

#define PG_TYPE_PG 0
#define PG_TYPE_PGBOUNCER 1
#define PG_TYPE_ODYSSEY 2
#define PG_TYPE_PGPOOL 3

typedef struct pg_data {
	PGconn *conn;
	int type;
} pg_data;

void postgresql_run(void* arg);

void postgresql_free(pq_library* pglib)
{
	if (!pglib) return;

	if (pglib && pglib->PQclear_lib) free(pglib->PQclear_lib);
	if (pglib && pglib->PQconnectdb_lib) free(pglib->PQconnectdb_lib);
	if (pglib && pglib->PQerrorMessage_lib) free(pglib->PQerrorMessage_lib);
	if (pglib && pglib->PQexecParams_lib) free(pglib->PQexecParams_lib);
	if (pglib && pglib->PQfinish_lib) free(pglib->PQfinish_lib);
	if (pglib && pglib->PQfname_lib) free(pglib->PQfname_lib);
	if (pglib && pglib->PQgetlength_lib) free(pglib->PQgetlength_lib);
	if (pglib && pglib->PQgetvalue_lib) free(pglib->PQgetvalue_lib);
	if (pglib && pglib->PQnfields_lib) free(pglib->PQnfields_lib);
	if (pglib && pglib->PQntuples_lib) free(pglib->PQntuples_lib);
	if (pglib && pglib->PQresultStatus_lib) free(pglib->PQresultStatus_lib);
	if (pglib && pglib->PQstatus_lib) free(pglib->PQstatus_lib);
	if (pglib && pglib->PQftype_lib) free(pglib->PQftype_lib);
	if (pglib && pglib->PQflush_lib) free(pglib->PQflush_lib);
	if (pglib && pglib->PQgetResult_lib) free(pglib->PQgetResult_lib);
	if (pglib && pglib->PQresetPoll_lib) free(pglib->PQresetPoll_lib);
	if (pglib && pglib->PQconnectPoll_lib) free(pglib->PQconnectPoll_lib);
	if (pglib && pglib->PQresetStart_lib) free(pglib->PQresetStart_lib);
	if (pglib && pglib->PQsocket_lib) free(pglib->PQsocket_lib);
	if (pglib && pglib->PQconnectStart_lib) free(pglib->PQconnectStart_lib);
	if (pglib && pglib->PQsendQueryParams_lib) free(pglib->PQsendQueryParams_lib);
	if (pglib && pglib->PQsendPrepare_lib) free(pglib->PQsendPrepare_lib);
	if (pglib && pglib->PQsendQueryPrepared_lib) free(pglib->PQsendQueryPrepared_lib);
	if (pglib && pglib->PQconsumeInput_lib) free(pglib->PQconsumeInput_lib);
	if (pglib && pglib->PQisBusy_lib) free(pglib->PQisBusy_lib);
	if (pglib && pglib->PQconnectStartParams_lib) free(pglib->PQconnectStartParams_lib);
	if (pglib && pglib->PQescapeIdentifier_lib) free(pglib->PQescapeIdentifier_lib);
	if (pglib && pglib->PQsendQuery_lib) free(pglib->PQsendQuery_lib);
	if (pglib && pglib->PQfreemem_lib) free(pglib->PQfreemem_lib);
	if (pglib && pglib->PQnotifies_lib) free(pglib->PQnotifies_lib);
	if (pglib && pglib->PQexec_lib) free(pglib->PQexec_lib);

	free(pglib);
}

pq_library* postgresql_init()
{
	module_t *libpq = alligator_ht_search(ac->modules, module_compare, "postgresql", tommy_strhash_u32(0, "postgresql"));
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

void postgresql_write(PGresult* r, query_node *qn, context_arg *carg, char *database_class)
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
		alligator_ht *hash = alligator_ht_init(NULL);

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
	}
}

void postgresql_query(PGconn *conn, char *query, query_node *qn, context_arg *carg, void callback(PGresult*, query_node*, context_arg*, char*), char *database_class)
{
	PGresult *res = ac->pqlib->PQexec(conn, query);
	if (ac->pqlib->PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		if (carg->log_level > 1)
		{
			fprintf(stderr, "%s command failed: %s", query, ac->pqlib->PQerrorMessage(conn));
			carg->parser_status = 0;

			//char reason[255];
			//uint64_t val = 1;
			//uint64_t reason_size = strlcpy(reason, ac->pqlib->PQerrorMessage(conn), 255);
			//prometheus_metric_name_normalizer(reason, reason_size);
			//metric_add_labels2("postgresql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);
		}
	}
	else
		callback(res, qn, carg, database_class);

	if (res)
		ac->pqlib->PQclear(res);
}

void postgresql_get_databases(PGconn *conn, context_arg *carg)
{
	//PGresult *res = ac->pqlib->PQexec(conn, "SELECT datname FROM pg_catalog.pg_database");
	PGresult *res = ac->pqlib->PQexec(conn, "SELECT datname FROM pg_catalog.pg_database WHERE datname != 'postgres'");
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

		//char reason[255];
		//uint64_t reason_size = strlcpy(reason, ac->pqlib->PQerrorMessage(conn), 255);
		//uint64_t val = 1;
		//prometheus_metric_name_normalizer(reason, reason_size);
		//metric_add_labels2("postgresql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);
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
    puts("postgresql_queries_foreach");
	context_arg *carg = (context_arg*)funcarg;
	pg_data *data = carg->data;
	PGconn *conn = data->conn;
	query_node *qn = arg;
	carglog(carg, L_INFO, "+-+-+-+-+-+-+-+\nrun datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	postgresql_query(conn, qn->expr, qn, carg, postgresql_write, "database");
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
		printf("pg status %d/%d\n", ac->pqlib->PQresultStatus(r), PGRES_TUPLES_OK);
		printf("pg tuples %d, field %d\n", ac->pqlib->PQntuples(r), ac->pqlib->PQnfields(r));
	}

	uint64_t i;
	uint64_t j;

	for (i=0; i<ac->pqlib->PQntuples(r); ++i)
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

		for (j=0; j<ac->pqlib->PQnfields(r); ++j)
		{
			char *colname = ac->pqlib->PQfname(r, j);
			//printf("colname: '%s', type: %d\n", colname, ac->pqlib->PQftype(r, j));
			if (ac->pqlib->PQftype(r, j) == 700 || ac->pqlib->PQftype(r, j) == 701) // FLOAT4OID FLOAT8OID
			{
				char *res = ac->pqlib->PQgetvalue(r, i, j);
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
			if (ac->pqlib->PQftype(r, j) == 25) // string
			{
				char *res = ac->pqlib->PQgetvalue(r, i, j);
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
				char *res = ac->pqlib->PQgetvalue(r, i, j);
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
	postgresql_query(conn, "SHOW STATS", NULL, carg, pgbouncer_callback, "database_server");
	postgresql_query(conn, "SHOW POOLS", (query_node *)"pool", carg, pgbouncer_callback, "database_server");
	postgresql_query(conn, "SHOW DATABASES", NULL, carg, pgbouncer_callback, "database_client");
	postgresql_query(conn, "SHOW LISTS", (query_node *)"lists", carg, pgbouncer_callback, "database");
	postgresql_query(conn, "SHOW MEM", (query_node *)"mem", carg, pgbouncer_callback, "database");
	postgresql_query(conn, "SHOW CLIENTS", (query_node *)"client", carg, pgbouncer_callback, "database_client");
	postgresql_query(conn, "SHOW SERVERS", (query_node *)"server", carg, pgbouncer_callback, "database_server");
}

void odyssey_queries(context_arg *carg)
{
	pg_data *data = carg->data;
	PGconn *conn = data->conn;
	postgresql_query(conn, "SHOW STATS", NULL, carg, pgbouncer_callback, "database_server");
	postgresql_query(conn, "SHOW POOLS_EXTENDED", (query_node *)"pool", carg, pgbouncer_callback, "database_server");
	postgresql_query(conn, "SHOW DATABASES", NULL, carg, pgbouncer_callback, "database_client");
	postgresql_query(conn, "SHOW LISTS", (query_node *)"lists", carg, pgbouncer_callback, "database");
	postgresql_query(conn, "SHOW MEM", (query_node *)"mem", carg, pgbouncer_callback, "database");
	postgresql_query(conn, "SHOW CLIENTS", (query_node *)"client", carg, pgbouncer_callback, "database_client");
	postgresql_query(conn, "SHOW SERVERS", (query_node *)"server", carg, pgbouncer_callback, "database_server");
}

void pgpool_queries(context_arg *carg)
{
	pg_data *data = carg->data;
	PGconn *conn = data->conn;
	postgresql_query(conn, "SHOW POOL_NODES", (query_node *)"node", carg, pgbouncer_callback, "database");
	postgresql_query(conn, "SHOW POOL_PROCESSES", (query_node *)"process", carg, pgbouncer_callback, "database");
	postgresql_query(conn, "SHOW POOL_POOLS", (query_node *)"pool", carg, pgbouncer_callback, "database");
	postgresql_query(conn, "SHOW POOL_CACHE", (query_node *)"cache", carg, pgbouncer_callback, "database");
}

void postgresql_run(void* arg)
{
	uint64_t val = 1;
	uint64_t unval = 0;
	context_arg *carg = arg;

	if (!postgres_init_module())
	{
		char reason[255];
		strlcpy(reason, "error with load libpq module", 255);
		//metric_add_labels2("postgresql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);
		metric_add_labels5("alligator_connect_ok", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql");
		metric_add_labels5("alligator_parser_status", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "postgresql");

		return;
	}

	PGconn	 *conn;

	conn = ac->pqlib->PQconnectdb(carg->url);
	if (ac->pqlib->PQstatus(conn) != CONNECTION_OK)
	{
		carglog(carg, L_INFO, "Connection to database status: '%d' error: %s", ac->pqlib->PQstatus(conn), ac->pqlib->PQerrorMessage(conn));

		char reason[255];
		uint64_t reason_size = strlcpy(reason, ac->pqlib->PQerrorMessage(conn), 255);
		prometheus_metric_name_normalizer(reason, reason_size);
		//metric_add_labels2("postgresql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);
		metric_add_labels5("alligator_connect_ok", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "postgresql");
		metric_add_labels5("alligator_parser_status", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "postgresql");

        printf("reason is '%s': %d\n", ac->pqlib->PQerrorMessage(conn), strcmp(ac->pqlib->PQerrorMessage(conn), "SSL SYSCALL error: Success\n"));
		if (!strcmp(ac->pqlib->PQerrorMessage(conn), "SSL SYSCALL error: Success\n"))
			return;

		ac->pqlib->PQfinish(conn);
		return;
	}

	postgresql_set_params(conn, carg);
	pg_data *data = carg->data;
	data->conn = conn;

	metric_add_labels5("alligator_connect_ok", &val, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "postgresql");
	carg->parser_status = 1;

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

	metric_add_labels5("alligator_parser_status", &carg->parser_status, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "postgresql");
	ac->pqlib->PQfinish(conn);
}

void postgresql_timer(void *arg) {
	extern aconf* ac;
	usleep(ac->aggregator_startup * 1000);
	while ( 1 )
	{
		alligator_ht_foreach(ac->pg_aggregator, postgresql_run);
		usleep(ac->aggregator_repeat * 1000);
	}
}

void postgresql_timer_without_thread(uv_timer_t* handle) {
	(void)handle;
	alligator_ht_foreach(ac->pg_aggregator, postgresql_run);
}

void postgresql_without_thread()
{
	uv_timer_init(ac->loop, &ac->pg_timer);
	uv_timer_start(&ac->pg_timer, postgresql_timer_without_thread, ac->aggregator_startup, ac->aggregator_repeat);
}

void postgresql_client_handler()
{
	// uncomment for thread
	//extern aconf* ac;

	//uv_thread_t th;
	//uv_thread_create(&th, postgresql_timer, NULL);
	postgresql_without_thread();
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
