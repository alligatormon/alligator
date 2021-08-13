#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "main.h"
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

string *nats_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, 0, NULL, env, proxy_settings, NULL), 0, 0);
}

string* nats_varz_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return nats_gen_url(hi, "/varz", env, proxy_settings); }
string* nats_connz_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return nats_gen_url(hi, "/connz", env, proxy_settings); }
string* nats_routez_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return nats_gen_url(hi, "/routez", env, proxy_settings); }
string* nats_subsz_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return nats_gen_url(hi, "/subsz", env, proxy_settings); }

void nats_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("nats");
	actx->handlers = 4;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = nats_varz_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = nats_varz_mesg;
	strlcpy(actx->handler[0].key,"nats_varz", 255);

	actx->handler[1].name = nats_connz_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = nats_connz_mesg;
	strlcpy(actx->handler[1].key,"nats_connz", 255);

	actx->handler[2].name = nats_routez_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = nats_routez_mesg;
	strlcpy(actx->handler[2].key,"nats_routez", 255);

	actx->handler[3].name = nats_subsz_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = nats_subsz_mesg;
	strlcpy(actx->handler[3].key,"nats_subsz", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
