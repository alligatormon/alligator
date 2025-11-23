#pragma once
#include "dstructures/tommy.h"
#include "events/context_arg.h"
#include "query/promql.h"
#define ACTION_TYPE_SHELL 0

typedef struct action_node
{
	char *expr;
	size_t expr_len;
	char *name;
	char *ns;
	string *work_dir;
	int serializer;
	alligator_ht *af_hash;
	//char *datasource;
	context_arg *carg;
	alligator_ht *labels;
	uint64_t follow_redirects;
	string *engine;
	string *index_template;
	uint8_t content_type_json;
	void *parser;
	char *parser_name;
	uint8_t dry_run;

	tommy_node node;
} action_node;

action_node* action_get(char *name);
void action_run_process(char *name, char *namespace, metric_query_context *mqc);
void action_del(json_t *action);
void action_push(json_t *action);
void action_query_foreach_process(query_struct *qs, action_node *an, void *val, int type);
void action_namespaced_run(char *action_name, char *key, metric_query_context *mqc);
int action_compare(const void* arg, const void* obj);
