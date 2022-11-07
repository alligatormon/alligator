#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
void ntpd_handler(char *metrics, size_t size, context_arg *carg)
{
	puts("ntpd");
	printf("====================(%zu)================\n%s\n======================================\n", size, metrics);
	carg->parser_status = 1;
}

string* ntpd_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add("\26\2\0\2\0\0\304\"\0\0\0\0", 12, 0);
}

void ntpd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("ntpd");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = ntpd_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = ntpd_mesg;
	strlcpy(actx->handler[0].key,"ntpd", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
