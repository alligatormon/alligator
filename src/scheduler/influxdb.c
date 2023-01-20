#include <stdio.h>
#include <jansson.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
void influxdb_handler(char *metrics, size_t size, context_arg *carg)
{
	string *body = string_new();
	metric_str_build(0, body);
	printf("body is %s\n", body->s);
}

int8_t influxdb_validator(context_arg *carg, char *data, size_t size)
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


string* influxdb_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("S\0\0\0\0\0\0\0", 8);
}

void influxdb_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("influxdb");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = influxdb_handler;
	actx->handler[0].validator = influxdb_validator;
	actx->handler[0].mesg_func = influxdb_mesg;
	strlcpy(actx->handler[0].key, "influxdb", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
