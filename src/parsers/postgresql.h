#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <libpq-fe.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include "modules/modules.h"
char* postgresql_client(context_arg* carg);

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

	uv_lib_t *PQftype_lib;
	Oid (*PQftype)(const PGresult *res, int field_num);

	uv_lib_t *PQflush_lib;
	int (*PQflush)(PGconn *conn);

	uv_lib_t *PQgetResult_lib;
	PGresult* (*PQgetResult)(PGconn *conn);

	uv_lib_t *PQresetPoll_lib;
	PostgresPollingStatusType (*PQresetPoll)(PGconn *conn);

	uv_lib_t *PQconnectPoll_lib;
	PostgresPollingStatusType (*PQconnectPoll)(PGconn *conn);

	uv_lib_t *PQresetStart_lib;
	int (*PQresetStart)(PGconn *conn);

	uv_lib_t *PQsocket_lib;
	int (*PQsocket)(const PGconn *conn);

	uv_lib_t *PQconnectStart_lib;
	PGconn *(*PQconnectStart)(const char *conninfo);

	uv_lib_t *PQsendQueryParams_lib;
	int (*PQsendQueryParams)(PGconn *conn,
				  const char *command,
				  int nParams,
				  const Oid *paramTypes,
				  const char *const * paramValues,
				  const int *paramLengths,
				  const int *paramFormats,
				  int resultFormat);

	uv_lib_t *PQsendPrepare_lib;
	int (*PQsendPrepare)(PGconn *conn, const char *stmtName,
			  const char *query, int nParams,
			  const Oid *paramTypes);

	uv_lib_t *PQsendQueryPrepared_lib;
	int (*PQsendQueryPrepared)(PGconn *conn,
                                        const char *stmtName,
                                        int nParams,
                                        const char *const * paramValues,
                                        const int *paramLengths,
                                        const int *paramFormats,
                                        int resultFormat);

	uv_lib_t *PQconsumeInput_lib;
	int (*PQconsumeInput)(PGconn *conn);

	uv_lib_t *PQisBusy_lib;
	int (*PQisBusy)(PGconn *conn);

	uv_lib_t *PQconnectStartParams_lib;
	PGconn *(*PQconnectStartParams)(const char *const * keywords, const char *const * values, int expand_dbname);

	uv_lib_t *PQescapeIdentifier_lib;
	char *(*PQescapeIdentifier)(PGconn *conn, const char *str, size_t len);

	uv_lib_t *PQsendQuery_lib;
	int (*PQsendQuery)(PGconn *conn, const char *query);

	uv_lib_t *PQfreemem_lib;
	void (*PQfreemem)(void *ptr);

	uv_lib_t *PQnotifies_lib;
	PGnotify *(*PQnotifies)(PGconn *conn);

	uv_lib_t *PQexec_lib;
	PGresult *(*PQexec)(PGconn *conn, const char *query);
} pq_library;

void postgresql_client_del(context_arg* carg);
void postgresql_client_handler();
