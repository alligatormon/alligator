#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"

void zookeeper_mntr_handler(char *metrics, size_t size, client_info *cinfo)
{
	char **maps = malloc(sizeof(char*)*1);
	maps[0] = strdup("zk_server_state");
	char *res = selector_split_metric(metrics, size, "\n", 1, "\t", 1, "", 0, maps, 1);
	free(maps[0]);
	free(maps);
	if (!res)
		return;

	int64_t nval = 0;
	int64_t val = 1;
	if(strstr(res, "standalone"))
	{
		metric_add_labels("zk_mode", &nval, DATATYPE_INT, 0, "mode", "leader");
		metric_add_labels("zk_mode", &nval, DATATYPE_INT, 0, "mode", "follower");
		metric_add_labels("zk_mode", &val, DATATYPE_INT, 0, "mode", "standalone");
	}
	else if(strstr(res, "follower"))
	{
		metric_add_labels("zk_mode", &nval, DATATYPE_INT, 0, "mode", "leader");
		metric_add_labels("zk_mode", &val, DATATYPE_INT, 0, "mode", "follower");
		metric_add_labels("zk_mode", &nval, DATATYPE_INT, 0, "mode", "standalone");
	}
	else if(strstr(res, "leader"))
	{
		metric_add_labels("zk_mode", &val, DATATYPE_INT, 0, "mode", "leader");
		metric_add_labels("zk_mode", &nval, DATATYPE_INT, 0, "mode", "follower");
		metric_add_labels("zk_mode", &nval, DATATYPE_INT, 0, "mode", "standalone");
	}
	free(res);
}
void zookeeper_wchs_handler(char *metrics, size_t size, client_info *cinfo)
{
	char *cur = strstr(metrics, "Total watches:");
	if (!cur)
		return;
	int64_t pvalue = atoi(cur+14);
	metric_add_auto("zk_total_watches", &pvalue, DATATYPE_INT, 0);
}
void zookeeper_isro_handler(char *metrics, size_t size, client_info *cinfo)
{
	int64_t val = 1;
	int64_t nval = 0;
	if (!strncmp(metrics, "ro", 2))
	{
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, 0, "status", "ro");
		metric_add_labels("zk_readwrite", &nval, DATATYPE_INT, 0, "status", "rw");
	}
	else if (!strncmp(metrics, "rw", 2))
	{
		metric_add_labels("zk_readwrite", &nval, DATATYPE_INT, 0, "status", "ro");
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, 0, "status", "rw");
	}
}
