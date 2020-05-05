#pragma once
#ifdef __linux__
#include <uv.h>
#include <pquv.h>
#include <stdlib.h>

typedef struct pq_library {
	void (*PQclear)(PGresult *res);
	uv_lib_t *PQclear_lib;

	PostgresPollingStatusType (*PQconnectPoll)(PGconn *conn);
	uv_lib_t *PQconnectPoll_lib;

	PGconn* (*PQconnectStart)(const char *conninfo);
	uv_lib_t *PQconnectStart_lib;

	int (*PQconsumeInput)(PGconn *conn);
	uv_lib_t *PQconsumeInput_lib;

	char (*PQerrorMessage)(const PGconn *conn);
	uv_lib_t *PQerrorMessage_lib;

	void (*PQfinish)(PGconn *conn);
	uv_lib_t *PQfinish_lib;

	int (*PQflush)(PGconn *conn);
	uv_lib_t *PQflush_lib;

	char* (*PQfname)(const PGresult *res, int column_number);
	uv_lib_t *PQfname_lib;

	Oid (*PQftype)(const PGresult *res, int column_number);
	uv_lib_t *PQftype_lib;

	PGresult* (*PQgetResult)(PGconn *conn);
	uv_lib_t *PQgetResult_lib;

	char* (*PQgetvalue)(const PGresult *res, int row_number, int column_number);
	uv_lib_t *PQgetvalue_lib;

	int (*PQisBusy)(PGconn *conn);
	uv_lib_t *PQisBusy_lib;

	int (*PQnfields)(const PGresult *res);
	uv_lib_t *PQnfields_lib;

	int (*PQntuples)(const PGresult *res);
	uv_lib_t *PQntuples_lib;

	int (*PQresetStart)(PGconn *conn);
	uv_lib_t *PQresetStart_lib;

	PostgresPollingStatusType (*PQresetPoll)(PGconn *conn);
	uv_lib_t *PQresetPoll_lib;

	ExecStatusType (*PQresultStatus)(const PGresult *res);
	uv_lib_t *PQresultStatus_lib;

	int (*PQsendPrepare)(PGconn *conn, const char *stmtName, const char *query, int nParams, const Oid *paramTypes);
	uv_lib_t *PQsendPrepare_lib;

	int (*PQsendQueryParams)(PGconn *conn, const char *command, int nParams, const Oid *paramTypes, const char * const *paramValues, const int *paramLengths, const int *paramFormats, int resultFormat);
	uv_lib_t *PQsendQueryParams_lib;

	int (*PQsendQueryPrepared)(PGconn *conn, const char *stmtName, int nParams, const char * const *paramValues, const int *paramLengths, const int *paramFormats, int resultFormat);
	uv_lib_t *PQsendQueryPrepared_lib;

	int (*PQsocket)(const PGconn *conn);
	uv_lib_t *PQsocket_lib;

	ConnStatusType (*PQstatus)(const PGconn *conn);
	uv_lib_t *PQstatus_lib;
} pq_library;

pq_library* pg_init(char *pqlib_path);
#endif
