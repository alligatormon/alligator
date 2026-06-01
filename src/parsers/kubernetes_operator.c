#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "metric/metric_types.h"
#include "main.h"
#include "kubernetes_common.h"
#include "kubernetes_watch.h"

static kubernetes_operator_opts *kubernetes_operator_global_opts;

void *kubernetes_operator_data_func(host_aggregator_info *hi, void *arg, void *data)
{
	kubernetes_operator_opts *opts = kubernetes_operator_opts_from_json(data, hi);
	if (kubernetes_operator_global_opts)
		kubernetes_operator_opts_free(kubernetes_operator_global_opts);
	kubernetes_operator_global_opts = opts;
	if (opts && opts->watch)
		kubernetes_watch_start(hi, opts);
	return opts;
}

void kubernetes_operator_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "kubernetes", METRIC_TYPE_GAUGE, "Kubernetes operator metrics.");

	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "kubernetes_operator json error on line %d: %s\n", error.line, error.text);
		return;
	}

	kubernetes_operator_opts *opts = carg->data;
	if (!opts)
		opts = kubernetes_operator_global_opts;

	kubernetes_reconcile_begin();

	json_t *items = json_object_get(root, "items");
	uint64_t items_size = json_array_size(items);
	for (uint64_t i = 0; i < items_size; i++)
	{
		json_t *item = json_array_get(items, i);
		kubernetes_reconcile_pod(item, carg, opts);
	}

	kubernetes_reconcile_end(carg);

	json_decref(root);
	carg->parser_status = 1;
}

string *kubernetes_operator_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	kubernetes_operator_opts *opts = arg;
	if (!opts)
		opts = kubernetes_operator_global_opts;
	if (opts && opts->watch && kubernetes_watch_active())
		return (string *)-1;
	return kubernetes_pods_list_mesg(hi, opts, env, proxy_settings);
}

void kubernetes_operator_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("kubernetes_operator");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->data_func = kubernetes_operator_data_func;

	actx->handler[0].name = kubernetes_operator_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = kubernetes_operator_mesg;
	strlcpy(actx->handler[0].key, "kubernetes_operator", sizeof(actx->handler[0].key));

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
