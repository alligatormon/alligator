#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/validator.h"
#include "metric/metric_types.h"
#include "main.h"
#define HTTPD_LABEL_SIZE 100
#define HTTPD_FIELD_SIZE 64

static void httpd_emit_mapped(context_arg *carg, const char *field, const char *sval, char *tmp)
{
	uint64_t uval;
	double dval;

	if (!strcmp(field, "Total Accesses")) {
		uval = strtoull(tmp, NULL, 10);
		namespace_metric_family_set(NULL, carg, "httpd_accesses_total", METRIC_TYPE_COUNTER,
			"Apache HTTPD total accesses from mod_status.");
		metric_add_auto("httpd_accesses_total", &uval, DATATYPE_UINT, carg);
		return;
	}
	if (!strcmp(field, "Total kBytes")) {
		uval = strtoull(tmp, NULL, 10) * 1024ULL;
		namespace_metric_family_set(NULL, carg, "httpd_sent_bytes_total", METRIC_TYPE_COUNTER,
			"Apache HTTPD total bytes sent from mod_status.");
		metric_add_auto("httpd_sent_bytes_total", &uval, DATATYPE_UINT, carg);
		return;
	}
	if (!strcmp(field, "Uptime")) {
		uval = strtoull(tmp, NULL, 10);
		namespace_metric_family_set(NULL, carg, "httpd_uptime_seconds", METRIC_TYPE_COUNTER,
			"Apache HTTPD uptime in seconds.");
		metric_add_auto("httpd_uptime_seconds", &uval, DATATYPE_UINT, carg);
		return;
	}
	if (!strcmp(field, "BusyWorkers")) {
		uval = strtoull(tmp, NULL, 10);
		namespace_metric_family_set(NULL, carg, "httpd_busy_workers", METRIC_TYPE_GAUGE,
			"Apache HTTPD busy workers.");
		metric_add_auto("httpd_busy_workers", &uval, DATATYPE_UINT, carg);
		return;
	}
	if (!strcmp(field, "IdleWorkers")) {
		uval = strtoull(tmp, NULL, 10);
		namespace_metric_family_set(NULL, carg, "httpd_idle_workers", METRIC_TYPE_GAUGE,
			"Apache HTTPD idle workers.");
		metric_add_auto("httpd_idle_workers", &uval, DATATYPE_UINT, carg);
		return;
	}
	if (!strcmp(field, "ReqPerSec")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_request_rate", METRIC_TYPE_GAUGE,
			"Apache HTTPD requests per second.");
		metric_add_auto("httpd_request_rate", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "BytesPerSec")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_bytes_per_second", METRIC_TYPE_GAUGE,
			"Apache HTTPD bytes per second.");
		metric_add_auto("httpd_bytes_per_second", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "BytesPerReq")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_bytes_per_request", METRIC_TYPE_GAUGE,
			"Apache HTTPD bytes per request.");
		metric_add_auto("httpd_bytes_per_request", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "Total Duration")) {
		dval = strtod(tmp, NULL) / 1000.0;
		namespace_metric_family_set(NULL, carg, "httpd_duration_seconds_total", METRIC_TYPE_COUNTER,
			"Apache HTTPD cumulative request duration in seconds.");
		metric_add_auto("httpd_duration_seconds_total", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "DurationPerReq")) {
		dval = strtod(tmp, NULL) / 1000.0;
		namespace_metric_family_set(NULL, carg, "httpd_duration_seconds", METRIC_TYPE_GAUGE,
			"Apache HTTPD mean request duration in seconds.");
		metric_add_auto("httpd_duration_seconds", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "CPULoad")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_cpu_load", METRIC_TYPE_GAUGE,
			"Apache HTTPD CPU load.");
		metric_add_auto("httpd_cpu_load", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "CPUUser")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_cpu_user", METRIC_TYPE_GAUGE,
			"Apache HTTPD CPU user time.");
		metric_add_auto("httpd_cpu_user", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "CPUSystem")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_cpu_system", METRIC_TYPE_GAUGE,
			"Apache HTTPD CPU system time.");
		metric_add_auto("httpd_cpu_system", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "CPUChildrenUser")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_cpu_children_user", METRIC_TYPE_GAUGE,
			"Apache HTTPD children CPU user time.");
		metric_add_auto("httpd_cpu_children_user", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "CPUChildrenSystem")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_cpu_children_system", METRIC_TYPE_GAUGE,
			"Apache HTTPD children CPU system time.");
		metric_add_auto("httpd_cpu_children_system", &dval, DATATYPE_DOUBLE, carg);
		return;
	}
	if (!strcmp(field, "Load1")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_load_average", METRIC_TYPE_GAUGE,
			"Apache HTTPD host load average.");
		metric_add_labels("httpd_load_average", &dval, DATATYPE_DOUBLE, carg, "period", "1");
		return;
	}
	if (!strcmp(field, "Load5")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_load_average", METRIC_TYPE_GAUGE,
			"Apache HTTPD host load average.");
		metric_add_labels("httpd_load_average", &dval, DATATYPE_DOUBLE, carg, "period", "5");
		return;
	}
	if (!strcmp(field, "Load15")) {
		dval = strtod(tmp, NULL);
		namespace_metric_family_set(NULL, carg, "httpd_load_average", METRIC_TYPE_GAUGE,
			"Apache HTTPD host load average.");
		metric_add_labels("httpd_load_average", &dval, DATATYPE_DOUBLE, carg, "period", "15");
		return;
	}

	/* Fallback: httpd_<snake_case_field> */
	{
		char metric_name[HTTPD_LABEL_SIZE];
		char norm[HTTPD_FIELD_SIZE];
		size_t flen = strlen(field);
		size_t copy = flen < (sizeof(norm) - 1) ? flen : (sizeof(norm) - 1);
		strlcpy(norm, field, copy + 1);
		prometheus_metric_name_normalizer(norm, copy);
		snprintf(metric_name, sizeof(metric_name), "httpd_%s", norm);

		if (strstr(sval, ".")) {
			dval = strtod(tmp, NULL);
			namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE,
				"Apache HTTPD mod_status field exported from ?auto.");
			metric_add_auto(metric_name, &dval, DATATYPE_DOUBLE, carg);
		} else {
			uval = strtoull(tmp, NULL, 10);
			namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE,
				"Apache HTTPD mod_status field exported from ?auto.");
			metric_add_auto(metric_name, &uval, DATATYPE_UINT, carg);
		}
	}
}

