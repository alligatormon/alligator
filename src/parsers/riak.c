#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/json_query.h"
#include "common/http.h"
#include "main.h"
void riak_handler(char *metrics, size_t size, context_arg *carg)
{
	carg->parser_status = json_query(metrics, NULL, "riak", carg, carg->pquery, carg->pquery_size);
}

string* riak_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, "/stats", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

void riak_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("riak");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = riak_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = riak_mesg;
	strlcpy(actx->handler[0].key,"riak", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
