#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "parsers/elasticsearch.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "scheduler/type.h"
#include "lang/type.h"
#include "main.h"

extern aconf *ac;
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
	lo->carg->no_metric = 1;
	lang_push_options(lo);

	scheduler_node* sn = scheduler_get(lo->key);
	if (!sn) {
		sn = calloc(1, sizeof(*sn));
		sn->name = strdup(lo->key);
		sn->period = ac->aggregator_repeat;
		sn->lang = strdup(lo->key);
		uint32_t hash = tommy_strhash_u32(0, sn->name);
		alligator_ht_insert(ac->scheduler, &(sn->node), sn, hash);
		scheduler_start(sn);
	}

	const char *module_key = lo->module;
	module_t *module = alligator_ht_search(ac->modules, module_compare, module_key, tommy_strhash_u32(0, module_key));
	if (!module)
	{
		module_t *module = calloc(1, sizeof(*module));
		module->key = strdup(module_key);
		module->path = strdup("/var/lib/alligator/mongo.so");
		alligator_ht_insert(ac->modules, &(module->node), module, tommy_strhash_u32(0, module->key));
	}

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
