#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#define NSD_NAME_SIZE 1024

void nsd_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t i = 0;
	char tmp[NSD_NAME_SIZE];
	char tmp2[NSD_NAME_SIZE];
	char tmp3[NSD_NAME_SIZE];
	uint64_t copysize;
	char *argindex;
	uint64_t val = 0;

	for (; i < size; i++)
	{
		copysize = strcspn(metrics+i, " =");
		strlcpy(tmp, metrics+i, (copysize > NSD_NAME_SIZE ? NSD_NAME_SIZE : copysize)+1);
		i += copysize;
		i += strspn(metrics+i, "= ");
		//printf("==> '%s'\n", tmp);

		if (!strncmp(tmp, "num.type", 8))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_labels("nsd_queries", &val, DATATYPE_UINT, carg, "type", tmp+9);
		}
		else
		{
			val = strtoull(metrics+i, NULL, 10);

			char *lastdot = argindex = tmp;
			char *pre_lastdot = argindex = tmp;
			uint64_t dots = 0;
			while (*argindex)
			{
				if (*argindex == '.')
				{
					pre_lastdot = lastdot;
					lastdot = argindex + 1;
					++dots;
				}
				++argindex;
			}
			strlcpy(tmp2, "nsd_", 5);
			strlcpy(tmp2+4, tmp, lastdot-tmp);
			metric_name_normalizer(tmp2, lastdot-tmp+1);
			strlcpy(tmp3, pre_lastdot, lastdot-pre_lastdot);
			//printf("finded dot: '%s' to '%s' {'%s'='%s'}\n", tmp, tmp2, tmp3, lastdot);
			metric_add_labels(tmp2, &val, DATATYPE_UINT, carg, tmp3, lastdot);
		}

		i += strcspn(metrics+i, "\n");
	}
	carg->parser_status = 1;
}

int8_t nsd_validator(char *data, size_t size)
{
	if (size < 500)
		return 0;
	char *ret = strstr(data, "zone.slave");
	if (ret)
		return 1;
	else
		return 0;
}

string* nsd_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("NSDCT1 stats_noreset\n", 0);
}

void nsd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("nsd");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = nsd_handler;
	actx->handler[0].validator = nsd_validator;
	actx->handler[0].mesg_func = nsd_mesg;
	strlcpy(actx->handler[0].key,"nsd", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
