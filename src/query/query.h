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
	char *expr;
	char *make;
	char *action;
	char *ns;
	//char *field;
	tommy_hashdyn *qf_hash;
	//tommy_hashdyn *qf;
	char *datasource;
	context_arg *carg;
	tommy_hashdyn *labels;

	tommy_node node;
} query_node;

typedef struct query_ds
{
	char *datasource;
	tommy_hashdyn *hash;
	tommy_node node;
} query_ds;

query_ds* query_get(char *datasource);
query_field* query_field_get(tommy_hashdyn *qf_hash, char *key);
tommy_hashdyn* query_get_field(json_t *jfield);
