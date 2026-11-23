#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "metric/labels.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "common/logs.h"
#include "main.h"
#define VARNISH_METRIC_SIZE 1000

static void varnish_ensure_total(char *name, size_t sz)
{
	size_t len = strlen(name);

	if (len >= 6 && !strcmp(name + len - 6, "_total"))
		return;
	if (len + 6 >= sz)
		return;
	memcpy(name + len, "_total", 7);
}

void varnish_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root;
	json_error_t error;
	carglog(carg, L_DEBUG, "run varnish_handler\n");

	(void)size;
	root = json_loads(metrics, 0, &error);

	if (!root)
	{
		carglog(carg, L_ERROR, "varnish json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char metricname[VARNISH_METRIC_SIZE];
	strlcpy(metricname, "varnish_", 9);

	const char *key1;
	json_t *value_json1;
	json_object_foreach(root, key1, value_json1)
	{
		carglog(carg, L_DEBUG, "varnish key: %s\n", key1);
		if (json_typeof(value_json1) == JSON_OBJECT)
		{
			size_t offset = 0;
			char *tmp;
			while ((tmp = strstr(key1+offset, ".")))
				offset = tmp - key1 + 1;

			strlcpy(metricname+8, key1+offset, VARNISH_METRIC_SIZE-9);
			prometheus_metric_name_normalizer(metricname + 8, strlen(metricname + 8));
			carglog(carg, L_DEBUG, "varnish metricname: %s\n", metricname);

			json_t *mvalue_json = json_object_get(value_json1, "value");
			if (mvalue_json)
			{
				alligator_ht *lbl = alligator_ht_init(NULL);

				const char *key2;
				json_t *value_json2;
				json_object_foreach(value_json1, key2, value_json2)
				{
					if (json_typeof(value_json2) == JSON_STRING &&
					    strcmp(key2, "description") &&
					    strcmp(key2, "flag") &&
					    strcmp(key2, "format"))
					{
						carglog(carg, L_DEBUG, "\tvarnish key2: %s\n", key2);
						char *value_json2_str = (char*)json_string_value(value_json2);
						if (value_json2_str)
						{
							carglog(carg, L_DEBUG, "\t\tvarnish insert label %s\n", value_json2_str);
							labels_hash_insert_nocache(lbl, (char*)key2, value_json2_str);
						}
					}
				}

				json_t *flag_json = json_object_get(value_json1, "flag");
				const char *flag = flag_json ? json_string_value(flag_json) : NULL;
				int is_counter = (flag && flag[0] == 'c');

				json_t *desc_json = json_object_get(value_json1, "description");
				const char *desc = desc_json ? json_string_value(desc_json) : NULL;
				if (!desc || !*desc)
					desc = is_counter ? "Varnish cumulative statistic." : "Varnish instantaneous statistic.";

				if (is_counter)
					varnish_ensure_total(metricname, sizeof(metricname));

				uint8_t mtype = is_counter ? METRIC_TYPE_COUNTER : METRIC_TYPE_GAUGE;
				namespace_metric_family_set(NULL, carg, metricname, mtype, desc);

				if (json_typeof(mvalue_json) == JSON_INTEGER)
				{
					int64_t mvalue = json_integer_value(mvalue_json);
					metric_add(metricname, lbl, &mvalue, DATATYPE_INT, carg);
				}
				else if (json_typeof(mvalue_json) == JSON_REAL)
				{
					double mvalue = json_real_value(mvalue_json);
					metric_add(metricname, lbl, &mvalue, DATATYPE_DOUBLE, carg);
				}
				else
				{
					labels_hash_free(lbl);
				}
			}
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

void varnish_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("varnish");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = varnish_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"varnish", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
