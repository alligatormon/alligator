#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "main.h"
#define CONSUL_METRIC_SIZE 1000
void consul_handler(char *metrics, size_t size, context_arg *carg)
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
	char metricname[CONSUL_METRIC_SIZE];

	char **dirtypes = (char *[]){"Gauges", "Counters", "Samples"};

	uint64_t j;
	for (j=0; j<3; j++)
	{
		json_t *direct = json_object_get(root, dirtypes[j]);
		size_t arr_sz = json_array_size(direct);

		for (i = 0; i < arr_sz; i++)
		{
			json_t *arr_obj = json_array_get(direct, i);
			if ( json_typeof(arr_obj) != JSON_OBJECT )
				return;

			json_t *jsonname = json_object_get(arr_obj, "Name");
			char *tname = (char*)json_string_value(jsonname);

			json_t *jsonvalue;
			int64_t tvalue;
			if (j == 0)
			{
				jsonvalue = json_object_get(arr_obj, "Value");
				tvalue = json_integer_value(jsonvalue);
			}
			else if (j > 0)
			{
				jsonvalue = json_object_get(arr_obj, "Sum");
				tvalue = json_integer_value(jsonvalue);
			}

			size_t sz = strlcpy(metricname, tname, CONSUL_METRIC_SIZE);
			metric_name_normalizer(metricname, sz);

			json_t *labels = json_object_get(arr_obj, "Labels");
			if (labels)
			{
				tommy_hashdyn *lbl = malloc(sizeof(*lbl));
				tommy_hashdyn_init(lbl);

				const char *key;
				json_t *value_json;
				json_object_foreach(labels, key, value_json)
				{
					char *value = (char*)json_string_value(value_json);
					if (value)
						labels_hash_insert_nocache(lbl, (char*)key, value);
				}

				metric_add(metricname, lbl, &tvalue, DATATYPE_INT, carg);
			}
			else
				metric_add_auto(metricname, &tvalue, DATATYPE_INT, carg);
		}
	}

	json_decref(root);
}

string* consul_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1, "1.0", env, proxy_settings), 0, 0);
}

void consul_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("consul");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = consul_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = consul_mesg;
	strlcpy(actx->handler[0].key,"consul", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
