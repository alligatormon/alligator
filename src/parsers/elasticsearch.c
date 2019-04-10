#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "dstructures/metric.h"
#include "events/client_info.h"
void elasticsearch_nodes_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("sum::elasticsearch_nodes(name)");
	json_parser_entry(metrics, 1, parsestring, "elasticsearch_nodes");
	free(parsestring[0]);
	free(parsestring);
	// wait for /ID/ support
}

void elasticsearch_cluster_handler(char *metrics, size_t size, client_info *cinfo)
{
	json_parser_entry(metrics, 0, NULL, "elasticsearch_cluster");
}

void elasticsearch_health_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("label::elasticsearch_health_status(status)");
	json_parser_entry(metrics, 1, parsestring, "elasticsearch_health");
	free(parsestring[0]);
	free(parsestring);
}
