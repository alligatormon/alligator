#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"

void log_handler(char *metrics, size_t size, client_info *cinfo)
{
	printf("log_handler:\n%s\n", metrics);
	pcre_match_multi(cinfo->rematch, 2, metrics);
}
