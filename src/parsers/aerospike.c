#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"

void aerospike_statistics_handler(char *metrics, size_t size, context_arg *carg)
{
	char *clmetrics = selector_get_field_by_str(metrics, size, "statistics", 2, NULL);
	if ( clmetrics )
	{
		selector_split_metric(clmetrics, strlen(clmetrics), ";", 1, "=", 1, "aerospike_", 10, NULL, 0, carg);
		free(clmetrics);
	}
}

void aerospike_get_namespaces_handler(char *metrics, size_t size, context_arg *carg)
{
}

void aerospike_namespace_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp = strstr(metrics+8, "namespace/");
	if (!tmp)
		return;

	tmp += 10;
	char namespace[1000];
	char key[1000];
	uint64_t vl;
	uint64_t num;
	char *typekey;
	num = strcspn(tmp, "\t");
	strlcpy(namespace, tmp, num+1);
	tmp += num;
	tmp += strspn(tmp, "\t");
	strlcpy(key, "aerospike_", 11);
	while (size > tmp-metrics)
	{
		num = strcspn(tmp, "=");
		strlcpy(key+10, tmp, num+1);
		if (strstr(key, "{"))
		{
			num = strcspn(tmp, ";");
			tmp += num+1;
			continue;
		}
		metric_name_normalizer(key+10, num);
		tmp += num+1;

		num = strcspn(tmp, ";");
		vl = atoll(tmp);
		tmp += num+1;

		if (!strncmp(key+10, "client", 6))
		{
			typekey = key+17;
			metric_add_labels2("aerospike_client", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "udf_sub", 7))
		{
			typekey = key+18;
			metric_add_labels2("aerospike_udf_sub", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "query", 5))
		{
			typekey = key+16;
			metric_add_labels2("aerospike_query", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "migrate", 7))
		{
			typekey = key+18;
			metric_add_labels2("aerospike_migrate", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "batch", 5))
		{
			typekey = key+16;
			metric_add_labels2("aerospike_batch", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "retransmit", 10))
		{
			typekey = key+21;
			metric_add_labels2("aerospike_retransmit", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "scan", 4))
		{
			typekey = key+15;
			metric_add_labels2("aerospike_scan", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "memory", 6))
		{
			typekey = key+17;
			metric_add_labels2("aerospike_memory", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "enable_benchmarks", 17))
		{
			typekey = key+28;
			metric_add_labels2("aerospike_enable_benchmarks", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "fail", 4))
		{
			typekey = key+15;
			metric_add_labels2("aerospike_fail", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "geo2dsphere_within", 8))
		{
			typekey = key+29;
			metric_add_labels2("aerospike_geo2dsphere", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "geo_region_query", 16))
		{
			typekey = key+27;
			metric_add_labels2("aerospike_geo_region_query", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else
			metric_add_labels(key, &vl, DATATYPE_UINT, carg, "namespace", namespace);
	}
}

void aerospike_status_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp = metrics+8;
	tmp += strcspn(tmp, "\t");
	tmp += strspn(tmp, "\t");
	tmp[size - (tmp-metrics) - 1] = 0;
	uint64_t vl = 1;
	metric_add_labels("aerospike_status", &vl, DATATYPE_UINT, carg, "status", tmp);
}
