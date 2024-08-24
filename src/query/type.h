#pragma once
#include "dstructures/tommy.h"
#include "events/context_arg.h"
#include <jansson.h>
typedef struct query_field
{
	char *field;
	int8_t type;
	union
	{
		double d;
		uint64_t u;
		int64_t i;
	};

	tommy_node node;
} query_field;

typedef struct query_node
{
	//char *field;
	char *expr;
	char *make;
	char *action;
	char *ns;
	alligator_ht *qf_hash;
	char *datasource;
	context_arg *carg;
	alligator_ht *labels;

	tommy_node node;
} query_node;

typedef struct query_ds
{
	char *datasource;
	alligator_ht *hash;
	tommy_node node;
} query_ds;

query_ds* query_get(char *datasource);
query_field* query_field_get(alligator_ht *qf_hash, char *key);
alligator_ht* query_get_field(json_t *jfield);
query_node *query_get_node(query_ds *qds, char *make);
void query_set_values(query_node *qn);
int query_del(json_t *query);
int query_push(json_t *query);
void query_stop();
void query_handler();
int query_compare(const void* arg, const void* obj);
int queryds_compare(const void* arg, const void* obj);
