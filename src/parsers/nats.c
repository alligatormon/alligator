#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"
void nats_varz_handler(char *metrics, size_t size, client_info *cinfo)
{
	json_parser_entry(metrics, 0, NULL, "nats_varz");
}

void nats_subsz_handler(char *metrics, size_t size, client_info *cinfo)
{
	json_parser_entry(metrics, 0, NULL, "nats_subsz");
}

void nats_connz_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("sum::nats_connz_connections(cid)");
	json_parser_entry(metrics, 1, parsestring, "nats_connz");
	free(parsestring[0]);
	free(parsestring);
}

void nats_routez_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("sum::nats_routez_routes(rid)");
	json_parser_entry(metrics, 1, parsestring, "nats_routez");
	free(parsestring[0]);
	free(parsestring);
}
