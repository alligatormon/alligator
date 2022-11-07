#include <stdio.h>
#include <string.h>
#include "common/selector_split_metric.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"

void zookeeper_mntr_handler(char *metrics, size_t size, context_arg *carg)
{
	char **maps = malloc(sizeof(char*)*1);
	maps[0] = strdup("zk_server_state");
	char *res = selector_split_metric(metrics, size, "\n", 1, "\t", 1, "", 0, maps, 1, carg);
	free(maps[0]);
	free(maps);
	if (!res)
		return;

	int64_t val = 1;
	if(strstr(res, "standalone"))
		metric_add_labels("zk_mode", &val, DATATYPE_INT, carg, "mode", "standalone");
	else if(strstr(res, "follower"))
		metric_add_labels("zk_mode", &val, DATATYPE_INT, carg, "mode", "follower");
	else if(strstr(res, "leader"))
		metric_add_labels("zk_mode", &val, DATATYPE_INT, carg, "mode", "leader");
	free(res);
	carg->parser_status = 1;
}
void zookeeper_wchs_handler(char *metrics, size_t size, context_arg *carg)
{
	char *cur = strstr(metrics, "Total watches:");
	if (!cur)
		return;
	int64_t pvalue = atoi(cur+14);
	metric_add_auto("zk_total_watches", &pvalue, DATATYPE_INT, carg);
	carg->parser_status = 1;
}
void zookeeper_isro_handler(char *metrics, size_t size, context_arg *carg)
{
	int64_t val = 1;
	if (!strncmp(metrics, "ro", 2))
	{
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, carg, "status", "ro");
	}
	else if (!strncmp(metrics, "rw", 2))
	{
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, carg, "status", "rw");
	}
	carg->parser_status = 1;
}

string* zookeeper_mntr_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("mntr", 4);
}

string* zookeeper_isro_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("isro", 4);
}

string* zookeeper_wchs_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("wchs", 4);
}

void zookeeper_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("zookeeper");
	actx->handlers = 3;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = zookeeper_mntr_handler;
	//actx->handler[0].validator = zookeeper_validator;
	actx->handler[0].mesg_func = zookeeper_mntr_mesg;
	strlcpy(actx->handler[0].key,"zookeeper_mntr", 255);

	actx->handler[1].name = zookeeper_isro_handler;
	//actx->handler[1].validator = zookeeper_validator;
	actx->handler[1].mesg_func = zookeeper_isro_mesg;
	strlcpy(actx->handler[1].key,"zookeeper_isro", 255);

	actx->handler[2].name = zookeeper_wchs_handler;
	//actx->handler[2].validator = zookeeper_validator;
	actx->handler[2].mesg_func = zookeeper_wchs_mesg;
	strlcpy(actx->handler[2].key,"zookeeper_wchs", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
