#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"

typedef struct aerospike_data
{
	host_aggregator_info *hi;
	aggregate_context *actx;
} aerospike_data;

void aerospike_statistics_handler(char *metrics, size_t size, context_arg *carg)
{
	char *clmetrics = selector_get_field_by_str(metrics, size, "statistics", 2, NULL);
	if ( clmetrics )
	{
		plain_parse(clmetrics, strlen(clmetrics), "=", ";", "aerospike_", 10, carg);
		free(clmetrics);
	}
}

void aerospike_namespace_list_handler(char *metrics, size_t size, context_arg *carg)
{
	aerospike_data *ad = carg->data;
	if (!ad)
		return;

	aggregate_context *actx = ad->actx;
	if (!actx)
		return;

	size_t elem_size;
	char namespace_name[255];
	char *tmp = strstr(metrics+8, "namespaces\t");
	if (tmp)
	{
		tmp += 11;
		size -= 19;
	}
	else
		return;
	for (uint64_t i = 0; i < size; i++)
	{
		elem_size = strcspn(tmp, ";");
		if (tmp[elem_size] == ';')
			++elem_size;
		strlcpy(namespace_name, tmp, elem_size);

		char *aeronamespace = malloc(255);
		aeronamespace[0] = 2;
		aeronamespace[1] = 1;
		aeronamespace[2] = 0;
		aeronamespace[3] = 0;
		aeronamespace[4] = 0;
		aeronamespace[5] = 0;
		aeronamespace[6] = 0;
		aeronamespace[7] = '\13' + elem_size-1;
		snprintf(aeronamespace+8, 255-8, "namespace/%s\n", namespace_name);
		uint64_t writelen = 20 + elem_size-1;

		try_again(carg, aeronamespace, writelen, aerospike_namespace_handler, "aerospike_namespace");

		elem_size += strspn(tmp, ";");
		tmp += elem_size;
		i += elem_size;
	}
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

int8_t aerospike_validator(char *data, size_t size)
{
	return 1;
}


string* aerospike_statistics_mesg(host_aggregator_info *hi, void *arg)
{
	return string_init_alloc("\2\1\0\0\0\0\0\0", 8);
}

string* aerospike_status_mesg(host_aggregator_info *hi, void *arg)
{
	return string_init_alloc("\2\1\0\0\0\0\0\7status\n", 16);
}

string* aerospike_namespace_list_mesg(host_aggregator_info *hi, void *arg)
{
	aggregate_context *actx = arg;
	aerospike_data *ad = actx->data;
	ad->hi = hi;

	char *aeronamespace = malloc(255);
	aeronamespace[0] = 2;
	aeronamespace[1] = 1;
	aeronamespace[2] = 0;
	aeronamespace[3] = 0;
	aeronamespace[4] = 0;
	aeronamespace[5] = 0;
	aeronamespace[6] = 0;
	aeronamespace[7] = '\14';
	strlcpy(aeronamespace+8, "namespaces\n", 20);

	return string_init_add(aeronamespace, 20, 20);
}

void aerospike_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	aerospike_data *ad = calloc(1, sizeof(*ad));
	ad->actx = actx;

	actx->key = strdup("aerospike");
	actx->data = ad;
	actx->handlers = 3;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = aerospike_namespace_list_handler;
	actx->handler[0].validator = aerospike_validator;
	actx->handler[0].mesg_func = aerospike_namespace_list_mesg;
	strlcpy(actx->handler[0].key,"aerospike_namespace_list", 255);

	actx->handler[1].name = aerospike_statistics_handler;
	actx->handler[1].validator = aerospike_validator;
	actx->handler[1].mesg_func = aerospike_statistics_mesg;
	strlcpy(actx->handler[1].key,"aerospike_statistics", 255);

	actx->handler[2].name = aerospike_status_handler;
	actx->handler[2].validator = aerospike_validator;
	actx->handler[2].mesg_func = aerospike_status_mesg;
	strlcpy(actx->handler[2].key,"aerospike_status", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
