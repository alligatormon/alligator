#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "parsers/elasticsearch.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "lang/lang.h"
#include "main.h"

string* mongodb_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	aggregate_context *actx = arg;
	lang_options *lo = calloc(1, sizeof(*lo));
	lo->key = malloc(1024);
	strlcpy(lo->key, "mongodb:go:", 1024);
	strlcpy(lo->key + 11, hi->host, 1013);
	lo->lang = "so";
	lo->method = "alligator_call";
	lo->module = "mongodb";
	lo->script = actx->data;
	printf("lo script is %s\n", lo->script);
	lo->arg = strdup(hi->url);
	lo->carg = context_arg_json_fill(NULL, hi, NULL, "mongodb", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0);
	lang_push_options(lo);

	return NULL;
}

void* mongodb_data(host_aggregator_info *hi, void *arg, void *data)
{
	json_t *aggregate = (json_t*)data;

	char *cstr = json_dumps(aggregate, JSON_INDENT(2));
	return cstr;
}

void mongodb_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("mongodb");
	actx->handlers = 1;
	actx->data_func = mongodb_data;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = mongodb_mesg;
	strlcpy(actx->handler[0].key, "mongodb", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
