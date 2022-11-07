#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"

void log_handler(char *metrics, size_t size, context_arg *carg)
{
	printf("log_handler:\n%s\n", metrics);
	pcre_match_multi(carg->rematch, 2, metrics);
	carg->parser_status = 1;
}
