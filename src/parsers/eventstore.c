#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/aggregator.h"
#include "main.h"

void eventstore_stats_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("plainprint::eventstore_es_queue(name)");
	json_parser_entry(metrics, 1, parsestring, "eventstore", carg);
	free(parsestring[0]);
	free(parsestring);
}

void eventstore_projections_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("print::eventstore_projections(name)");
	json_parser_entry(metrics, 1, parsestring, "eventstore", carg);
	free(parsestring[0]);
	free(parsestring);
}

void eventstore_info_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(void*)*1);
	parsestring[0] = strdup("label::eventstore_state(state)");
	json_parser_entry(metrics, 1, parsestring, "eventstore", carg);
	free(parsestring[0]);
	free(parsestring);
}

string *eventstore_gen_url(host_aggregator_info *hi, char *addition)
{
	return string_init_add(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, 1), 0, 0);
}

string* eventstore_stats_mesg(host_aggregator_info *hi, void *arg) { return eventstore_gen_url(hi, "/stats"); }
string* eventstore_projections_mesg(host_aggregator_info *hi, void *arg) { return eventstore_gen_url(hi, "/projections/any"); }
string* eventstore_info_mesg(host_aggregator_info *hi, void *arg) { return eventstore_gen_url(hi, "/info"); }

void eventstore_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("eventstore");
	actx->handlers = 3;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = eventstore_stats_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = eventstore_stats_mesg;
	strlcpy(actx->handler[0].key, "eventstore_stats", 255);

	actx->handler[1].name = eventstore_projections_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = eventstore_projections_mesg;
	strlcpy(actx->handler[1].key, "eventstore_projections", 255);

	actx->handler[2].name = eventstore_info_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = eventstore_info_mesg;
	strlcpy(actx->handler[2].key, "eventstore_info", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
