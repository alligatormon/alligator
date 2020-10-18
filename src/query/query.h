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
