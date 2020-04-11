#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#define VARNISH_METRIC_SIZE 1000
void varnish_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root;
	json_error_t error;
	puts(metrics);

	root = json_loads(metrics, 0, &error);

	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char metricname[VARNISH_METRIC_SIZE];
	strlcpy(metricname, "varnish_", 9);

	const char *key1;
	json_t *value_json1;
	json_object_foreach(root, key1, value_json1)
	{
		printf("key: %s\n", key1);
		if (json_typeof(value_json1) == JSON_OBJECT)
		{
			size_t offset = 0;
			char *tmp = (char*)key1;
			while ((tmp = strstr(key1+offset, ".")))
				offset = tmp - key1 + 1;

			strlcpy(metricname+8, key1+offset, VARNISH_METRIC_SIZE-9);
			printf("metricname: %s\n", metricname);

			json_t *mvalue_json = json_object_get(value_json1, "value");
			if (mvalue_json)
			{
				tommy_hashdyn *lbl = malloc(sizeof(*lbl));
				tommy_hashdyn_init(lbl);

				const char *key2;
				json_t *value_json2;
				json_object_foreach(value_json1, key2, value_json2)
				{
					if (json_typeof(value_json2) == JSON_STRING && strcmp(key2, "description"))
					{
						printf("\tkey2: %s\n", key2);
						char *value_json2_str = (char*)json_string_value(value_json2);
						if (value_json2_str)
						{
							printf("\t\tinsert label %s\n", value_json2_str);
							labels_hash_insert_nocache(lbl, (char*)key2, value_json2_str);
						}
					}
				}
				if (json_typeof(mvalue_json) == JSON_INTEGER)
				{
					int64_t mvalue = json_integer_value(mvalue_json);
					metric_add(metricname, lbl, &mvalue, DATATYPE_INT, carg);
				}
				else if (json_typeof(mvalue_json) == JSON_REAL)
				{
					double mvalue = json_integer_value(mvalue_json);
					metric_add(metricname, lbl, &mvalue, DATATYPE_DOUBLE, carg);
				}
			}
		}
	}

	json_decref(root);
}
