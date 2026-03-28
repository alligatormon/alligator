#pragma once

#include <stdint.h>
#include "events/context_arg.h"

typedef struct cassandra_conn_t cassandra_conn_t;

typedef struct cassandra_row_t {
	uint8_t **fields;
	uint64_t *lengths;
	int column_count;
	int row_no;
	char **column_names;
	const uint16_t *column_types;
} cassandra_row_t;

typedef void (*cassandra_row_cb)(cassandra_row_t *row, void *user_data);

int cassandra2_start_connect(cassandra_conn_t **pconn, context_arg *carg);
void cassandra2_await_query(cassandra_conn_t *conn, const char *cql, cassandra_row_cb cb, void *user_data);
int cassandra2_use_keyspace(cassandra_conn_t *conn, const char *keyspace);

int cassandra2_is_ready(cassandra_conn_t *conn);
int cassandra2_is_failed(cassandra_conn_t *conn);
