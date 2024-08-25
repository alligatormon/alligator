#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/selector.h"
#include "common/http.h"
#include "lang/type.h"
#include "main.h"

void lang_parser_handler(char *metrics, size_t size, context_arg *carg)
{
	if (carg->log_level > 3)
	{
		puts("====lang_parser_handler===============");
		printf("'%s'\n", metrics);
		printf("carg->lang '%s'\n", carg->lang);
	}

	string *request = string_init_add_auto(metrics);
	lang_run(carg->lang, NULL, request, carg->response);

	carg->parser_status = 1;
}

string* lang_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
	else
		return string_init_add_auto(hi->query);
}

void lang_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("lang");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = lang_parser_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = lang_mesg;
	strlcpy(actx->handler[0].key, "lang", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
