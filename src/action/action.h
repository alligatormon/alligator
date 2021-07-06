#pragma once
#include "dstructures/tommy.h"
#include "events/context_arg.h"

typedef struct action_node
{
	char *expr;
	size_t expr_len;
	char *name;
	char *ns;
	int serializer;
	tommy_hashdyn *af_hash;
	char *datasource;
	context_arg *carg;
	tommy_hashdyn *labels;
	uint64_t follow_redirects;

	tommy_node node;
} action_node;

action_node* action_get(char *name);
