#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "common/validator.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"

int8_t aerospike_validator(context_arg *carg, char *data, size_t size)
{
	//char *ret = strstr(data, "Keyspace");
	//if (ret)
	//	return 1;
	//else
	//	return 0;
	//printf("aerospike_validator %zu\n=======\n%s\n======\n", size, data);
	return 1;
}

void aerospike_namespace_handler(char *metrics, size_t size, context_arg *carg);

typedef struct aerospike_data
{
	host_aggregator_info *hi;
	aggregate_context *actx;
} aerospike_data;

static char *aerospike_memmem(char *haystack, size_t haystack_len, const char *needle, size_t needle_len)
{
	char *p;

	if (!haystack || !needle || !needle_len || haystack_len < needle_len)
		return NULL;

	for (p = haystack; p <= haystack + haystack_len - needle_len; p++)
	{
		if (!memcmp(p, needle, needle_len))
			return p;
	}

	return NULL;
}

static inline void aerospike_metric_set(context_arg *carg, const char *metric_name)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "Aerospike exported metric value.");
}

static inline void aerospike_plain_parse(char *text, uint64_t size, char *sep, char *nlsep, char *prefix, uint64_t psize, context_arg *carg)
{
	plain_parse_family(text, size, sep, nlsep, prefix, psize, carg, aerospike_metric_set);
}

static inline char *aerospike_statistics_payload(char *metrics, size_t size, size_t *payload_size)
{
	if (size <= 8 || !payload_size)
		return NULL;

	char *body = metrics + 8;
	char *end = metrics + size;
	char *line = body;

	*payload_size = 0;
	while (line < end)
	{
		char *nl = memchr(line, '\n', end - line);
		char *cr = memchr(line, '\r', end - line);
		char *line_end = end;
		if (nl && nl < line_end)
			line_end = nl;
		if (cr && cr < line_end)
			line_end = cr;

		char *stats = aerospike_memmem(line, line_end - line, "statistics", 10);
		if (stats)
		{
			stats += 10;
			while (stats < line_end && (*stats == '\t' || *stats == ' '))
				stats++;
			if (stats >= line_end)
				break;

			*payload_size = line_end - stats;
			if (*payload_size == 0)
				break;

			char *payload = malloc(*payload_size + 1);
			if (!payload)
				return NULL;
			memcpy(payload, stats, *payload_size);
			payload[*payload_size] = 0;
			return payload;
		}

		if (line_end >= end)
			break;
		line = line_end;
		if (line < end && (*line == '\n' || *line == '\r'))
			line++;
	}
	return NULL;
}

void aerospike_statistics_handler(char *metrics, size_t size, context_arg *carg)
{
	size_t payload_size = 0;
	char *payload = aerospike_statistics_payload(metrics, size, &payload_size);
	if (payload)
	{
		aerospike_plain_parse(payload, payload_size, "=", ";", "aerospike_", 10, carg);
		free(payload);
	}
	carg->parser_status = 1;
}

void aerospike_namespace_list_handler(char *metrics, size_t size, context_arg *carg)
{
	aerospike_data *ad = carg->data;
	if (!ad)
		return;

	aggregate_context *actx = ad->actx;
	if (!actx)
		return;

	if (!metrics || size <= 8)
		return;

	char *end = metrics + size;
	size_t elem_size;
	size_t list_size = size > 19 ? size - 19 : 0;
	char namespace_name[32];
	char *tmp = aerospike_memmem(metrics+8, end - (metrics+8), "namespaces\t", 11);
	if (!tmp)
		return;
	tmp += 11;

	for (uint64_t i = 0; i < list_size; i++)
	{
		char *semi = memchr(tmp, ';', end - tmp);
		if (!semi)
			break;

		elem_size = semi - tmp + 1;
		size_t name_len = elem_size - 1;
		if (name_len >= sizeof(namespace_name))
			name_len = sizeof(namespace_name) - 1;
		memcpy(namespace_name, tmp, name_len);
		namespace_name[name_len] = 0;

		char *aeronamespace = malloc(64);
		aeronamespace[0] = 2;
		aeronamespace[1] = 1;
		aeronamespace[2] = 0;
		aeronamespace[3] = 0;
		aeronamespace[4] = 0;
		aeronamespace[5] = 0;
		aeronamespace[6] = 0;
		aeronamespace[7] = '\13' + elem_size-1;
		snprintf(aeronamespace+8, 64-8, "namespace/%s\n", namespace_name);
		uint64_t writelen = 20 + elem_size-1;

		char *key = malloc(255);
		snprintf(key, 255, "(tcp://%s:%s)/%s", carg->host, carg->port, aeronamespace+8);
		key[strlen(key) - 1] = 0;
		try_again(carg, aeronamespace, writelen, aerospike_namespace_handler, "aerospike_namespace", aerospike_validator, key, carg->data);

		tmp = semi + 1;
		i += elem_size;
	}
	carg->parser_status = 1;
}

