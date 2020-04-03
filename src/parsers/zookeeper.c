#include <stdio.h>
#include <string.h>
#include "common/selector_split_metric.h"
#include "metric/namespace.h"
#include "events/context_arg.h"

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
}
void zookeeper_wchs_handler(char *metrics, size_t size, context_arg *carg)
{
	char *cur = strstr(metrics, "Total watches:");
	if (!cur)
		return;
	int64_t pvalue = atoi(cur+14);
	metric_add_auto("zk_total_watches", &pvalue, DATATYPE_INT, carg);
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
}
