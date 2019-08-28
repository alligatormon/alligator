#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
void mssql_handler(char *metrics, size_t size, context_arg *carg)
{
	printf("===========metrics %s\n", metrics);
}
