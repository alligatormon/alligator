#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#define UNBOUND_NAME_SIZE 1024

void unbound_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t i = 0;
	char tmp[UNBOUND_NAME_SIZE];
	char tmp2[UNBOUND_NAME_SIZE];
	char tmp3[UNBOUND_NAME_SIZE];
	char tmp4[UNBOUND_NAME_SIZE];
	uint64_t copysize;
	uint64_t copysize2;
	char *argindex;
	uint64_t val = 0;
	double dval;

	for (; i < size; i++)
	{
		copysize = strcspn(metrics+i, " =");
		strlcpy(tmp, metrics+i, (copysize > UNBOUND_NAME_SIZE ? UNBOUND_NAME_SIZE : copysize)+1);
		i += copysize;
		i += strspn(metrics+i, "= ");
		//printf("==> '%s'\n", tmp);

		if (!strncmp(tmp, "thread", 6))
		{
			copysize = strcspn(tmp, ".")+1;
			strlcpy(tmp2, tmp, copysize);

			copysize2 = strlen(tmp+copysize)+1;
			strlcpy(tmp3, tmp+copysize, copysize2);

			if (!strncmp(tmp+copysize, "recursion.time.avg", 18))
			{
				dval = strtof(metrics+i, NULL);
				metric_add_labels("unbound_recursion_time_avg", &dval, DATATYPE_DOUBLE, carg, "thread", tmp2);
			}
			else
			{
				val = strtoull(metrics+i, NULL, 10);
				strlcpy(tmp4, "unbound_thread_", UNBOUND_NAME_SIZE);
				strcat(tmp4, tmp3);
				metric_name_normalizer(tmp4, strlen(tmp4));
				metric_add_labels(tmp4, &val, DATATYPE_UINT, carg, "thread", tmp2);
			}
		}
		else if (!strncmp(tmp, "total.recursion.time.avg", 24))
		{
			dval = strtof(metrics+i, NULL);

			metric_add_auto("unbound_total_recursion_time_avg", &dval, DATATYPE_DOUBLE, carg);
		}
		else if (!strncmp(tmp, "histogram", 9))
		{
			val = strtoull(metrics+i, NULL, 10);

			argindex = strstr(tmp+10, ".to.");
			argindex += 4;

			double dtmp = strtof(argindex, NULL);
			uint64_t utmp = dtmp * 1000000;
			snprintf(tmp2, UNBOUND_NAME_SIZE, "%"u64, utmp);

			metric_add_labels("unbound_duration_microseconds", &val, DATATYPE_UINT, carg, "bucket", tmp2);
		}
		else if (!strncmp(tmp, "time.up", 7))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_auto("unbound_uptime", &val, DATATYPE_UINT, carg);
		}
		else if (!strncmp(tmp, "total.tcpusage", 14))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_auto("unbound_total_tcpusage", &val, DATATYPE_UINT, carg);
		}
		else if (!strncmp(tmp, "total.recursion.time.median", 27))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_auto("unbound_total_recursion_time_median", &val, DATATYPE_UINT, carg);
		}
		else if (!strncmp(tmp, "num.rrset.bogus", 15))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_auto("unbound_num_rrset_bogus", &val, DATATYPE_UINT, carg);
		}
		else if (!strncmp(tmp, "unwanted.queries", 16))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_auto("unbound_unwanted_queries", &val, DATATYPE_UINT, carg);
		}
		else if (!strncmp(tmp, "unwanted.replies", 16))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_auto("unbound_unwanted_replies", &val, DATATYPE_UINT, carg);
		}
		else if (!strncmp(tmp, "msg.cache.count", 15))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_labels("unbound_cache_count", &val, DATATYPE_UINT, carg, "cache", "msg");
		}
		else if (!strncmp(tmp, "rrset.cache.count", 17))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_labels("unbound_cache_count", &val, DATATYPE_UINT, carg, "cache", "rrset");
		}
		else if (!strncmp(tmp, "infra.cache.count", 17))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_labels("unbound_cache_count", &val, DATATYPE_UINT, carg, "cache", "infra");
		}
		else if (!strncmp(tmp, "key.cache.count", 15))
		{
			val = strtoull(metrics+i, NULL, 10);
			metric_add_labels("unbound_cache_count", &val, DATATYPE_UINT, carg, "cache", "key");
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
			strlcpy(tmp2, "unbound_", 9);
			strlcpy(tmp2+8, tmp, lastdot-tmp);
			metric_name_normalizer(tmp2, lastdot-tmp+7);
			strlcpy(tmp3, pre_lastdot, lastdot-pre_lastdot);
			//printf("finded dot: '%s' to '%s' {'%s'='%s'}\n", tmp, tmp2, tmp3, lastdot);
			metric_add_labels(tmp2, &val, DATATYPE_UINT, carg, tmp3, lastdot);
		}

		i += strcspn(metrics+i, "\n");
	}

	carg->parser_status = 1;
}

int8_t unbound_validator(char *data, size_t size)
{
	if (size < 4500)
		return 0;
	char *ret = strstr(data+4500, "key.cache.count");
	if (ret)
		return 1;
	else
		return 0;
}

string* unbound_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("UBCT1 stats_noreset\n", 0);
}

void unbound_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("unbound");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = unbound_handler;
	actx->handler[0].validator = unbound_validator;
	actx->handler[0].mesg_func = unbound_mesg;
	strlcpy(actx->handler[0].key,"unbound", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
