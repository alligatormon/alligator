#pragma once
//#include <my_global.h>
#include <mysql.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include "modules/modules.h"
#include "events/context_arg.h"
char* mysql_client(context_arg* carg);

typedef struct my_library {
	uv_lib_t *mysql_init_lib;
	MYSQL *(*mysql_init)(MYSQL *mysql);

	uv_lib_t *mysql_real_connect_lib;
	MYSQL* (*mysql_real_connect)(MYSQL *mysql, const char *host, const char *user, const char *passwd, const char *db, unsigned int port, const char *unix_socket, unsigned long clientflag);

	uv_lib_t *mysql_query_lib;
	int (*mysql_query)(MYSQL *mysql, const char *q);

	uv_lib_t *mysql_error_lib;
	const char* (*mysql_error)(MYSQL *mysql);

	uv_lib_t *mysql_close_lib;
	void (*mysql_close)(MYSQL *sock);

	uv_lib_t *mysql_store_result_lib;
	MYSQL_RES* (*mysql_store_result)(MYSQL *mysql);

	uv_lib_t *mysql_num_fields_lib;
	unsigned int (*mysql_num_fields)(MYSQL_RES *res);

	uv_lib_t *mysql_fetch_row_lib;
	MYSQL_ROW (*mysql_fetch_row)(MYSQL_RES *result);

	uv_lib_t *mysql_fetch_field_direct_lib;
	MYSQL_FIELD* (*mysql_fetch_field_direct)(MYSQL_RES *result, unsigned int fieldnr);

	uv_lib_t *mysql_free_result_lib;
	void (*mysql_free_result)(MYSQL_RES *result);
} my_library;

void mysql_client_del(context_arg* carg);
void mysql_client_handler();
