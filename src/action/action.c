#include "stdlib.h"
#include "string.h"
#include "query/type.h"
#include "action/type.h"
#include "metric/labels.h"
#include "metric/query.h"
#include "common/selector.h"
#include "main.h"
#include "common/url.h"
#include "common/json_query.h"
#include "common/logs.h"
#include "common/aggregator.h"
#include "events/context_arg.h"
#include "parsers/multiparser.h"
#include "common/http.h"
#include "lang/type.h"

static void action_merge_action_env(alligator_ht *dst, action_node *an)
{
	if (an->env)
		alligator_ht_foreach_arg(an->env, env_struct_duplicate_foreach, dst);
}

static size_t action_http_query_size_with_body(char *http_data, size_t body_size)
{
	char *headers_end;
	if (!http_data)
		return 0;

	headers_end = strstr(http_data, "\r\n\r\n");
	if (headers_end)
		return (size_t)(headers_end - http_data) + 4 + body_size;

	headers_end = strstr(http_data, "\n\n");
	if (headers_end)
		return (size_t)(headers_end - http_data) + 2 + body_size;

	return strlen(http_data);
}

void action_query_foreach_process(query_struct *qs, action_node *an, void *val, int type)
{
	glog(L_DEBUG, "an %p: qs %p: qs->key %s, count: %"u64"\n", an, qs, qs->key, qs->count);
	if (!an)
		return;

	char key[255];
	snprintf(key, 254, "action:%s", an->name);
	namespace_struct *ns = insert_namespace(key, 0);
	if (!ns)
		return;

	context_arg *current_carg = calloc(1, sizeof(*current_carg));
	current_carg->namespace = ns->key;

	glog(L_DEBUG, "insert metric name %s, namespace %s\n", qs->metric_name, key);
	alligator_ht *duplabels = labels_dup(qs->lbl);
	metric_add(qs->metric_name, duplabels, val, type, current_carg);
	free(current_carg);
}

char *action_aggregator_key(const char *scheduler_name, const char *url, size_t url_len, const char *parser_name, const char *base_override)
{
	char base[512];
	base[0] = 0;

	if (base_override && *base_override)
		strlcpy(base, base_override, sizeof(base));
	else if (scheduler_name && *scheduler_name && url && url_len)
	{
		host_aggregator_info *hi = parse_url((char *)url, url_len);
		if (hi)
		{
			smart_aggregator_default_key(base,
				hi->transport_string,
				parser_name ? parser_name : "NULL",
				hi->host,
				hi->port,
				hi->query);
			url_free(hi);
		}
	}

	if (!scheduler_name || !*scheduler_name)
	{
		if (base[0])
			return strdup(base);
		return NULL;
	}

	char *key = malloc(512);
	if (!key)
		return NULL;
	if (base[0])
		snprintf(key, 512, "scheduler:%s:%s", scheduler_name, base);
	else
		snprintf(key, 512, "scheduler:%s", scheduler_name);
	smart_aggregator_key_normalize(key);
	return key;
}

static context_arg *action_run_oneshot(action_node *an, context_arg *oneshot_carg, char *url, size_t url_len, char *mesg, size_t mesg_len, void *handler, char *parser_name, char *key_base, void *data, char *s_stdin, size_t l_stdin, string *work_dir, alligator_ht *env, const char *scheduler_name)
{
	if (an->dry_run)
		return NULL;

	char *key = action_aggregator_key(scheduler_name, url, url_len, parser_name, key_base);
	return aggregator_oneshot(oneshot_carg, url, url_len, mesg, mesg_len, handler, parser_name, NULL, key, an->follow_redirects, data, s_stdin, l_stdin, work_dir, env);
}

