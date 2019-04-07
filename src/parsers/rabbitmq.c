#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "dstructures/metric.h"
#include "events/client_info.h"
void rabbitmq_overview_handler(char *metrics, size_t size, client_info *cinfo)
{
	json_parser_entry(metrics, 0, NULL, "rabbitmq");
}
void rabbitmq_nodes_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*2);
	parsestring[0] = strdup("print::rabbitmq_nodes(name)");
	parsestring[1] = strdup("print::rabbitmq_nodes_cluster_links(name/peer_name)");
	json_parser_entry(metrics, 2, parsestring, "rabbitmq_nodes");
	free(parsestring[1]);
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_connections_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("sum::rabbitmq_connections(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_connections");
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_channels_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("sum::rabbitmq_channels(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_channels");
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_exchanges_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("sum::rabbitmq_exchanges(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_exchanges");
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_queues_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("print::rabbitmq_queues(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_queues");
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_vhosts_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("print::rabbitmq_vhosts(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_vhosts");
	free(parsestring[0]);
	free(parsestring);
}
