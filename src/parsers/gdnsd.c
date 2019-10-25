#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
void gdnsd_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics+8, 0, NULL, "gdnsd", carg);
}
