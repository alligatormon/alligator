#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "metric/namespace.h"
#include "common/validator.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "metric/metric_types.h"
#include "main.h"
#define CONSUL_METRIC_SIZE 1000
void consul_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root;
	json_error_t error;

	root = json_loads(metrics, 0, &error);

	if (!root)
	{
		carglog(carg, L_ERROR, "json error on line %d: %s\n", error.line, error.text);
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
			if (!tname)
				continue;

			json_t *jsonvalue;
			double tvalue;
			/* Gauges use Value; Counters and Samples expose Sum. Samples stay gauges. */
			uint8_t mtype = (j == 1) ? METRIC_TYPE_COUNTER : METRIC_TYPE_GAUGE;
			if (j == 0)
				jsonvalue = json_object_get(arr_obj, "Value");
			else
				jsonvalue = json_object_get(arr_obj, "Sum");
			tvalue = json_number_value(jsonvalue);

			size_t prefix_len = 0;
			if (strncmp(tname, "consul_", 7) != 0)
				prefix_len = strlcpy(metricname, "consul_", CONSUL_METRIC_SIZE);
			else
				metricname[0] = 0;
			size_t sz = strlcpy(metricname + prefix_len, tname, CONSUL_METRIC_SIZE - prefix_len);
			metric_name_normalizer(metricname, prefix_len + sz);
			if (mtype == METRIC_TYPE_COUNTER) {
				size_t nlen = strlen(metricname);
				if (nlen < 6 || strcmp(metricname + nlen - 6, "_total")) {
					if (nlen + 7 < CONSUL_METRIC_SIZE)
						memcpy(metricname + nlen, "_total", 7);
				}
			}
			namespace_metric_family_set(NULL, carg, metricname, mtype, "Consul telemetry metric exported from /v1/agent/metrics.");

			json_t *labels = json_object_get(arr_obj, "Labels");
			if (labels)
			{
				alligator_ht *lbl = alligator_ht_init(NULL);

				const char *key;
				json_t *value_json;
				json_object_foreach(labels, key, value_json)
				{
					char *value = (char*)json_string_value(value_json);
					if (value)
						labels_hash_insert_nocache(lbl, (char*)key, value);
				}

				metric_add(metricname, lbl, &tvalue, DATATYPE_DOUBLE, carg);
			}
			else
				metric_add_auto(metricname, &tvalue, DATATYPE_DOUBLE, carg);
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

string* consul_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

void consul_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("consul");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = consul_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = consul_mesg;
	strlcpy(actx->handler[0].key,"consul", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