void httpd_status_handler(char *metrics, size_t size, context_arg *carg)
{
	char field[HTTPD_FIELD_SIZE];
	char sval[HTTPD_LABEL_SIZE];
	uint64_t sz;
	char *tmp = metrics;

	for (uint64_t i = 0; i < size;)
	{
		if (!strncmp(tmp, "Scoreboard", 10) || !strncmp(tmp, "ServerUptime", 12)) {}
		else
		{
			sz = strcspn(tmp, ":");
			size_t field_copy = sz < (sizeof(field) - 1) ? sz : (sizeof(field) - 1);
			strlcpy(field, tmp, field_copy + 1);

			carglog(carg, L_DEBUG, "httpd field '%s'\n", field);

			tmp += sz;
			tmp += strcspn(tmp, " \t");
			tmp += strspn(tmp, " \t");
			sz = strcspn(tmp, "\n");
			size_t sval_copy = sz < (sizeof(sval) - 1) ? sz : (sizeof(sval) - 1);
			strlcpy(sval, tmp, sval_copy + 1);

			httpd_emit_mapped(carg, field, sval, tmp);
		}

		tmp += strcspn(tmp, "\n");
		tmp += strspn(tmp, "\n");

		i = tmp - metrics;
	}
	carg->parser_status = 1;
}

string* httpd_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, "?auto", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

void httpd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("httpd");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = httpd_status_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = httpd_status_mesg;
	strlcpy(actx->handler[0].key, "httpd_status", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
