#pragma once

#include <stdint.h>
#include "events/context_arg.h"

typedef struct mysql_conn_t mysql_conn_t; /* opaque */

typedef struct mysql_row_t {
    uint8_t **fields;
    uint64_t *lengths;
    int column_count;
    int row_no;
    char **column_names;
    const uint8_t *column_types;
    const uint16_t *column_flags;
} mysql_row_t;

typedef void (*mysql_row_cb)(mysql_row_t *row, void *user_data);

int mysql2_connect(mysql_conn_t **pconn, context_arg *carg);

void mysql2_await_query(mysql_conn_t *conn, const char *sql, mysql_row_cb cb, void *user_data);

int mysql2_start_connect(mysql_conn_t **pconn, context_arg *carg);

int mysql2_is_ready(mysql_conn_t *conn);
int mysql2_is_failed(mysql_conn_t *conn);
