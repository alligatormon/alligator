#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/validator.h"
#include "metric/metric_types.h"
#include "main.h"
#define POWERDNS_METRIC_SIZE 1000

/* Instantaneous / size / latency fields — everything else is a cumulative counter.
 * Do not treat every *_bytes name as a gauge: Auth counters like udp-answers-bytes
 * / tcp-answers-bytes are cumulative and must become COUNTER + _total. */
static int powerdns_is_gauge(const char *name)
{
	size_t len;

	if (!name || !*name)
		return 1;
	if (!strcmp(name, "uptime") || !strcmp(name, "security_status") ||
	    !strcmp(name, "qsize_q") || !strcmp(name, "concurrent_queries") ||
	    !strcmp(name, "xfr_queue"))
		return 1;
	if (strstr(name, "latency"))
		return 1;

	/* Memory/size byte gauges only (not cumulative *answers-bytes counters). */
	if (!strcmp(name, "packetcache_bytes") || !strcmp(name, "query_cache_bytes") ||
	    !strcmp(name, "cache_bytes"))
		return 1;

	len = strlen(name);
	if (len >= 5 && !strcmp(name + len - 5, "_size"))
		return 1;
	if (len >= 6 && !strcmp(name + len - 6, "_usage"))
		return 1;
	if (len >= 8 && !strcmp(name + len - 8, "_entries"))
		return 1;
	if (len >= 8 && !strcmp(name + len - 8, "_clients"))
		return 1;
	if (strstr(name, "_connections"))
		return 1;
	return 0;
}

static void powerdns_ensure_total(char *name, size_t sz)
{
	size_t len = strlen(name);

	if (len >= 6 && !strcmp(name + len - 6, "_total"))
		return;
	if (len + 6 >= sz)
		return;
	memcpy(name + len, "_total", 7);
}

void powerdns_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root;
	json_error_t error;

	(void)size;
	root = json_loads(metrics, 0, &error);

	if (!root)
	{
		carglog(carg, L_ERROR, "powerdns json error on line %d: %s\n", error.line, error.text);
		return;
	}

	size_t i;
	size_t arr_sz = json_array_size(root);
	char metricname[POWERDNS_METRIC_SIZE];
	strlcpy(metricname, "powerdns_", 10);

	for (i = 0; i < arr_sz; i++)
	{
		json_t *arr_obj = json_array_get(root, i);
		if ( json_typeof(arr_obj) != JSON_OBJECT )
			continue;

		json_t *jsonname = json_object_get(arr_obj, "name");
		char *tname = (char*)json_string_value(jsonname);
		json_t *jsonvalue = json_object_get(arr_obj, "value");
		const char *tvalue_str = json_string_value(jsonvalue);
		if (!tname || !tvalue_str)
			continue;
		int64_t tvalue = atoll(tvalue_str);

		strlcpy(metricname + 9, tname, POWERDNS_METRIC_SIZE - 10);
		prometheus_metric_name_normalizer(metricname + 9, strlen(metricname + 9));

		if (powerdns_is_gauge(metricname + 9)) {
			namespace_metric_family_set(NULL, carg, metricname, METRIC_TYPE_GAUGE,
				"PowerDNS instantaneous statistic.");
		} else {
			powerdns_ensure_total(metricname, sizeof(metricname));
			namespace_metric_family_set(NULL, carg, metricname, METRIC_TYPE_COUNTER,
				"PowerDNS cumulative statistic.");
		}
		metric_add_auto(metricname, &tvalue, DATATYPE_INT, carg);
	}
	json_decref(root);
	carg->parser_status = 1;
}

string* powerdns_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, "/api/v1/servers/localhost/statistics", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

void powerdns_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("powerdns");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = powerdns_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = powerdns_mesg;
	strlcpy(actx->handler[0].key,"powerdns", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
