#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/pcre_parser.h"

void log_handler(char *metrics, size_t size, context_arg *carg)
{
	carglog(carg, L_INFO, "log_handler(%zu):'%s'\n", size, metrics);
	carg->parser_status = 1;
}
