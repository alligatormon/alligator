#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#define HADOOP_METRIC_SIZE 1000
void hadoop_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root;
	json_error_t error;

	root = json_loads(metrics, 0, &error);

	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *beans = json_object_get(root, "beans");

	size_t i;
	size_t arr_sz = json_array_size(beans);
	char metricname[HADOOP_METRIC_SIZE];
	char *modelerType;
	strlcpy(metricname, "Hadoop_", 8);

	for (i = 0; i < arr_sz; i++)
	{
		json_t *arr_obj = json_array_get(beans, i);
		if ( json_typeof(arr_obj) != JSON_OBJECT )
			return;

		json_t *jobj;
		const char *val;
		modelerType = 0;
		json_t *j_modelerType = json_object_get(arr_obj, "modelerType");
		if (j_modelerType)
			modelerType = (char*)json_string_value(j_modelerType);
		json_object_foreach(arr_obj, val, jobj)
		{
			if (json_typeof(jobj) == JSON_REAL)
			{
				strlcpy(metricname+7, val, HADOOP_METRIC_SIZE-8);
				double tvalue = json_real_value(jobj);
				if (modelerType)
					metric_add_labels(metricname, &tvalue, DATATYPE_DOUBLE, carg, "modelerType", modelerType);
				else
					metric_add_auto(metricname, &tvalue, DATATYPE_DOUBLE, carg);
			}
			else if (json_typeof(jobj) == JSON_INTEGER)
			{
				strlcpy(metricname+7, val, HADOOP_METRIC_SIZE-8);
				int64_t tvalue = json_integer_value(jobj);
				if (modelerType)
					metric_add_labels(metricname, &tvalue, DATATYPE_INT, carg, "modelerType", modelerType);
				else
					metric_add_auto(metricname, &tvalue, DATATYPE_INT, carg);
			}
		}

	}
	json_decref(root);
}
