#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
void php_fpm_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(char*)*1);
	parsestring[0] = strdup("print::php_fpm_processes(pid)");
	json_parser_entry(metrics, 1, parsestring, "php_fpm", carg);
	free(parsestring[0]);
	free(parsestring);
}
