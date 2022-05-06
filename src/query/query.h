#include "dstructures/tommy.h"
#include "events/context_arg.h"
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
