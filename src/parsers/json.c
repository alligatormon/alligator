#include <stdio.h>
#include <inttypes.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/aggregator.h"
#include "common/json_parser.h"
#include "main.h"
void json_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "json", carg);
	//puts("==============");
	//puts(metrics);
	//puts("==============");
	//write_to_file("1", metrics, size, NULL, NULL);
	//FILE *fd = fopen("1", "w");
	//fwrite(metrics, strlen(metrics), 1, fd);
	//fclose(fd);
}

string* json_mesg(host_aggregator_info *hi, void *arg)
{
	return string_init_add(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1), 0, 0);
}

void json_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("jsonparse");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = json_handler;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = json_mesg;
	strlcpy(actx->handler[0].key, "jsonparse", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