// custom mqc
void action_run_process(char *name, char *namespace, metric_query_context *mqc, const char *scheduler_name)
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

	string *body = metric_query_deserialize(1024, mqc, an->serializer, 0, namespace, &ms, an->engine, an->index_template, an);
	query_context_free(mqc);

	string *work_dir = NULL;
	if (an->work_dir)
		work_dir = string_string_init_dup(an->work_dir);

	context_arg carg_hint = {0};
	context_arg *oneshot_carg = NULL;
	if (an->log_level_defined || an->log_ch)
	{
		if (an->log_level_defined)
			carg_hint.log_level = an->log_level;
		carg_hint.log_ch = an->log_ch;
		carg_hint.ttl = ac->ttl;
		oneshot_carg = &carg_hint;
	}

	int log_level = L_INFO;
	if (an->dry_run) {
		log_level = L_OFF;
	}
	if (!an->expr || !an->expr[0])
	{
		glog(L_FATAL, "action '%s' has no expr; add an expr directive (e.g. exec://..., http://..., udp://...)\n", name);
		if (ms)
			string_tokens_free(ms);
		free(body);
		if (work_dir)
			string_free(work_dir);
		return;
	}
	if (!strncmp(an->expr, "exec://", 7))
	{
		glog(log_level, "run action exec %s with cmd: '%s' from directory '%s'\n", name, an->expr, work_dir);
		action_run_oneshot(an, oneshot_carg, an->expr, an->expr_len, NULL, 0, NULL, "NULL", NULL, NULL, body->s, body->l, work_dir, an->env, scheduler_name);
	}
	else if (!strncmp(an->expr, "http", 4))
	{
		host_aggregator_info *hi = parse_url(an->expr, an->expr_len);

		if ((an->serializer == METRIC_SERIALIZER_CLICKHOUSE) && ms)
		{
			for (uint64_t i = 0; i < ms->l; ++i)
			{
				char cl[20];
				snprintf(cl, 19, "%zu", ms->str[i]->l);
				alligator_ht *env = alligator_ht_init(NULL);
				action_merge_action_env(env, an);
				env_struct_push_alloc(env, "Content-Length", cl);

				char key_base[256];
				snprintf(key_base, 256, "%s:clickhouse_action_query:%zu", hi->host, ms->str[i]->l);

				char *http_data = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", NULL, "1.0", env, NULL, ms->str[i]);
				glog(log_level, "run action clickhouse %s\n", name);
				action_run_oneshot(an, oneshot_carg, an->expr, an->expr_len, http_data, strlen(http_data), clickhouse_response_catch, "clickhouse_response_catch", key_base, NULL, NULL, 0, NULL, env, scheduler_name);

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
				snprintf(cl, 19, "%zu", ms->str[i]->l);
				alligator_ht *env = alligator_ht_init(NULL);
				action_merge_action_env(env, an);
				env_struct_push_alloc(env, "Content-Length", cl);

				char key_base[256];
				snprintf(key_base, 256, "%s:postgresql_action_query:%zu", hi->host, ms->str[i]->l);
				glog(log_level, "ms %s: %"u64"\n", ms->str[i]->s, ms->l);

				char *http_data = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", NULL, "1.0", env, NULL, ms->str[i]);
				glog(log_level, "run action pg %s\n", name);
				action_run_oneshot(an, oneshot_carg, an->expr, an->expr_len, http_data, strlen(http_data), clickhouse_response_catch, "clickhouse_response_catch", key_base, NULL, NULL, 0, NULL, env, scheduler_name);

				alligator_ht_foreach_arg(env, env_struct_free, env);
				alligator_ht_done(env);
				free(env);
			}

			free(body->s);
		}
		else
		{
			char cl[20];
			snprintf(cl, 19, "%zu", body->l);
			alligator_ht *env = alligator_ht_init(NULL);
			action_merge_action_env(env, an);
			env_struct_push_alloc(env, "Content-Length", cl);

			if (an->content_type_json)
				env_struct_push_alloc(env, "Content-Type", "application/json");
			else if (an->content_type_protobuf)
				env_struct_push_alloc(env, "Content-Type", "application/x-protobuf");
			else if (an->content_type_plain)
				env_struct_push_alloc(env, "Content-Type", "text/plain; charset=utf-8");

			char *http_data = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", NULL, "1.0", env, NULL, body);
			size_t http_data_size = action_http_query_size_with_body(http_data, body->l);
			free(body->s);
			glog(log_level, "run action http %s\n", name);
			action_run_oneshot(an, oneshot_carg, an->expr, an->expr_len, http_data, http_data_size, an->parser, an->parser_name, NULL, NULL, NULL, 0, NULL, env, scheduler_name);
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

			json_t *queries = ms ? string_tokens_json(ms) : json_array();
			json_array_object_insert(cassandra_json, "queries", queries);

			char *pdata = json_dumps(cassandra_json, 0);
			string *parser_data = string_init_dup(pdata);
			free(pdata);
			json_decref(cassandra_json);

			glog(log_level, "run action cassandra %s\n", name);
			if (!an->dry_run)
				lang_run(an->name, NULL, parser_data, NULL);
			string_free(parser_data);
		}
	}
	else if (!strncmp(an->expr, "udp", 3))
	{
		glog(log_level, "run action any %s with expr: '%s'\n", name, an->expr);
		context_arg *carg = action_run_oneshot(an, oneshot_carg, an->expr, an->expr_len, body->s, body->l, NULL, "NULL", NULL, NULL, NULL, 0, NULL, an->env, scheduler_name);
		if (carg)
			carg->timeout = 1000;
	}
	else
	{
		glog(log_level, "run action any %s with expr: '%s'\n", name, an->expr);
		action_run_oneshot(an, oneshot_carg, an->expr, an->expr_len, body->s, body->l, NULL, "NULL", NULL, NULL, NULL, 0, NULL, an->env, scheduler_name);
	}

	if (ms)
		string_tokens_free(ms);

	free(body);
}

void action_namespaced_run(char *action_name, char *key, metric_query_context *mqc)
{
	char ns[255];
	snprintf(ns, 254, "action:%s", key);
	action_run_process(action_name, ns, mqc, NULL);
}

