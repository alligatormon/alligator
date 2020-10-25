#include "dstructures/tommy.h"
typedef struct query_node
{
	char *expr;
	char *make;
	char *action;
	char *field;
	char *datasource;

	tommy_node node;
} query_node;

typedef struct query_ds
{
	char *datasource;
	tommy_hashdyn *hash;
	tommy_node node;
} query_ds;

query_ds* query_get(char *datasource);
