#include "stdlib.h"
#include "query/type.h"
#include "action/type.h"
#include "metric/labels.h"
#include "metric/query.h"
#include "common/selector.h"
#include "main.h"
#include "common/url.h"
#include "common/json_query.h"
#include "common/logs.h"
#include "events/context_arg.h"
#include "parsers/multiparser.h"
#include "common/http.h"
#include "lang/type.h"

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

	string_tokens *ms = NULL;

	if (!mqc)
		mqc = query_context_new(NULL);

	string *body = metric_query_deserialize(1024, mqc, an->serializer, 0, namespace, &ms, an->engine, an->index_template);
	query_context_free(mqc);

	string *work_dir = NULL;
	if (an->work_dir)
		work_dir = string_string_init_dup(an->work_dir);

	int log_level = L_INFO;
	if (an->dry_run) {
		log_level = L_OFF;
	}
	if (!strncmp(an->expr, "exec://", 7))
	{
		glog(log_level, "run action exec %s with cmd: '%s' from directory '%s'\n", name, an->expr, work_dir);
		if (!an->dry_run)
			aggregator_oneshot(NULL, an->expr, an->expr_len, NULL, 0, NULL, "NULL", NULL, NULL, an->follow_redirects, NULL, body->s, body->l, work_dir, NULL); // params pass for exec in stdin
	}
	else if (!strncmp(an->expr, "http", 4))
	{
		host_aggregator_info *hi = parse_url(an->expr, an->expr_len);

		if ((an->serializer == METRIC_SERIALIZER_CLICKHOUSE) && ms)
		{
			for (uint64_t i = 0; i < ms->l; ++i)
			{
				char cl[20];
				snprintf(cl, 19, "%"u64, ms->str[i]->l);
				alligator_ht *env = alligator_ht_init(NULL);
				env_struct_push_alloc(env, "Content-Length", cl);

				char *key = malloc(256);
				snprintf(key, 256, "%s:clickhouse_action_query:%"u64, hi->host, ms->str[i]->l);

				char *http_data = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", NULL, "1.0", env, NULL, ms->str[i]);
				glog(log_level, "run action clickhouse %s\n", name);
				if (!an->dry_run)
					aggregator_oneshot(NULL, an->expr, an->expr_len, http_data, strlen(http_data), clickhouse_response_catch, "clickhouse_response_catch", NULL, key, an->follow_redirects, NULL, NULL, 0, NULL, NULL); // params pass for other in body

				alligator_ht_foreach_arg(env, env_struct_free, env);
				alligator_ht_done(env);
				free(env);
			}

			free(body->s);
		}
		else if ((an->serializer == METRIC_SERIALIZER_PG) && ms)
		{
			for (uint64_t i = 0; i < ms->l; ++i)
			{
				char cl[20];
				snprintf(cl, 19, "%"u64, ms->str[i]->l);
				alligator_ht *env = alligator_ht_init(NULL);
				env_struct_push_alloc(env, "Content-Length", cl);

				char *key = malloc(256);
				snprintf(key, 256, "%s:postgresql_action_query:%"u64, hi->host, ms->str[i]->l);
				printf("ms %s: %"u64"\n", ms->str[i]->s, ms->l);

				char *http_data = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", NULL, "1.0", env, NULL, ms->str[i]);
				glog(log_level, "run action pg %s\n", name);
				if (!an->dry_run)
					aggregator_oneshot(NULL, an->expr, an->expr_len, http_data, strlen(http_data), clickhouse_response_catch, "clickhouse_response_catch", NULL, key, an->follow_redirects, NULL, NULL, 0, NULL, NULL); // params pass for other in body

				alligator_ht_foreach_arg(env, env_struct_free, env);
				alligator_ht_done(env);
				free(env);
			}

			free(body->s);
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
			glog(log_level, "run action http %s\n", name);
			if (!an->dry_run)
				aggregator_oneshot(NULL, an->expr, an->expr_len, http_data, strlen(http_data), an->parser, an->parser_name, NULL, NULL, an->follow_redirects, NULL, NULL, 0, NULL, NULL); // params pass for other in body
			alligator_ht_foreach_arg(env, env_struct_free, env);
			alligator_ht_done(env);
			free(env);
		}
		url_free(hi);
	}
	else if (!strncmp(an->expr, "mongo", 5))
	{
		if (an->name)
		{
			char pdata[255];
			snprintf(pdata, 254, "{\"type\": \"insert\", \"db\": \"%s\"}", an->ns);
			string *parser_data = string_init_dup(pdata);
			glog(log_level, "run action lang %s\n", name);
			if (!an->dry_run)
				lang_run(an->name, body, parser_data, NULL);
			string_free(parser_data);
		}
	}
	else if (!strncmp(an->expr, "cassandra", 5))
	{
		if (an->name)
		{
			json_t *cassandra_json = json_object();

			json_t *insert = json_string("insert");
			json_array_object_insert(cassandra_json, "type", insert);

			json_t *queries = string_tokens_json(ms);
			json_array_object_insert(cassandra_json, "queries", queries);

			char *pdata = json_dumps(cassandra_json, 0);
			string *parser_data = string_init_dup(pdata);

			glog(log_level, "run action cassandra %s\n", name);
			if (!an->dry_run)
				lang_run(an->name, NULL, parser_data, NULL);
			string_free(parser_data);
		}
	}
	else
	{
		glog(log_level, "run action any %s with expr: '%s'\n", name, an->expr);
		if (!an->dry_run)
			aggregator_oneshot(NULL, an->expr, an->expr_len, body->s, body->l, NULL, "NULL", NULL, NULL, an->follow_redirects, NULL, NULL, 0, NULL, NULL); // params pass for other in body
	}

	if (ms)
		string_tokens_free(ms);

	free(body);
}

void action_namespaced_run(char *action_name, char *key, metric_query_context *mqc)
{
	char ns[255];
	snprintf(ns, 254, "action:%s", key);
	action_run_process(action_name, ns, mqc);
}

