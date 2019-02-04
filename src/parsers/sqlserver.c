#include "stdafx.h"
#include <iostream>
#include <windows.h>
#include <sqltypes.h>
#include <sql.h>
#include <sqlext.h>

void show_error(unsigned int handletype, const SQLHANDLE& handle)
{
	SQLCHAR sqlstate[1024];
	SQLCHAR message[1024];
	if(SQL_SUCCESS == SQLGetDiagRec(handletype, handle, 1, sqlstate, NULL, message, 1024, NULL))
	printf("Message: %s, SQLSTATE: %s\n", sqlstate);
}

void sqlserver_handler()
{
	SQLHANDLE sqlenvhandle;    
	SQLHANDLE sqlconnectionhandle;
	SQLHANDLE sqlstatementhandle;
	SQLRETURN retcode;

	do
	{
		if(SQL_SUCCESS!=SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &sqlenvhandle))
			break;
		if(SQL_SUCCESS!=SQLSetEnvAttr(sqlenvhandle,SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0)) 
			break;
		if(SQL_SUCCESS!=SQLAllocHandle(SQL_HANDLE_DBC, sqlenvhandle, &sqlconnectionhandle))
			break;
		SQLCHAR retconstring[1024];

		switch(SQLDriverConnect (sqlconnectionhandle, NULL, 
				(SQLCHAR*)"DRIVER={SQL Server};SERVER=localhost, 1433;DATABASE=test;UID=test;PWD=test;", 
				SQL_NTS, retconstring, 1024, NULL,SQL_DRIVER_NOPROMPT))
		{
			case SQL_SUCCESS_WITH_INFO:
				show_error(SQL_HANDLE_DBC, sqlconnectionhandle);
				break;
			case SQL_INVALID_HANDLE:
			case SQL_ERROR:
				show_error(SQL_HANDLE_DBC, sqlconnectionhandle);
				retcode = -1;
				break;
			default:
				break;
		}

		if(retcode == -1)
			break;
		if(SQL_SUCCESS!=SQLAllocHandle(SQL_HANDLE_STMT, sqlconnectionhandle, &sqlstatementhandle))
			break;
		if(SQL_SUCCESS!=SQLExecDirect(sqlstatementhandle, (SQLCHAR*)"select * from testtable", SQL_NTS))
		{
			show_error(SQL_HANDLE_STMT, sqlstatementhandle);
			break;
		}
		else
		{
			char name[64];
			char address[64];
			int id;
			while(SQLFetch(sqlstatementhandle)==SQL_SUCCESS)
			{
				SQLGetData(sqlstatementhandle, 1, SQL_C_ULONG, &id, 0, NULL);
				SQLGetData(sqlstatementhandle, 2, SQL_C_CHAR, name, 64, NULL);
				SQLGetData(sqlstatementhandle, 3, SQL_C_CHAR, address, 64, NULL);
				printf("id %s name %s address %s\n", id, name, address);
			}
		}
	}
	while(FALSE);
	SQLFreeHandle(SQL_HANDLE_STMT, sqlstatementhandle );
	SQLDisconnect(sqlconnectionhandle);
	SQLFreeHandle(SQL_HANDLE_DBC, sqlconnectionhandle);
	SQLFreeHandle(SQL_HANDLE_ENV, sqlenvhandle);
}
