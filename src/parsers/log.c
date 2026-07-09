#include "parsers/multiparser.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/url.h"
#include "events/context_arg.h"
#include "main.h"

void log_handler(char *metrics, size_t size, context_arg *carg)
{
	(void)metrics;
	(void)size;
	if (carg)
		carg->parser_status = 1;
}

string* log_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;

	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, "", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
	else if (hi->query)
		return string_init_alloc(hi->query, 0);
	else
		return NULL;
}

void log_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("log");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);

	actx->handler[0].name = log_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = log_mesg;
	actx->handler[0].no_metric = 1;
	strlcpy(actx->handler[0].key, "log", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
