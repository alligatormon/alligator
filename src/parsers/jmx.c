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
#define JMXEXEC "exec://java -Djava.class.path=/var/lib/alligator/ alligatorJmx "
extern aconf *ac;

string* jmx_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	//lang_options *lo = calloc(1, sizeof(*lo));
	//lo->key = "jmx";
	//lo->lang = "java";
	//lo->classpath = "-Djava.class.path=/var/lib/alligator/";
	//lo->classname = "alligatorJmx";
	//lo->method = "getJmx";
	////lo->arg = strdup("service:jmx:rmi:///jndi/rmi://127.0.0.1:12345/jmxrmi");
	//lo->arg = strdup(hi->url);
	//lo->carg = context_arg_json_fill(NULL, hi, NULL, "jmx", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0);
	//lang_push(lo);

	context_arg *carg = aggregator_oneshot(NULL, JMXEXEC, strlen(JMXEXEC), strdup(hi->url), strlen(hi->url), NULL, "jmx_handler", NULL, NULL, 0, NULL, NULL, 0, NULL);
	if (carg)
		carg->context_ttl = 0;
	return NULL;
}

void jmx_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("jmx");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = jmx_mesg;
	strlcpy(actx->handler[0].key,"jmx", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
