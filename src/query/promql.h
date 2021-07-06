#pragma once
#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "dstructures/tommy.h"
#define QUERY_FUNC_NONE 0
#define QUERY_FUNC_COUNT 1
#define QUERY_FUNC_SUM 2
#define QUERY_FUNC_MIN 3
#define QUERY_FUNC_AVG 4
#define QUERY_FUNC_MAX 5
#define QUERY_FUNC_ABSENT 8
#define QUERY_FUNC_PRESENT 9

typedef struct metric_query_context {
	int func;
	string *groupkey;
	tommy_hashdyn *lbl;
	char *query;
	size_t size;
	char *name;
} metric_query_context;

//tommy_hashdyn* promql_parser(tommy_hashdyn* lbl, char *query, size_t size, char *name, int *func, string *groupkey, metric_query_context *mqc);
metric_query_context *promql_parser(tommy_hashdyn* lbl, char *query, size_t size);
metric_query_context *query_context_new(char *name);
