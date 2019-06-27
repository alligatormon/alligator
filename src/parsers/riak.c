#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"
void riak_handler(char *metrics, size_t size, client_info *cinfo)
{
	json_parser_entry(metrics, 0, NULL, "riak");
}
