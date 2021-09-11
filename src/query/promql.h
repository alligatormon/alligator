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
#define QUERY_OPERATOR_NOOP 0 // ==
#define QUERY_OPERATOR_EQ 1 // ==
#define QUERY_OPERATOR_NE 2 // !=
#define QUERY_OPERATOR_GT 3 // >
#define QUERY_OPERATOR_LT 4 // <
#define QUERY_OPERATOR_GE 5 // >=
#define QUERY_OPERATOR_LE 6 // <=

typedef struct metric_query_context {
	int func;
	string *groupkey;
	alligator_ht *lbl;
	char *query;
	size_t size;
	char *name;
	uint8_t op;
	double opval;
} metric_query_context;

//alligator_ht* promql_parser(alligator_ht* lbl, char *query, size_t size, char *name, int *func, string *groupkey, metric_query_context *mqc);
metric_query_context *promql_parser(alligator_ht* lbl, char *query, size_t size);
metric_query_context *query_context_new(char *name);
