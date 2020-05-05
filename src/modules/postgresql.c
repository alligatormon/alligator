#ifdef __linux__
#include "modules/modules.h"
#include "modules/postgresql.h"

void pg_free(pq_library *pqlib)
{
	if (!pqlib)
		return;

	if (pqlib->PQclear)
	{
		free(pqlib->PQclear);
		uv_dlclose(pqlib->PQclear_lib);
	}
}

pq_library* pg_init(char *pqlib_path)
{
	pq_library *pqlib = calloc(1, sizeof(*pqlib));

	pqlib->PQclear = module_load(pqlib_path, "PQclear", &pqlib->PQclear_lib);
	if (!pqlib->PQclear)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQconnectPoll = (void*)module_load(pqlib_path, "PQconnectPoll", &pqlib->PQconnectPoll_lib);
	if (!pqlib->PQconnectPoll)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQconnectStart = (void*)module_load(pqlib_path, "PQconnectStart", &pqlib->PQconnectStart_lib);
	if (!pqlib->PQconnectStart)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQconsumeInput = (void*)module_load(pqlib_path, "PQconsumeInput", &pqlib->PQconsumeInput_lib);
	if (!pqlib->PQconsumeInput)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQerrorMessage = (void*)module_load(pqlib_path, "PQerrorMessage", &pqlib->PQerrorMessage_lib);
	if (!pqlib->PQerrorMessage)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQfinish = module_load(pqlib_path, "PQfinish", &pqlib->PQfinish_lib);
	if (!pqlib->PQfinish)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQflush = (void*)module_load(pqlib_path, "PQflush", &pqlib->PQflush_lib);
	if (!pqlib->PQflush)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQfname = (void*)module_load(pqlib_path, "PQfname", &pqlib->PQfname_lib);
	if (!pqlib->PQfname)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQftype = (void*)module_load(pqlib_path, "PQftype", &pqlib->PQftype_lib);
	if (!pqlib->PQftype)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQgetResult = (void*)module_load(pqlib_path, "PQgetResult", &pqlib->PQgetResult_lib);
	if (!pqlib->PQgetResult)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQgetvalue = (void*)module_load(pqlib_path, "PQgetvalue", &pqlib->PQgetvalue_lib);
	if (!pqlib->PQgetvalue)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQisBusy = (void*)module_load(pqlib_path, "PQisBusy", &pqlib->PQisBusy_lib);
	if (!pqlib->PQisBusy)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQnfields = (void*)module_load(pqlib_path, "PQnfields", &pqlib->PQnfields_lib);
	if (!pqlib->PQnfields)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQntuples = (void*)module_load(pqlib_path, "PQntuples", &pqlib->PQntuples_lib);
	if (!pqlib->PQntuples)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQresetPoll = (void*)module_load(pqlib_path, "PQresetPoll", &pqlib->PQresetPoll_lib);
	if (!pqlib->PQresetPoll)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQresetStart = (void*)module_load(pqlib_path, "PQresetStart", &pqlib->PQresetStart_lib);
	if (!pqlib->PQresetStart)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQresultStatus = (void*)module_load(pqlib_path, "PQresultStatus", &pqlib->PQresultStatus_lib);
	if (!pqlib->PQresultStatus)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQsendPrepare = (void*)module_load(pqlib_path, "PQsendPrepare", &pqlib->PQsendPrepare_lib);
	if (!pqlib->PQsendPrepare)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQsendQueryParams = (void*)module_load(pqlib_path, "PQsendQueryParams", &pqlib->PQsendQueryParams_lib);
	if (!pqlib->PQsendQueryParams)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQsendQueryPrepared = (void*)module_load(pqlib_path, "PQsendQueryPrepared", &pqlib->PQsendQueryPrepared_lib);
	if (!pqlib->PQsendQueryPrepared)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQsocket = (void*)module_load(pqlib_path, "PQsocket", &pqlib->PQsocket_lib);
	if (!pqlib->PQsocket)
	{
		pg_free(pqlib);
		return NULL;
	}

	pqlib->PQstatus = (void*)module_load(pqlib_path, "PQstatus", &pqlib->PQstatus_lib);
	if (!pqlib->PQstatus)
	{
		pg_free(pqlib);
		return NULL;
	}

	return pqlib;
}
#endif
