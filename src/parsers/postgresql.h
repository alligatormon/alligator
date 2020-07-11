#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <libpq-fe.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include "modules/modules.h"

typedef struct pq_library {
	uv_lib_t *PQclear_lib;
	void (*PQclear)(PGresult *res);

	uv_lib_t *PQconnectdb_lib;
	PGconn* (*PQconnectdb)(const char *conninfo);

	uv_lib_t *PQerrorMessage_lib;
	char* (*PQerrorMessage)(const PGconn *conn);

	uv_lib_t *PQexecParams_lib;
	PGresult* (*PQexecParams)(PGconn *conn, const char *command, int nParams, const Oid *paramTypes, const char * const *paramValues, const int *paramLengths, const int *paramFormats, int resultFormat);

	uv_lib_t *PQfinish_lib;
	void (*PQfinish)(PGconn *conn);

	uv_lib_t *PQfname_lib;
	char* (*PQfname)(const PGresult *res, int column_number);

	uv_lib_t *PQgetlength_lib;
	int (*PQgetlength)(const PGresult *res, int row_number, int column_number);

	uv_lib_t *PQgetvalue_lib;
	char* (*PQgetvalue)(const PGresult *res, int row_number, int column_number);

	uv_lib_t *PQnfields_lib;
	int (*PQnfields)(const PGresult *res);

	uv_lib_t *PQntuples_lib;
	int (*PQntuples)(const PGresult *res);

	uv_lib_t *PQresultStatus_lib;
	ExecStatusType (*PQresultStatus)(const PGresult *res);

	uv_lib_t *PQstatus_lib;
	ConnStatusType (*PQstatus)(const PGconn *conn);

} pq_library;
