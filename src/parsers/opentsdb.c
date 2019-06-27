#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"
#define OPENTSDB_METRIC_SIZE 1000
void opentsdb_handler(char *metrics, size_t size, client_info *cinfo)
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

	for (i = 0; i < arr_sz; i++)
	{
		json_t *arr_obj = json_array_get(root, i);
		if ( json_typeof(arr_obj) != JSON_OBJECT )
			return;

		json_t *jsonname = json_object_get(arr_obj, "metric");
		char *tname = (char*)json_string_value(jsonname);

		json_t *jsonvalue = json_object_get(arr_obj, "value");
		int64_t tvalue = atoll(json_string_value(jsonvalue));

		char metricname[OPENTSDB_METRIC_SIZE];
		strlcpy(metricname, "opentsdb_", 10);
		char *latency = strstr(tname, "latency_");
		if (latency)
		{
			//snprintf(metricname, latency - tname + 9, "opentsdb_%s", tname);
			strlcpy(metricname+9, tname, latency - tname);
			char *latencyname = latency+8;
			metric_add_labels(metricname, &tvalue, DATATYPE_INT, 0, "latency", latencyname);
		}
		else
		{
			size_t tname_size = strlen(tname);
			strlcpy(metricname+9, tname, tname_size + 1);
			metric_add_auto(metricname, &tvalue, DATATYPE_INT, 0);
		}
	}
	json_decref(root);
}
