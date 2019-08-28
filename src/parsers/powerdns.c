#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
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
		metric_add_auto(metricname, &tvalue, DATATYPE_INT, 0);
	}
	json_decref(root);
}
