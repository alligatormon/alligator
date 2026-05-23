#include <stdio.h>
#include <inttypes.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/aggregator.h"
#include "common/json_query.h"
#include "common/yaml.h"
#include "main.h"
void json_handler(char *metrics, size_t size, context_arg *carg)
{
	(void)size;
	namespace_metric_family_set(NULL, carg, "json", METRIC_TYPE_GAUGE, "JSON parser extracted numeric value.");
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (root) {
		json_decref(root);
		carg->parser_status = json_query(metrics, NULL, "json", carg, carg->pquery, carg->pquery_size);
		return;
	}

	/* NDJSON / single-document JSON before YAML (multi-line JSON is not YAML). */
	carg->parser_status = json_query(metrics, NULL, "json", carg, carg->pquery, carg->pquery_size);
	if (carg->parser_status)
		return;

	char *data = yaml_str_to_json_str(metrics);
	if (!data)
		return;

	carg->parser_status = json_query(data, NULL, "json", carg, carg->pquery, carg->pquery_size);
	free(data);
}

string* json_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

void json_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("jsonparse");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = json_handler;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = json_mesg;
	strlcpy(actx->handler[0].key, "jsonparse", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void json_query_handler(char *metrics, size_t size, context_arg *carg)
{
	(void)size;
	namespace_metric_family_set(NULL, carg, "json", METRIC_TYPE_GAUGE, "JSON parser extracted numeric value.");
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (root) {
		carg->parser_status = json_query(NULL, root, "json", carg, carg->pquery, carg->pquery_size);
		json_decref(root);
		return;
	}

	/* NDJSON / single-document JSON before YAML (multi-line JSON is not YAML). */
	carg->parser_status = json_query(metrics, NULL, "json", carg, carg->pquery, carg->pquery_size);
	if (carg->parser_status)
		return;

	char *data = yaml_str_to_json_str(metrics);
	if (!data)
		return;

	carg->parser_status = json_query(data, NULL, "json", carg, carg->pquery, carg->pquery_size);
	free(data);
}

void json_query_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("json_query");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = json_query_handler;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = json_mesg;
	strlcpy(actx->handler[0].key, "json_query", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
