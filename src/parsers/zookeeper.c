#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"

static inline void zookeeper_metric_set(context_arg *carg, const char *metric_name)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "ZooKeeper exported metric value.");
}

void zookeeper_mntr_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "zk_mode", METRIC_TYPE_GAUGE, "ZooKeeper node mode state.");

	plain_parse_family(metrics, size, "\t", "\n", "", 0, carg, zookeeper_metric_set);

	char *state = strstr(metrics, "zk_server_state\t");
	if (!state)
		return;

	state += strlen("zk_server_state\t");
	int64_t val = 1;
	if(strstr(state, "standalone"))
		metric_add_labels("zk_mode", &val, DATATYPE_INT, carg, "mode", "standalone");
	else if(strstr(state, "follower"))
		metric_add_labels("zk_mode", &val, DATATYPE_INT, carg, "mode", "follower");
	else if(strstr(state, "leader"))
		metric_add_labels("zk_mode", &val, DATATYPE_INT, carg, "mode", "leader");
	carg->parser_status = 1;
}
void zookeeper_wchs_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "zk_total_watches", METRIC_TYPE_GAUGE, "ZooKeeper total watches.");

	char *cur = strstr(metrics, "Total watches:");
	if (!cur)
		return;
	int64_t pvalue = strtoll(cur + 14, NULL, 10);
	metric_add_auto("zk_total_watches", &pvalue, DATATYPE_INT, carg);
	carg->parser_status = 1;
}
void zookeeper_isro_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "zk_readwrite", METRIC_TYPE_GAUGE, "ZooKeeper read/write mode state.");

	int64_t val = 1;
	if (!strncmp(metrics, "ro", 2))
	{
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, carg, "status", "ro");
		carg->parser_status = 0;
	}
	else if (!strncmp(metrics, "rw", 2))
	{
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, carg, "status", "rw");
		carg->parser_status = 1;
	}
	else if (!strncmp(metrics, "null", 4))
	{
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, carg, "status", "null");
		carg->parser_status = 0;
	}
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
