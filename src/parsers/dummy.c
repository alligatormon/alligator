#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "main.h"
void dummy_handler(char *metrics, size_t size, context_arg *carg)
{
	puts("DUMMY");
	printf("====================(%zu)================\n%s\n======================================\n", size, metrics);
	carg->parser_status = 1;
}

string* dummy_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
	{
		return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
	}
	else
		return string_init_add_auto("");
}

void dummy_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("dummy");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = dummy_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = dummy_mesg;
	strlcpy(actx->handler[0].key,"dummy", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
