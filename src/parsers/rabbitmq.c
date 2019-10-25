#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
void rabbitmq_overview_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "rabbitmq", carg);
}
void rabbitmq_nodes_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(void*)*2);
	parsestring[0] = strndup("print::rabbitmq_nodes(name)", 17);
	parsestring[1] = strdup("print::rabbitmq_nodes_cluster_links(name/peer_name)");
	json_parser_entry(metrics, 2, parsestring, "rabbitmq_nodes", carg);
	free(parsestring[1]);
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_connections_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("sum::rabbitmq_connections(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_connections", carg);
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_channels_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("sum::rabbitmq_channels(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_channels", carg);
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_exchanges_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("sum::rabbitmq_exchanges(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_exchanges", carg);
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_queues_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("print::rabbitmq_queues(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_queues", carg);
	free(parsestring[0]);
	free(parsestring);
}
void rabbitmq_vhosts_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("print::rabbitmq_vhosts(name)");
	json_parser_entry(metrics, 1, parsestring, "rabbitmq_vhosts", carg);
	free(parsestring[0]);
	free(parsestring);
}
