#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
void beanstalkd_handler(char *metrics, size_t size, context_arg *carg)
{
	plain_parse(metrics, size, ": ", "\n", "beanstalkd_", 11, carg);
}
