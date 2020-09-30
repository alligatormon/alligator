#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
void dummy_handler(char *metrics, size_t size, context_arg *carg)
{
	puts("DUMMY");
	printf("====================(%zu)================\n%s\n======================================\n", size, metrics);
}

string* dummy_mesg(host_aggregator_info *hi, void *arg)
{
	return string_init_add("", 0, 0);
}

void dummy_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("dummy");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = dummy_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = dummy_mesg;
	strlcpy(actx->handler[0].key,"dummy", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
