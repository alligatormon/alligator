#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "parsers/multiparser.h"
#include "main.h"

#define SENTINEL_SIZE 1000

static inline void sentinel_metric_set(context_arg *carg, const char *metric_name)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "Redis Sentinel exported metric value.");
}

static inline void sentinel_plain_parse(char *text, uint64_t size, context_arg *carg)
{
	plain_parse_family(text, size, ":", "\r\n", "sentinel_", 9, carg, sentinel_metric_set);
}

void sentinel_handler(char *metrics, size_t size, context_arg *carg)
{
	sentinel_plain_parse(metrics, size, carg);

	char *master = strstr(metrics, "master0:");
	if (!master)
		return;

	uint64_t nval = 0;
	uint64_t val = 1;

	char name[SENTINEL_SIZE];
	char address[SENTINEL_SIZE];
	uint64_t sentinels;
	uint64_t slaves;

	char *tmp = master;
	char *tmp2;
	size_t end;

	tmp2 = strstr(tmp, "name=");
	if (!tmp2)
		return;
	tmp = tmp2;
	end = strcspn(tmp+5, ",");
	size_t name_copy = end < (sizeof(name) - 1) ? end : (sizeof(name) - 1);
	strlcpy(name, tmp+5, name_copy + 1);
	tmp += end;

	if(strstr(tmp, "status=ok"))
	{
		sentinel_metric_set(carg, "sentinel_status");
		metric_add_labels("sentinel_status", &val, DATATYPE_UINT, carg, "status", "ok");
		metric_add_labels("sentinel_status", &nval, DATATYPE_UINT, carg, "status", "fail");
	}
	else
	{
		sentinel_metric_set(carg, "sentinel_status");
		metric_add_labels("sentinel_status", &nval, DATATYPE_UINT, carg, "status", "ok");
		metric_add_labels("sentinel_status", &val, DATATYPE_UINT, carg, "status", "fail");
	}

	tmp2 = strstr(tmp, "address=");
	if (!tmp2)
		return;
	tmp = tmp2;
	end = strcspn(tmp+8, ",");
	size_t address_copy = end < (sizeof(address) - 1) ? end : (sizeof(address) - 1);
	strlcpy(address, tmp+8, address_copy + 1);
	tmp += end;

	tmp2 = strstr(tmp, "slaves=");
	if (!tmp2)
		return;
	tmp = tmp2;
	end = strcspn(tmp+7, ",");
	slaves = atoll(tmp+7);
	tmp += end;

	tmp2 = strstr(tmp, "sentinels=");
	if (!tmp2)
		return;
	tmp = tmp2;
	sentinels = atoll(tmp+10);

	sentinel_metric_set(carg, "sentinel_slaves");
	metric_add_labels2("sentinel_slaves", &slaves, DATATYPE_UINT, carg, "name", name, "address", address);
	sentinel_metric_set(carg, "sentinel_sentinels");
	metric_add_labels2("sentinel_sentinels", &sentinels, DATATYPE_UINT, carg, "name", name, "address", address);

	carg->parser_status = 1;
}

string* sentinel_parser_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	char* query = malloc(1000);
	if (hi->pass)
		snprintf(query, 1000, "AUTH %s\r\nINFO\r\n", hi->pass);
	else
		snprintf(query, 1000, "INFO\n");

	return string_init_add_auto(query);
}

void sentinel_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("sentinel");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);
	actx->handler[0].name = sentinel_handler;
	actx->handler[0].validator = redis_cluster_validator;
	actx->handler[0].mesg_func = sentinel_parser_mesg;

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
