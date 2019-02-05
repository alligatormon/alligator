#include <stdio.h>
#include <windows.h>
#include <sqltypes.h>
#include <sql.h>
#include <sqlext.h>

void show_error(unsigned int handletype, const SQLHANDLE handle)
{
	puts("100");
	SQLCHAR sqlstate[1024];
	puts("101");
	SQLCHAR message[1024];
	puts("102");
	if(SQL_SUCCESS == SQLGetDiagRec(handletype, handle, 1, sqlstate, NULL, message, 1024, NULL))
	{
	puts("103");
		printf("Message: %s, SQLSTATE: %s\n", sqlstate);
	}
	puts("104");
}

void sqlserver_handler()
{
	puts("1");
	SQLHANDLE sqlenvhandle;    
	puts("2");
	SQLHANDLE sqlconnectionhandle;
	puts("3");
	SQLHANDLE sqlstatementhandle;
	puts("4");
	SQLRETURN retcode;
	puts("5");

	do
	{
	puts("6");
		if(SQL_SUCCESS!=SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &sqlenvhandle))
			break;
	puts("7");
		if(SQL_SUCCESS!=SQLSetEnvAttr(sqlenvhandle,SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0)) 
			break;
	puts("8");
		if(SQL_SUCCESS!=SQLAllocHandle(SQL_HANDLE_DBC, sqlenvhandle, &sqlconnectionhandle))
			break;
	puts("9");
		SQLCHAR retconstring[1024];
	puts("10");

		switch(SQLDriverConnect (sqlconnectionhandle, NULL, 
				(SQLCHAR*)"DRIVER={SQL Server};SERVER=localhost, 1433;DATABASE=test;UID=test;PWD=test;", 
				SQL_NTS, retconstring, 1024, NULL,SQL_DRIVER_NOPROMPT))
		{
			case SQL_SUCCESS_WITH_INFO:
				show_error(SQL_HANDLE_DBC, sqlconnectionhandle);
	puts("11");
				break;
			case SQL_INVALID_HANDLE:
			case SQL_ERROR:
				show_error(SQL_HANDLE_DBC, sqlconnectionhandle);
				retcode = -1;
	puts("12");
				break;
			default:
	puts("13");
				break;
		}
	puts("14");

		if(retcode == -1)
			break;
	puts("15");
		if(SQL_SUCCESS!=SQLAllocHandle(SQL_HANDLE_STMT, sqlconnectionhandle, &sqlstatementhandle))
			break;
	puts("16");
		if(SQL_SUCCESS!=SQLExecDirect(sqlstatementhandle, (SQLCHAR*)"select * from testtable", SQL_NTS))
		{
			show_error(SQL_HANDLE_STMT, sqlstatementhandle);
			break;
	puts("17");
		}
		else
		{
	puts("18");
			char name[64];
	puts("19");
			char address[64];
	puts("20");
			int id;
	puts("21");
			while(SQLFetch(sqlstatementhandle)==SQL_SUCCESS)
			{
	puts("22");
				SQLGetData(sqlstatementhandle, 1, SQL_C_ULONG, &id, 0, NULL);
	puts("23");
				SQLGetData(sqlstatementhandle, 2, SQL_C_CHAR, name, 64, NULL);
	puts("24");
				SQLGetData(sqlstatementhandle, 3, SQL_C_CHAR, address, 64, NULL);
	puts("25");
				printf("id %s name %s address %s\n", id, name, address);
	puts("26");
			}
	puts("27");
		}
	puts("28");
	}
	while(FALSE);
	puts("29");
	SQLFreeHandle(SQL_HANDLE_STMT, sqlstatementhandle );
	puts("30");
	SQLDisconnect(sqlconnectionhandle);
	puts("31");
	SQLFreeHandle(SQL_HANDLE_DBC, sqlconnectionhandle);
	puts("32");
	SQLFreeHandle(SQL_HANDLE_ENV, sqlenvhandle);
	puts("33");
}
