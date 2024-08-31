#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/json_parser.h"
#include "common/http.h"
#include "common/logs.h"
#include "main.h"
// check for actual with https://docs.nats.io/running-a-nats-service/nats_admin/monitoring
void nats_varz_handler(char *metrics, size_t size, context_arg *carg)
{
	carg->parser_status = json_query(metrics, NULL, "nats_varz", carg, carg->pquery, carg->pquery_size);
}

void nats_subsz_handler(char *metrics, size_t size, context_arg *carg)
{
	carg->parser_status = json_query(metrics, NULL, "nats_subz", carg, carg->pquery, carg->pquery_size);
}

void nats_connz_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!carg->pquery) {
		carg->pquery = calloc(1, sizeof(void*));
		carg->pquery[0] = strdup(".connections.[cid]");
		carg->pquery_size = 1;
	}
	carg->parser_status = json_query(metrics, NULL, "nats_connz", carg, carg->pquery, carg->pquery_size);
}

void nats_routez_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!carg->pquery) {
		carg->pquery = calloc(1, sizeof(void*));
		carg->pquery[0] = strdup(".routes.[rid]");
		carg->pquery_size = 1;
	}
	carg->parser_status = json_query(metrics, NULL, "nats_routez", carg, carg->pquery, carg->pquery_size);
}

string *nats_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
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
