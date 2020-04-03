#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
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

				char *key;
				json_t *value_json;
				json_object_foreach(labels, key, value_json)
				{
					char *value = (char*)json_string_value(value_json);
					if (value)
						labels_hash_insert_nocache(lbl, key, value);
				}

				metric_add(metricname, lbl, &tvalue, DATATYPE_INT, carg);
			}
			else
				metric_add_auto(metricname, &tvalue, DATATYPE_INT, carg);
		}
	}

	json_decref(root);
}
