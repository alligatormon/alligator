#pragma once
#include "modules/postgresql.h"
extern pq_library *pqlib;

void (*PQclear)(PGresult *res) = pqlib ? pglib->PQclear : NULL;
PostgresPollingStatusType (*PQconnectPoll)(PGconn *conn) = pqlib ? pqlib->PQconnectPoll : NULL;
PGconn* (*PQconnectStart)(const char *conninfo) = pqlib ? pqlib->PQconnectStart : NULL;
int (*PQconsumeInput)(PGconn *conn) = pqlib ? pqlib->PQconsumeInput : NULL;
char (*PQerrorMessage)(const PGconn *conn) = pqlib ? pqlib->PQerrorMessage : NULL;
void (*PQfinish)(PGconn *conn) = pqlib ? pqlib->PQfinish : NULL;
int (*PQflush)(PGconn *conn) = pqlib ? pqlib->PQflush : NULL;
char* (*PQfname)(const PGresult *res, int column_number) = pqlib ? pqlib->PQfname : NULL;
Oid (*PQftype)(const PGresult *res, int column_number) = pqlib ? pqlib->PQftype : NULL;
PGresult* (*PQgetResult)(PGconn *conn) = pqlib ? pqlib->PQgetResult : NULL;
char* (*PQgetvalue)(const PGresult *res, int row_number, int column_number) = pqlib ? pqlib->PQgetResult : NULL;
int (*PQisBusy)(PGconn *conn) = pqlib ? pqlib->PQisBusy : NULL;
int (*PQnfields)(const PGresult *res) = pqlib ? pqlib->PQnfields : NULL;
int (*PQntuples)(const PGresult *res) = pqlib ? pqlib->PQntuples : NULL;
int (*PQresetStart)(PGconn *conn) = pqlib ? pqlib->PQresetStart : NULL;
PostgresPollingStatusType (*PQresetPoll)(PGconn *conn) = pqlib ? pqlib->PQresetPoll : NULL;
ExecStatusType (*PQresultStatus)(const PGresult *res) = pqlib ? pqlib->PQresultStatus : NULL;
