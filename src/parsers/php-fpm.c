#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "dstructures/metric.h"
#include "events/client_info.h"
void php_fpm_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("print::php_fpm_processes(pid)");
	json_parser_entry(metrics, 1, parsestring, "php_fpm");
	free(parsestring[0]);
	free(parsestring);
}
