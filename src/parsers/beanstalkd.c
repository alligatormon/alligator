#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"
void beanstalkd_handler(char *metrics, size_t size, client_info *cinfo)
{
	selector_split_metric(metrics, size, "\n", 1, ": ", 2, "beanstalkd_", 6, NULL, 0);
}
