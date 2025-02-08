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

int mongodb_aggregator(context_arg *carg) {
	lang_options *lo = calloc(1, sizeof(*lo));
	lo->key = malloc(1024);
	if (carg->name)
		strcpy(lo->key, carg->name);
	else {
		strlcpy(lo->key, "mongodb:go:", 1024);
		strlcpy(lo->key + 11, carg->host, 1013);
	}
	lo->lang = "so";
	lo->method = "alligator_call";
	lo->module = "mongodb";
	lo->script = carg->data;
	lo->arg = strdup(carg->url);
	lo->carg = carg;
	printf("lo script is %s, carg %p, key %s\n", lo->script, lo->carg, lo->key);
	lo->carg->no_metric = 1;
	lang_push_options(lo);

    if (!carg->pquery_size) {
		carg->pquery_size = 1;
		carg->pquery[0] = strdup("");
	}

	scheduler_node* sn = scheduler_get(lo->key);
	if (!sn) {
		sn = calloc(1, sizeof(*sn));
		sn->name = strdup(lo->key);
		sn->period = carg->period;
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
	return 1;
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
	actx->handler[0].smart_aggregator_replace = mongodb_aggregator;
	strlcpy(actx->handler[0].key, "mongodb", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
