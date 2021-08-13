#pragma once
#include "dstructures/tommy.h"
#include "events/context_arg.h"
#define ACTION_TYPE_SHELL 0

typedef struct action_node
{
	char *expr;
	size_t expr_len;
	char *name;
	char *ns;
	int serializer;
	uint8_t type;
	alligator_ht *af_hash;
	char *datasource;
	context_arg *carg;
	alligator_ht *labels;
	uint64_t follow_redirects;

	tommy_node node;
} action_node;

action_node* action_get(char *name);
void action_push(char *name, char *datasource, json_t *jexpr, json_t *jns, json_t *jtype, uint8_t follow_redirects, json_t *jserializer);
