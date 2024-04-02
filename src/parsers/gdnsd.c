#include <stdio.h>
#include <jansson.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/json_parser.h"
#include "common/aggregator.h"
#include "main.h"
void gdnsd_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics+8, 0, NULL, "gdnsd", carg);
	carg->parser_status = 1;
}

int8_t gdnsd_validator(context_arg *carg, char *data, size_t size)
{
	json_error_t error;
	json_t *root = json_loads(data+8, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return 0;
	}
	return 1;
}


string* gdnsd_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("S\0\0\0\0\0\0\0", 8);
}

void gdnsd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("gdnsd");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = gdnsd_handler;
	actx->handler[0].validator = gdnsd_validator;
	actx->handler[0].mesg_func = gdnsd_mesg;
	strlcpy(actx->handler[0].key, "gdnsd", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
