#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/json_query.h"
#include "metric/metric_types.h"
#include "api/api.h"
#include "cluster/later.h"
#include "cluster/get.h"
#include "cluster/type.h"
#include "main.h"
#include "kubernetes_common.h"

static uint8_t kubernetes_ingress_sharedlock_allowed(context_arg *carg)
{
	if (!carg->cluster)
		return 1;

	cluster_node *cn = get_cluster_node_from_carg(carg);
	if (!cn || cn->type != CLUSER_TYPE_SHAREDLOCK)
		return 1;

	r_time time_now = setrtime();
	if (!cn->shared_lock_instance || !carg->instance)
		return 0;
	if (cn->ttl < time_now.sec)
		return 0;
	return string_compare(cn->shared_lock_instance, carg->instance, strlen(carg->instance));
}

void kubernetes_ingress_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!kubernetes_ingress_sharedlock_allowed(carg))
	{
		carg->parser_status = 1;
		return;
	}

	namespace_metric_family_set(NULL, carg, "kubernetes", METRIC_TYPE_GAUGE, "Kubernetes-discovered metrics.");

	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *items = json_object_get(root, "items");
	uint64_t items_size = json_array_size(items);
	uint64_t i, j;
	for (i = 0; i < items_size; i++)
	{
		json_t *item = json_array_get(items, i);
		json_t *spec = json_object_get(item, "spec");
		json_t *rules = json_object_get(spec, "rules");
		uint64_t rules_size = json_array_size(rules);
		for (j = 0; j < rules_size; j++)
		{
			json_t *rule = json_array_get(rules, j);
			json_t *host_json = json_object_get(rule, "host");
			char *host = (char*)json_string_value(host_json);
			if (carg->log_level > 0)
				printf("ingress host is %s\n", host);

			json_t *http = json_object_get(rule, "http");
			char *keyhost = malloc((json_string_length(host_json)+10));
			snprintf(keyhost, (json_string_length(host_json)+9), "http://%s", host);

			json_t *aggregate_root = json_object();
			json_t *aggregate_arr = json_array();
			json_t *aggregate_obj = json_object();
			json_t *aggregate_handler = json_string("http");
			json_t *aggregate_follow_redirects = json_integer(carg->follow_redirects-1);
			json_t *aggregate_url = json_string(keyhost);
			json_t *aggregate_name = json_string(keyhost);
			json_t *aggregate_add_label = json_object();
			json_array_object_insert(aggregate_root, "aggregate", aggregate_arr);
			json_array_object_insert(aggregate_arr, "", aggregate_obj);
			if (http)
				json_array_object_insert(aggregate_obj, "handler", aggregate_handler);
			else
			{
				json_decref(aggregate_root);
				continue;
			}

			json_array_object_insert(aggregate_obj, "url", aggregate_url);
			json_array_object_insert(aggregate_obj, "add_label", aggregate_add_label);
			json_array_object_insert(aggregate_obj, "follow_redirects", aggregate_follow_redirects);
			json_array_object_insert(aggregate_add_label, "name", aggregate_name);
			char *dvalue = json_dumps(aggregate_root, JSON_INDENT(2));
			if (carg->log_level > 1)
				puts(dvalue);
			http_api_v1(NULL, NULL, dvalue);
			free(dvalue);
			free(keyhost);
			json_decref(aggregate_root);
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

void kubernetes_endpoint_handler(char *metrics, size_t size, context_arg *carg)
{
	kubernetes_operator_opts opts = {0};
	opts.node_name = getenv("NODE_NAME");
	if (!opts.node_name || !opts.node_name[0])
		opts.node_name = getenv("ALLIGATOR_NODE_NAME");
	if (opts.node_name && opts.node_name[0])
		opts.node_local = 1;

	namespace_metric_family_set(NULL, carg, "kubernetes", METRIC_TYPE_GAUGE, "Kubernetes-discovered metrics.");

	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	kubernetes_reconcile_begin();

	json_t *items = json_object_get(root, "items");
	uint64_t items_size = json_array_size(items);
	for (uint64_t i = 0; i < items_size; i++)
		kubernetes_reconcile_pod(json_array_get(items, i), carg, &opts);

	kubernetes_reconcile_end(carg);

	json_decref(root);
	carg->parser_status = 1;
}

string* kubernetes_ingress_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	alligator_ht *merged_env = kubernetes_incluster_env(env);
	string *ret = string_init_add_auto(gen_http_query(0, hi->query, "/apis/networking.k8s.io/v1/ingresses",
		hi->host, "alligator", hi->auth, NULL, merged_env, proxy_settings, NULL));
	if (merged_env && merged_env != env)
	{
		alligator_ht_foreach_arg(merged_env, env_struct_free, merged_env);
		alligator_ht_done(merged_env);
		free(merged_env);
	}
	return ret;
}

string* kubernetes_endpoint_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	kubernetes_operator_opts opts = {0};
	opts.node_name = getenv("NODE_NAME");
	if (!opts.node_name || !opts.node_name[0])
		opts.node_name = getenv("ALLIGATOR_NODE_NAME");
	if (opts.node_name && opts.node_name[0])
		opts.node_local = 1;
	return kubernetes_pods_list_mesg(hi, &opts, env, proxy_settings);
}

void kubernetes_ingress_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("kubernetes_ingress");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = kubernetes_ingress_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = kubernetes_ingress_mesg;
	strlcpy(actx->handler[0].key,"kubernetes_ingress", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void kubernetes_endpoint_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("kubernetes_endpoint");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = kubernetes_endpoint_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = kubernetes_endpoint_mesg;
	strlcpy(actx->handler[0].key,"kubernetes_endpoint", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
