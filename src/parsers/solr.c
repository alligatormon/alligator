#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#define SOLR_METRIC_SIZE 1000
void solr_handler(char *metrics, size_t size, context_arg *carg)
{
	puts(metrics);
	//json_t *root;
	//json_error_t error;

	//root = json_loads(metrics, 0, &error);

	//if (!root)
	//{
	//	fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
	//	return;
	//}

	//size_t i;
	//char metricname[SOLR_METRIC_SIZE];
	//char metricname2[SOLR_METRIC_SIZE];
	//strlcpy(metricname, "Nifi_", 6);
	//strlcpy(metricname2, "Nifi_", 6);

	//json_t *system_diagnostics = json_object_get(root, "systemDiagnostics");
	//if (!system_diagnostics)
	//	return;

	//json_t *aggregate_snapshot = json_object_get(system_diagnostics, "aggregateSnapshot");
	//if (!aggregate_snapshot)
	//	return;


	//const char *key1;
	//json_t *value_json1;
	//json_object_foreach(aggregate_snapshot, key1, value_json1)
	//{
	//	if ( json_typeof(value_json1) == JSON_INTEGER )
	//	{
	//		int64_t tvalue = json_integer_value(value_json1);
	//		strlcpy(metricname+5, key1, SOLR_METRIC_SIZE-5);
	//		metric_add_auto(metricname, &tvalue, DATATYPE_INT, carg);
	//	}
	//	if ( json_typeof(value_json1) == JSON_REAL )
	//	{
	//		double dvalue = json_real_value(value_json1);
	//		strlcpy(metricname+5, key1, SOLR_METRIC_SIZE-5);
	//		metric_add_auto(metricname, &dvalue, DATATYPE_DOUBLE, carg);
	//	}
	//	else if ( json_typeof(value_json1) == JSON_OBJECT )
	//	{
	//		size_t size1 = strlcpy(metricname2+5, key1, SOLR_METRIC_SIZE)+5;
	//		metricname2[size1++] = '_';
	//		const char *key2;
	//		json_t *value_json2;

	//		json_object_foreach(value_json1, key2, value_json2)
	//		{
	//			if ( json_typeof(value_json2) == JSON_INTEGER )
	//			{
	//				int64_t tvalue = json_integer_value(value_json2);
	//				strlcpy(metricname2+size1, key2, SOLR_METRIC_SIZE-size1);
	//				metric_add_auto(metricname2, &tvalue, DATATYPE_INT, carg);
	//			}
	//			if ( json_typeof(value_json2) == JSON_REAL )
	//			{
	//				double dvalue = json_real_value(value_json2);
	//				strlcpy(metricname2+size1, key2, SOLR_METRIC_SIZE-size1);
	//				metric_add_auto(metricname2, &dvalue, DATATYPE_DOUBLE, carg);
	//			}
	//			if ( json_typeof(value_json2) == JSON_STRING )
	//			{
	//				char *customkey = (char*)json_string_value(value_json2);
	//				if (!strcmp(key2, "utilization"))
	//				{
	//					double dvalue = atof(customkey);
	//					strlcpy(metricname2+size1, key2, SOLR_METRIC_SIZE-size1);

	//					metric_add_auto(metricname2, &dvalue, DATATYPE_DOUBLE, carg);
	//				}
	//			}
	//		}
	//	}
	//	else if ( json_typeof(value_json1) == JSON_ARRAY )
	//	{
	//		size_t arr_sz = json_array_size(value_json1);
	//		size_t size1 = strlcpy(metricname2+5, key1, SOLR_METRIC_SIZE)+5;
	//		metricname2[size1++] = '_';

	//		const char *key2;
	//		json_t *value_json2;

	//		for (i = 0; i < arr_sz; i++)
	//		{
	//			json_t *arr_obj = json_array_get(value_json1, i);
	//			json_t *identifier_json = json_object_get(arr_obj, "identifier");
	//			if (!identifier_json)
	//				identifier_json = json_object_get(arr_obj, "name");
	//			if (!identifier_json)
	//				continue;

	//			char *identifier = (char*)json_string_value(identifier_json);

	//			json_object_foreach(arr_obj, key2, value_json2)
	//			{
	//				if ( json_typeof(value_json2) == JSON_INTEGER )
	//				{
	//					int64_t tvalue = json_integer_value(value_json2);
	//					strlcpy(metricname2+size1, key2, SOLR_METRIC_SIZE-size1);
	//					metric_add_labels(metricname2, &tvalue, DATATYPE_INT, carg, "identifier", identifier);
	//				}
	//				if ( json_typeof(value_json2) == JSON_REAL )
	//				{
	//					double dvalue = json_real_value(value_json2);
	//					strlcpy(metricname2+size1, key2, SOLR_METRIC_SIZE-size1);
	//					metric_add_labels(metricname2, &dvalue, DATATYPE_DOUBLE, carg, "identifier", identifier);
	//				}
	//				if ( json_typeof(value_json2) == JSON_STRING )
	//				{
	//					char *customkey = (char*)json_string_value(value_json2);
	//					if (!strcmp(key2, "utilization"))
	//					{
	//						double dvalue = atof(customkey);
	//						strlcpy(metricname2+size1, key2, SOLR_METRIC_SIZE-size1);

	//						metric_add_auto(metricname2, &dvalue, DATATYPE_DOUBLE, carg);
	//					}
	//				}
	//			}
	//		}
	//	}
	//	if ( json_typeof(value_json1) == JSON_STRING )
	//	{
	//		char *customkey = (char*)json_string_value(value_json1);
	//		if ((!strcmp(key1, "heapUtilization")) || (!strcmp(key1,"nonHeapUtilization")))
	//		{
	//			double dvalue = atof(customkey);
	//			if (dvalue < 0)
	//				continue;

	//			strlcpy(metricname+5, key1, SOLR_METRIC_SIZE-5);

	//			metric_add_auto(metricname, &dvalue, DATATYPE_DOUBLE, carg);
	//		}
	//	}
	//}

	//json_decref(root);
	//carg->parser_status = 1;
}
