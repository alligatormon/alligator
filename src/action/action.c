#include "stdlib.h"
#include "query/type.h"
#include "action/action.h"
#include "metric/labels.h"
#include "metric/query.h"
#include "common/selector.h"
#include "main.h"
#include "common/url.h"
#include "common/json_parser.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "parsers/multiparser.h"

int action_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((action_node*)obj)->name;
	return strcmp(s1, s2);
}

action_node* action_get(char *name)
{
	action_node *an = alligator_ht_search(ac->action, action_compare, name, tommy_strhash_u32(0, name));
	if (an)
		return an;
	else
		return NULL;
}

void action_query_foreach_process(query_struct *qs, action_node *an, void *val, int type)
{
	if (ac->log_level > 2)
		printf("an %p: qs %p: qs->key %s, count: %"u64"\n", an, qs, qs->key, qs->count);
	if (!an)
		return;

	char key[255];
	snprintf(key, 254, "action:%s", an->name);
	namespace_struct *ns = insert_namespace(key);
	if (!ns)
		return;

	context_arg *current_carg = calloc(1, sizeof(*current_carg));
	current_carg->namespace = ns->key;

	//printf("insert metric name %s, namespace %s\n", qs->metric_name, key);
	alligator_ht *duplabels = labels_dup(qs->lbl);
	metric_add(qs->metric_name, duplabels, val, type, current_carg);
	free(current_carg);
}

// custom mqc
void action_run_process(char *name, char *namespace, metric_query_context *mqc)
{
	if (!name)
		return;

	action_node *an = action_get(name);
	if (!an)
	{
		if (ac->log_level > 0)
			printf("No action named '%s'\n", name);
		return;
	}

	//char key[255];
	//snprintf(key, 254, "action:%s", an->name);

	string **ms = NULL;
	uint64_t ms_size = 0;

	if (!mqc)
		mqc = query_context_new(NULL);
	string *body = metric_query_deserialize(1024, mqc, an->serializer, 0, namespace, &ms, &ms_size, an->engine, an->index_template);
	query_context_free(mqc);

	string *work_dir = NULL;
	if (an->work_dir)
		work_dir = string_string_init_dup(an->work_dir);

	if (!strncmp(an->expr, "exec://", 7))
	{
		aggregator_oneshot(NULL, an->expr, an->expr_len, NULL, 0, NULL, "NULL", NULL, NULL, an->follow_redirects, NULL, body->s, body->l, work_dir, NULL); // params pass for exec in stdin
	}
	else if (!strncmp(an->expr, "http", 4))
	{
		host_aggregator_info *hi = parse_url(an->expr, an->expr_len);

		if ((an->serializer == METRIC_SERIALIZER_CLICKHOUSE) && ms)
		{
			for (uint64_t i = 0; i < ms_size; ++i)
			{
				char cl[20];
				snprintf(cl, 19, "%"u64, ms[i]->l);
				alligator_ht *env = alligator_ht_init(NULL);
				env_struct_push_alloc(env, "Content-Length", cl);

				char *key = malloc(256);
				snprintf(key, 256, "%s:clickhouse_action_query:%"u64, hi->host, ms[i]->l);

				char *http_data = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", NULL, "1.0", env, NULL, ms[i]);
				aggregator_oneshot(NULL, an->expr, an->expr_len, http_data, strlen(http_data), clickhouse_response_catch, "clickhouse_response_catch", NULL, key, an->follow_redirects, NULL, NULL, 0, NULL, NULL); // params pass for other in body

				string_free(ms[i]);
				alligator_ht_foreach_arg(env, env_struct_free, env);
				alligator_ht_done(env);
				free(env);
			}

			free(body->s);
			free(ms);
		}
		else if ((an->serializer == METRIC_SERIALIZER_PG) && ms)
		{
			for (uint64_t i = 0; i < ms_size; ++i)
			{
				char cl[20];
				snprintf(cl, 19, "%"u64, ms[i]->l);
				alligator_ht *env = alligator_ht_init(NULL);
				env_struct_push_alloc(env, "Content-Length", cl);

				char *key = malloc(256);
				snprintf(key, 256, "%s:postgresql_action_query:%"u64, hi->host, ms[i]->l);
				printf("ms %s: %"u64"\n", ms[i]->s, ms_size);

				char *http_data = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", NULL, "1.0", env, NULL, ms[i]);
				aggregator_oneshot(NULL, an->expr, an->expr_len, http_data, strlen(http_data), clickhouse_response_catch, "clickhouse_response_catch", NULL, key, an->follow_redirects, NULL, NULL, 0, NULL, NULL); // params pass for other in body

				string_free(ms[i]);
				alligator_ht_foreach_arg(env, env_struct_free, env);
				alligator_ht_done(env);
				free(env);
			}

			free(body->s);
			free(ms);
		}
		else
		{
			char cl[20];
			snprintf(cl, 19, "%"u64, body->l);
			alligator_ht *env = alligator_ht_init(NULL);
			env_struct_push_alloc(env, "Content-Length", cl);
			printf("elasticsearch parser name is %s/%p\n", an->parser_name, an->parser);

			if (an->content_type_json)
				env_struct_push_alloc(env, "Content-Type", "application/json");

			char *http_data = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", NULL, "1.0", env, NULL, body);
			free(body->s);
			aggregator_oneshot(NULL, an->expr, an->expr_len, http_data, strlen(http_data), an->parser, an->parser_name, NULL, NULL, an->follow_redirects, NULL, NULL, 0, NULL, NULL); // params pass for other in body
			alligator_ht_foreach_arg(env, env_struct_free, env);
			alligator_ht_done(env);
			free(env);
		}
		url_free(hi);
	}
	else
	{
		aggregator_oneshot(NULL, an->expr, an->expr_len, body->s, body->l, NULL, "NULL", NULL, NULL, an->follow_redirects, NULL, NULL, 0, NULL, NULL); // params pass for other in body
	}

	printf("run %s\n", an->expr);
	free(body);
}

