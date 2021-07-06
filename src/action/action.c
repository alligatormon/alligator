#include "stdlib.h"
#include "query/query.h"
#include "query/promql.h"
#include "action/action.h"
#include "metric/labels.h"
#include "metric/query.h"
#include "main.h"

int action_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((action_node*)obj)->name;
	return strcmp(s1, s2);
}

action_node* action_get(char *name)
{
	action_node *an = tommy_hashdyn_search(ac->action, action_compare, name, tommy_strhash_u32(0, name));
	if (an)
		return an;
	else
		return NULL;
}

void action_query_foreach_process(query_struct *qs, action_node *an, void *val, int type)
{
	if (ac->log_level > 2)
		printf("qs->key %s, count: %"u64"\n", qs->key, qs->count);

	char key[255];
	snprintf(key, 254, "action:%s", an->name);
	namespace_struct *ns = insert_namespace(key);
	if (!ns)
		return;

	context_arg *current_carg = calloc(1, sizeof(*current_carg));
	current_carg->namespace = ns->key;

	//printf("insert metric name %s, namespace %s\n", qs->metric_name, key);
	metric_add(qs->metric_name, qs->lbl, val, type, current_carg);
	free(current_carg);
}

void action_run_process(char *name)
{
	if (!name)
		return;

	action_node *an = action_get(name);
	if (!an)
		return;

	char key[255];
	snprintf(key, 254, "action:%s", an->name);

	metric_query_context *mqc = query_context_new(NULL);
	string *body = metric_query_deserialize(1024, mqc, an->serializer, 0, key);
	query_context_free(mqc);

	aggregator_oneshot(NULL, an->expr, an->expr_len, NULL, 0, NULL, "NULL", NULL, NULL, an->follow_redirects, NULL, body->s, body->l);
}

void query_res_foreach(void *funcarg, void* arg)
{
	query_struct *qs = arg;

	action_node *an = funcarg;

	if (ac->log_level > 2)
		printf("qs->key %s, count: %"u64"\n", qs->key, qs->count);

	char key[255];
	snprintf(key, 254, "action:%s", an->name);
	namespace_struct *ns = insert_namespace(key);
	if (!ns)
		return;

	context_arg *current_carg = calloc(1, sizeof(*current_carg));
	current_carg->namespace = ns->key;

	metric_add(qs->metric_name, qs->lbl, &qs->count, DATATYPE_UINT, current_carg);
	free(current_carg);
}

void action_push(char *name, char *datasource, json_t *jexpr, json_t *jns, json_t *jtype, json_t *jfollow_redirects, json_t *jserializer)
{
	action_node *an = calloc(1, sizeof(*an));

	if (jexpr)
	{
		char *expr = (char*)json_string_value(jexpr);
		an->expr = strdup(expr);
		an->expr_len = json_string_length(jexpr);
	}
	if (jns)
	{
		char *ns = (char*)json_string_value(jns);
		an->ns = strdup(ns);
	}
	if (jfollow_redirects)
	{
		char *follow_redirects = (char*)json_string_value(jfollow_redirects);
		an->follow_redirects = strtoll(follow_redirects, NULL, 10);
	}
	an->serializer = METRIC_SERIALIZER_OPENMETRICS;
	if (jserializer)
	{
		if(!strcmp(json_string_value(jserializer), "json"))
			an->serializer = METRIC_SERIALIZER_JSON;
		else if(!strcmp(json_string_value(jserializer), "dsv"))
			an->serializer = METRIC_SERIALIZER_DSV;
	}

	an->name = strdup(name);
	an->datasource = strdup(datasource);

	if (ac->log_level > 0)
		printf("create action node name '%s', expr '%s'\n", an->name, an->expr);

	tommy_hashdyn_insert(ac->action, &(an->node), an, tommy_strhash_u32(0, an->name));
}

void action_del(char *name, char *datasource)
{
	action_node *an = tommy_hashdyn_search(ac->action, action_compare, name, tommy_strhash_u32(0, name));
	if (an)
	{
		tommy_hashdyn_remove_existing(ac->action, &(an->node));

		if (an->expr)
			free(an->expr);
		if (an->name)
			free(an->name);
		if (an->ns)
			free(an->ns);
		if (an->af_hash)
			free(an->af_hash);
		free(an);
	}

	uint64_t count = tommy_hashdyn_count(ac->action);
	if (!count)
	{
		tommy_hashdyn_done(ac->action);
	}
}
