#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
void nats_varz_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "nats_varz", carg);
}

void nats_subsz_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "nats_subsz", carg);
}

void nats_connz_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(char*)*1);
	parsestring[0] = strdup("sum::nats_connz_connections(cid)");
	json_parser_entry(metrics, 1, parsestring, "nats_connz", carg);
	free(parsestring[0]);
	free(parsestring);
}

void nats_routez_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(char*)*1);
	parsestring[0] = strdup("sum::nats_routez_routes(rid)");
	json_parser_entry(metrics, 1, parsestring, "nats_routez", carg);
	free(parsestring[0]);
	free(parsestring);
}
