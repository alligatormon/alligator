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

string* jks_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	//lang_options *lo = calloc(1, sizeof(*lo));
	//lo->key = "jks";
	//lo->lang = "java";
	//lo->classpath = "-Djava.class.path=/var/lib/alligator/";
	//lo->classname = "alligatorJks";
	//lo->method = "walkJks";
	//lo->arg = strdup(hi->url);
	//lo->carg = context_arg_json_fill(NULL, hi, NULL, "jks", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0);
	//lang_push(lo);
	jks_push(hi->url, NULL, NULL, NULL, strdup(hi->url));

	return NULL;
}

void jks_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("jks");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = jks_mesg;
	strlcpy(actx->handler[0].key, "jks", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