void aerospike_namespace_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp;

	if (!metrics || size <= 8)
		return;

	char *end = metrics + size;
	tmp = aerospike_memmem(metrics+8, end - (metrics+8), "namespace/", 10);
	if (!tmp)
		return;

	tmp += 10;
	char namespace[1000];
	char key[1000];
	uint64_t vl;
	size_t num;
	char *typekey;

	char *tab = memchr(tmp, '\t', end - tmp);
	if (!tab)
		return;
	num = tab - tmp;
	if (num >= sizeof(namespace))
		num = sizeof(namespace) - 1;
	memcpy(namespace, tmp, num);
	namespace[num] = 0;
	tmp = tab + 1;
	while (tmp < end && *tmp == '\t')
		tmp++;
	strlcpy(key, "aerospike_", 11);
	while (tmp < end)
	{
		char *eq = memchr(tmp, '=', end - tmp);
		if (!eq)
			break;

		size_t keylen = eq - tmp;
		size_t copy_len = keylen;
		if (copy_len > sizeof(key) - 11)
			copy_len = sizeof(key) - 11;
		memcpy(key + 10, tmp, copy_len);
		key[10 + copy_len] = 0;
		if (keylen != copy_len)
			keylen = copy_len;

		if (strstr(key, "{"))
		{
			char *brace_semi = memchr(eq, ';', end - eq);
			if (!brace_semi)
				break;
			tmp = brace_semi + 1;
			continue;
		}
		metric_name_normalizer(key+10, keylen);

		char *semi = memchr(eq + 1, ';', end - (eq + 1));
		if (!semi)
			break;

		size_t vlen = semi - (eq + 1);
		char value[64];
		size_t vcopy = vlen < sizeof(value) - 1 ? vlen : sizeof(value) - 1;
		memcpy(value, eq + 1, vcopy);
		value[vcopy] = 0;
		vl = atoll(value);
		tmp = semi + 1;

		if (!strncmp(key+10, "client", 6))
		{
			typekey = key+17;
			aerospike_metric_set(carg, "aerospike_client");
			metric_add_labels2("aerospike_client", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "udf_sub", 7))
		{
			typekey = key+18;
			aerospike_metric_set(carg, "aerospike_udf_sub");
			metric_add_labels2("aerospike_udf_sub", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "query", 5))
		{
			typekey = key+16;
			aerospike_metric_set(carg, "aerospike_query");
			metric_add_labels2("aerospike_query", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "migrate", 7))
		{
			typekey = key+18;
			aerospike_metric_set(carg, "aerospike_migrate");
			metric_add_labels2("aerospike_migrate", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "batch", 5))
		{
			typekey = key+16;
			aerospike_metric_set(carg, "aerospike_batch");
			metric_add_labels2("aerospike_batch", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "retransmit", 10))
		{
			typekey = key+21;
			aerospike_metric_set(carg, "aerospike_retransmit");
			metric_add_labels2("aerospike_retransmit", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "scan", 4))
		{
			typekey = key+15;
			aerospike_metric_set(carg, "aerospike_scan");
			metric_add_labels2("aerospike_scan", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "memory", 6))
		{
			typekey = key+17;
			aerospike_metric_set(carg, "aerospike_memory");
			metric_add_labels2("aerospike_memory", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "enable_benchmarks", 17))
		{
			typekey = key+28;
			aerospike_metric_set(carg, "aerospike_enable_benchmarks");
			metric_add_labels2("aerospike_enable_benchmarks", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "fail", 4))
		{
			typekey = key+15;
			aerospike_metric_set(carg, "aerospike_fail");
			metric_add_labels2("aerospike_fail", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "geo2dsphere_within", 8))
		{
			typekey = key+29;
			aerospike_metric_set(carg, "aerospike_geo2dsphere");
			metric_add_labels2("aerospike_geo2dsphere", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else if (!strncmp(key+10, "geo_region_query", 16))
		{
			typekey = key+27;
			aerospike_metric_set(carg, "aerospike_geo_region_query");
			metric_add_labels2("aerospike_geo_region_query", &vl, DATATYPE_UINT, carg, "namespace", namespace, "type", typekey);
		}
		else
		{
			aerospike_metric_set(carg, key);
			metric_add_labels(key, &vl, DATATYPE_UINT, carg, "namespace", namespace);
		}
	}
	carg->parser_status = 1;
}

void aerospike_status_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp;
	uint64_t vl = 1;

	if (!metrics || size <= 8)
		return;

	char *end = metrics + size;
	tmp = metrics + 8;
	char *tab = memchr(tmp, '\t', end - tmp);
	if (!tab)
		return;
	tmp = tab + 1;
	while (tmp < end && *tmp == '\t')
		tmp++;
	if (tmp >= end)
		return;
	metrics[size - 1] = 0;
	aerospike_metric_set(carg, "aerospike_status");
	metric_add_labels("aerospike_status", &vl, DATATYPE_UINT, carg, "status", tmp);
	carg->parser_status = 1;
}

string* aerospike_statistics_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("\2\1\0\0\0\0\0\0", 8);
}

string* aerospike_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("\2\1\0\0\0\0\0\7status\n", 16);
}

string* aerospike_namespace_list_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
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
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

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

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
