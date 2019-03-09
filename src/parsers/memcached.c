#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "dstructures/metric.h"
void memcached_handler(char *metrics, size_t size, char *instance, int kind)
{
	selector_split_metric(metrics, size, "\r\nSTAT ", 7, " ", 1, "memcached_", 6, NULL, 0);
}
