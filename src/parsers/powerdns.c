#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "main.h"
#define POWERDNS_METRIC_SIZE 1000
void powerdns_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root;
	json_error_t error;

	root = json_loads(metrics, 0, &error);

	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	size_t i;
	size_t arr_sz = json_array_size(root);
	char metricname[POWERDNS_METRIC_SIZE];
	strlcpy(metricname, "powerdns_", 10);

	for (i = 0; i < arr_sz; i++)
	{
		json_t *arr_obj = json_array_get(root, i);
		if ( json_typeof(arr_obj) != JSON_OBJECT )
			return;

		json_t *jsonname = json_object_get(arr_obj, "name");
		char *tname = (char*)json_string_value(jsonname);
		json_t *jsonvalue = json_object_get(arr_obj, "value");
		int64_t tvalue = atoll(json_string_value(jsonvalue));

		strlcpy(metricname+9, tname, POWERDNS_METRIC_SIZE-10);
		metric_add_auto(metricname, &tvalue, DATATYPE_INT, carg);
	}
	json_decref(root);
}

string* powerdns_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, "/api/v1/servers/localhost/statistics", hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings), 0, 0);
}

void powerdns_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("powerdns");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = powerdns_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = powerdns_mesg;
	strlcpy(actx->handler[0].key,"powerdns", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
