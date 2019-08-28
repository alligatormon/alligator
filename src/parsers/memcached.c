#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
void memcached_handler(char *metrics, size_t size, context_arg *carg)
{
	selector_split_metric(metrics, size, "\r\nSTAT ", 7, " ", 1, "memcached_", 6, NULL, 0);
}