void action_namespaced_run(char *action_name, char *key, metric_query_context *mqc)
{
	char ns[255];
	snprintf(ns, 254, "action:%s", key);
	action_run_process(action_name, ns, mqc);
}

void action_push(json_t *action)
{
	json_t *jname = json_object_get(action, "name");
	if (!jname)
	{
		if (ac->log_level > 0)
			printf("create action failed, 'name' is empty\n");
		return;
	}
	char *name = (char*)json_string_value(jname);

	json_t *jdatasource = json_object_get(action, "datasource");
	if (!jdatasource)
	{
		if (ac->log_level > 0)
			printf("create action failed, 'datasource' is empty\n");
		return;
	}
	char *datasource = (char*)json_string_value(jdatasource);

	action_node *an = calloc(1, sizeof(*an));

	json_t *jexpr = json_object_get(action, "expr");
	if (jexpr)
	{
		char *expr = (char*)json_string_value(jexpr);
		an->expr = strdup(expr);
		an->expr_len = json_string_length(jexpr);
	}

	json_t *jns = json_object_get(action, "ns");
	if (jns)
	{
		char *ns = (char*)json_string_value(jns);
		an->ns = strdup(ns);
	}

	json_t *jwork_dir = json_object_get(action, "work_dir");
	if (jwork_dir)
	{
		an->work_dir = string_init_dupn((char*)json_string_value(jwork_dir), json_string_length(jwork_dir));
	}

	json_t *jengine = json_object_get(action, "engine");
	if (jengine)
	{
		an->engine = string_init_dupn((char*)json_string_value(jengine), json_string_length(jengine));
	}

	json_t *jindex_template = json_object_get(action, "index_template");
	if (jindex_template)
	{
		an->index_template = string_init_dupn((char*)json_string_value(jindex_template), json_string_length(jindex_template));
	}


	an->follow_redirects = config_get_intstr_json(action, "follow_redirects");

	json_t *jtype = json_object_get(action, "type");
	if (jtype)
	{
		char *type_str = (char*)json_string_value(jtype);
		if (!strcmp(type_str, "shell"))
			an->type = ACTION_TYPE_SHELL;
	}
	an->serializer = METRIC_SERIALIZER_OPENMETRICS;

	json_t *jserializer = json_object_get(action, "serializer");
	if (jserializer)
	{
		char *srlz = (char*)json_string_value(jserializer);
		if(!strcmp(srlz, "json"))
			an->serializer = METRIC_SERIALIZER_JSON;
		else if(!strcmp(srlz, "dsv"))
			an->serializer = METRIC_SERIALIZER_DSV;
		else if(!strcmp(srlz, "graphite"))
			an->serializer = METRIC_SERIALIZER_GRAPHITE;
		else if(!strcmp(srlz, "carbon2"))
			an->serializer = METRIC_SERIALIZER_CARBON2;
		else if(!strcmp(srlz, "influxdb"))
			an->serializer = METRIC_SERIALIZER_INFLUXDB;
		else if(!strcmp(srlz, "clickhouse"))
			an->serializer = METRIC_SERIALIZER_CLICKHOUSE;
		else if(!strcmp(srlz, "postgresql"))
			an->serializer = METRIC_SERIALIZER_PG;
		else if(!strcmp(srlz, "elasticsearch"))
		{
			an->serializer = METRIC_SERIALIZER_ELASTICSEARCH;
			an->content_type_json = 1;
			an->parser = elasticsearch_response_catch;
			an->parser_name = strdup("elasticsearch_response_catch");
		}
		else
		{
			if (ac->log_level > 0)
				printf("action %s error: unknown serializator '%s', use openmetrics by default\n", name, srlz);
		}
	}

	an->name = strdup(name);
	an->datasource = strdup(datasource);

	if (ac->log_level > 0)
		printf("create action node %p name '%s', expr '%s'\n", an, an->name, an->expr);

	alligator_ht_insert(ac->action, &(an->node), an, tommy_strhash_u32(0, an->name));
}

void action_del(json_t *action)
{
	json_t *jname = json_object_get(action, "name");
	if (!jname)
	{
		if (ac->log_level > 0)
			printf("delete action failed, 'name' is empty\n");
		return;
	}
	char *name = (char*)json_string_value(jname);

	action_node *an = alligator_ht_search(ac->action, action_compare, name, tommy_strhash_u32(0, name));
	if (an)
	{
		alligator_ht_remove_existing(ac->action, &(an->node));

		if (an->expr)
			free(an->expr);
		if (an->name)
			free(an->name);
		if (an->ns)
			free(an->ns);
		if (an->af_hash)
			free(an->af_hash);
		if (an->work_dir)
			string_free(an->work_dir);
		if (an->engine)
			string_free(an->engine);
		if (an->index_template)
			string_free(an->index_template);
		if (an->parser_name)
			free(an->parser_name);
		free(an);
	}

	uint64_t count = alligator_ht_count(ac->action);
	if (!count)
	{
		alligator_ht_done(ac->action);
	}
}
