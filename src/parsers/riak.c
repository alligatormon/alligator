#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
void riak_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "riak", carg);
}
