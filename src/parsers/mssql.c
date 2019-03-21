#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "dstructures/metric.h"
#include "events/client_info.h"
void mssql_handler(char *metrics, size_t size, client_info *cinfo)
{
	printf("===========metrics %s\n", metrics);
}
