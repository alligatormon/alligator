#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "dstructures/metric.h"

void zookeeper_mntr_handler(char *metrics, size_t size, char *instance, int kind)
{
	char **maps = malloc(sizeof(char*)*1);
	maps[0] = strdup("zk_server_state");
	char *res = selector_split_metric(metrics, size, "\n", 1, "\t", 1, "", 0, maps, 1);
	if (!res)
		return;

	int64_t nval = 0;
	int64_t val = 1;
	if(strstr(res, "standalone"))
	{
		metric_labels_add_lbl("zk_mode", &nval, ALLIGATOR_DATATYPE_INT, 0, "mode", "leader");
		metric_labels_add_lbl("zk_mode", &nval, ALLIGATOR_DATATYPE_INT, 0, "mode", "follower");
		metric_labels_add_lbl("zk_mode", &val, ALLIGATOR_DATATYPE_INT, 0, "mode", "standalone");
	}
	else if(strstr(res, "follower"))
	{
		metric_labels_add_lbl("zk_mode", &nval, ALLIGATOR_DATATYPE_INT, 0, "mode", "leader");
		metric_labels_add_lbl("zk_mode", &val, ALLIGATOR_DATATYPE_INT, 0, "mode", "follower");
		metric_labels_add_lbl("zk_mode", &nval, ALLIGATOR_DATATYPE_INT, 0, "mode", "standalone");
	}
	else if(strstr(res, "leader"))
	{
		metric_labels_add_lbl("zk_mode", &val, ALLIGATOR_DATATYPE_INT, 0, "mode", "leader");
		metric_labels_add_lbl("zk_mode", &nval, ALLIGATOR_DATATYPE_INT, 0, "mode", "follower");
		metric_labels_add_lbl("zk_mode", &nval, ALLIGATOR_DATATYPE_INT, 0, "mode", "standalone");
	}
}
void zookeeper_wchs_handler(char *metrics, size_t size, char *instance, int kind)
{
	char *cur = strstr(metrics, "Total watches:");
	if (!cur)
		return;
	int64_t pvalue = atoi(cur+14);
	metric_labels_add_auto_prefix("total_watches", "zk_", &pvalue, ALLIGATOR_DATATYPE_INT, 0);
}
void zookeeper_isro_handler(char *metrics, size_t size, char *instance, int kind)
{
	int64_t val = 1;
	int64_t nval = 0;
	if (!strncmp(metrics, "ro", 2))
	{
		metric_labels_add_lbl("zk_readwrite", &val, ALLIGATOR_DATATYPE_INT, 0, "status", "ro");
		metric_labels_add_lbl("zk_readwrite", &nval, ALLIGATOR_DATATYPE_INT, 0, "status", "rw");
	}
	else if (!strncmp(metrics, "rw", 2))
	{
		metric_labels_add_lbl("zk_readwrite", &nval, ALLIGATOR_DATATYPE_INT, 0, "status", "ro");
		metric_labels_add_lbl("zk_readwrite", &val, ALLIGATOR_DATATYPE_INT, 0, "status", "rw");
	}
}
