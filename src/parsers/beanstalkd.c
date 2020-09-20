#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"

void beanstalkd_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp = strstr(metrics, "\n");
	if (!tmp)
		return;

	++tmp;
	tmp += strcspn(tmp, "\n");
	tmp += strspn(tmp, "\n");
	
	plain_parse(tmp, size, ": ", "\n", "beanstalkd_", 11, carg);
}

int8_t beanstalkd_validator(char *data, size_t size)
{
	if (strncmp(data, "OK", 2))
	{
		return 0;
	}

	uint64_t nsize = strtoull(data+3, NULL, 10);

	if (size >= nsize)
		return 1;
	else
		return 0;
}

string* beanstalkd_mesg(host_aggregator_info *hi, void *arg)
{
	return string_init_add(strdup("stats\r\n"), 0, 0);
}

void beanstalkd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("beanstalkd");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = beanstalkd_handler;
	actx->handler[0].validator = beanstalkd_validator;
	actx->handler[0].mesg_func = beanstalkd_mesg;
	strlcpy(actx->handler[0].key, "beanstalkd", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
