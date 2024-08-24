#include "stdlib.h"
#include "query/type.h"
#include "action/type.h"
#include "metric/labels.h"
#include "metric/query.h"
#include "common/selector.h"
#include "main.h"
#include "common/url.h"
#include "common/json_parser.h"
#include "common/logs.h"
#include "events/context_arg.h"
#include "parsers/multiparser.h"
#include "common/http.h"

void action_query_foreach_process(query_struct *qs, action_node *an, void *val, int type)
{
	glog(L_INFO, "an %p: qs %p: qs->key %s, count: %"u64"\n", an, qs, qs->key, qs->count);
	if (!an)
		return;

	char key[255];
	snprintf(key, 254, "action:%s", an->name);
	namespace_struct *ns = insert_namespace(key);
	if (!ns)
		return;

	context_arg *current_carg = calloc(1, sizeof(*current_carg));
	current_carg->namespace = ns->key;

	glog(L_INFO, "insert metric name %s, namespace %s\n", qs->metric_name, key);
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
		glog(L_ERROR, "No action named '%s'\n", name);
		return;
	}

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

