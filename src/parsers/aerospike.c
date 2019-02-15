#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "dstructures/metric.h"
void aerospike_handler(char *metrics, size_t size, char *instance, int kind)
{
	char *clmetrics = selector_get_field_by_str(metrics, size, "statistics", 2, NULL);
	if ( clmetrics )
	{
		selector_split_metric(clmetrics, strlen(clmetrics), ";", 1, "=", 1, "aerospike_", 10, NULL, 0);
		free(clmetrics);
	}
}
