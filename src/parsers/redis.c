#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "dstructures/metric.h"
void redis_handler(char *metrics, size_t size, char *instance, int kind)
{
	char **maps = malloc(sizeof(char*)*1);
	maps[0] = strdup("role");
	char *res = selector_split_metric(metrics, size, "\r\n", 2, ":", 1, "redis_", 6, maps, 1);
	int64_t nval = 0;
	int64_t val = 1;
	if(strstr(res, ":master"))
	{
		metric_labels_add_lbl("redis_role", &val, ALLIGATOR_DATATYPE_INT, 0, "role", "master");
		metric_labels_add_lbl("redis_role", &nval, ALLIGATOR_DATATYPE_INT, 0, "role", "slave");
	}
	else
	{
		metric_labels_add_lbl("redis_role", &nval, ALLIGATOR_DATATYPE_INT, 0, "role", "master");
		metric_labels_add_lbl("redis_role", &val, ALLIGATOR_DATATYPE_INT, 0, "role", "slave");
	}
	free(res);
}
