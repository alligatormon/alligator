#include <libpq-fe.h>
#include "parsers/postgresql.h"
#include "main.h"
extern aconf* ac;

void postgres_get_result(PGresult *res)
{
	int i, j;

	int64_t rows = ac->pqlib->PQntuples(res);
	for (i = 0; i < rows; i++)
	{
		char	   *iptr;
		int64_t cols = ac->pqlib->PQnfields(res);
		for (j = 0; j < cols; j++)
		{
			int64_t iptr_size = ac->pqlib->PQgetlength(res, i, j);
			iptr = ac->pqlib->PQgetvalue(res, i, j);
			char *col_name = ac->pqlib->PQfname(res, j);
			printf("%s='%s'(%zu) ", col_name, iptr, iptr_size);
		}
		printf("\n");
	}
}

PGconn* postgres_connect(char *conninfo)
{
	PGconn *conn = ac->pqlib->PQconnectdb(conninfo);
	if (ac->pqlib->PQstatus(conn) != CONNECTION_OK)
	{
		fprintf(stderr, "Connection to database failed: %s", ac->pqlib->PQerrorMessage(conn));
		ac->pqlib->PQfinish(conn);
		return NULL;
	}

	return conn;
}

void posgres_exec(PGconn *conn, char *query)
{
	if (!conn)
		return;

	PGresult *res = ac->pqlib->PQexecParams(conn, query, 0, NULL, NULL, NULL, NULL, 0);
	if (ac->pqlib->PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		printf("query: '%s' failed: %s", query, ac->pqlib->PQerrorMessage(conn));
		ac->pqlib->PQclear(res);
	}

	postgres_get_result(res);
	ac->pqlib->PQclear(res);
}

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

	return pglib;
}

void postgres_run(char *conninfo, char *query, char *query2, char *query3, char *query4)
{
	if (!ac->pqlib)
	{
		pq_library* pqlib = postgresql_init();
		if (pqlib)
			ac->pqlib = pqlib;
		else
		{
			puts("Cannot initialize postgresql library");
			return;
		}
	}

	PGconn *conn = postgres_connect(conninfo);
	posgres_exec(conn, query);
	posgres_exec(conn, query2);
	posgres_exec(conn, query3);
	posgres_exec(conn, query4);
	ac->pqlib->PQfinish(conn);
}

//int main()
//{
//	postgres_run("postgresql://postgres@localhost", "SELECT * FROM pg_stat_activity", "SELECT * FROM pg_stat_all_tables", "SELECT * FROM pg_stat_replication", "SELECT count(datname) FROM pg_database;");
//}
