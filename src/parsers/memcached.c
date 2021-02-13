#include <stdio.h>
#include <string.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#define MC_NAME_SIZE 255

void memcached_handler(char *metrics, size_t size, context_arg *carg)
{
	char *cur = metrics;
	char name[MC_NAME_SIZE+10];
	strcpy(name, "memcached_");
	uint64_t name_size;
	uint64_t msize;

	while (cur-metrics < size)
	{
		cur = strstr(cur, "STAT ");
		if (!cur)
			return;

		cur += 5;
		msize = strcspn(cur, " \t\r\n");
		name_size = MC_NAME_SIZE > msize+1 ? msize+1 : MC_NAME_SIZE;
		strlcpy(name+10, cur, name_size);
		if (!strncmp(name, "memcached_pid", 13))
			continue;

		cur += msize;
		msize = strspn(cur, " \t\r\n");
		cur += msize;
		msize = strcspn(cur, " \t\r\n");

		int rc = metric_value_validator(cur, msize);
		if (rc == DATATYPE_INT)
		{
			int64_t mval = strtoll(cur, &cur, 10);
			metric_add_auto(name, &mval, rc, carg);
		}
		else if (rc == DATATYPE_UINT)
		{
			uint64_t mval = strtoll(cur, &cur, 10);
			metric_add_auto(name, &mval, rc, carg);
		}
		else if (rc == DATATYPE_DOUBLE)
		{
			double mval = strtoll(cur, &cur, 10);
			metric_add_auto(name, &mval, rc, carg);
		}
	}
}

string* memcached_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(strdup("stats\n"), 0, 0);
}

void memcached_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("memcached");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = memcached_handler;
	//actx->handler[0].validator = memcached_validator;
	actx->handler[0].mesg_func = memcached_mesg;
	strlcpy(actx->handler[0].key,"memcached", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
