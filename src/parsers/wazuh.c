#if defined(__linux__)
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <time.h>
#include <jansson.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/validator.h"
#include "common/http.h"
#include "common/logs.h"
#include "events/fs_read.h"
#include "common/json_query.h"
#include "main.h"
#define WAZUH_SIZE 1000

static inline void wazuh_metric_set(context_arg *carg, const char *metric_name)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "Wazuh API exported metric value.");
}

static void wazuh_logcollector_metric_families_set(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_global_start", METRIC_TYPE_GAUGE,
		"Wazuh logcollector global statistics period start time.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_global_end", METRIC_TYPE_GAUGE,
		"Wazuh logcollector global statistics period end time.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_global_files_bytes_total", METRIC_TYPE_COUNTER,
		"Total bytes read from monitored log files since logcollector startup, by location.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_global_files_events_total", METRIC_TYPE_COUNTER,
		"Total log events collected since logcollector startup, by location.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_global_files_location", METRIC_TYPE_GAUGE,
		"Monitored log file location marker (value 1) with location label.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_global_files_targets_drops_total", METRIC_TYPE_COUNTER,
		"Total log events dropped at delivery targets since logcollector startup, by location.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_global_files_targets_name", METRIC_TYPE_GAUGE,
		"Log delivery target name marker (value 1) with location label.");

	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_interval_start", METRIC_TYPE_GAUGE,
		"Wazuh logcollector interval statistics period start time.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_interval_end", METRIC_TYPE_GAUGE,
		"Wazuh logcollector interval statistics period end time.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_interval_files_bytes", METRIC_TYPE_GAUGE,
		"Bytes read from monitored log files during the last statistics interval, by location.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_interval_files_events", METRIC_TYPE_GAUGE,
		"Log events collected during the last statistics interval, by location.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_interval_files_location", METRIC_TYPE_GAUGE,
		"Monitored log file location marker (value 1) for the last statistics interval.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_interval_files_targets_drops", METRIC_TYPE_GAUGE,
		"Log events dropped at delivery targets during the last statistics interval, by location.");
	namespace_metric_family_set(NULL, carg, "wazuh_logcollector_interval_files_targets_name", METRIC_TYPE_GAUGE,
		"Log delivery target name marker (value 1) for the last statistics interval.");
}

static uint64_t wazuh_ts_from_string(const char *s)
{
	struct tm tm = {0};
	if (!s || !strptime(s, "%Y-%m-%d %H:%M:%S", &tm))
		return 0;
	return (uint64_t)mktime(&tm);
}

static void wazuh_logcollector_emit_section(context_arg *carg, json_t *section, int is_global)
{
	const char *start = json_string_value(json_object_get(section, "start"));
	const char *end = json_string_value(json_object_get(section, "end"));
	uint64_t start_ts = wazuh_ts_from_string(start);
	uint64_t end_ts = wazuh_ts_from_string(end);
	json_t *files;
	size_t i;
	size_t n;

	if (is_global) {
		if (start_ts)
			metric_add_auto("wazuh_logcollector_global_start", &start_ts, DATATYPE_UINT, carg);
		if (end_ts)
			metric_add_auto("wazuh_logcollector_global_end", &end_ts, DATATYPE_UINT, carg);
	} else {
		if (start_ts)
			metric_add_auto("wazuh_logcollector_interval_start", &start_ts, DATATYPE_UINT, carg);
		if (end_ts)
			metric_add_auto("wazuh_logcollector_interval_end", &end_ts, DATATYPE_UINT, carg);
	}

	files = json_object_get(section, "files");
	if (!json_is_array(files))
		return;
	n = json_array_size(files);
	for (i = 0; i < n; i++) {
		json_t *file = json_array_get(files, i);
		const char *location = json_string_value(json_object_get(file, "location"));
		int64_t events = json_integer_value(json_object_get(file, "events"));
		int64_t bytes = json_integer_value(json_object_get(file, "bytes"));
		json_t *targets = json_object_get(file, "targets");
		uint64_t one = 1;
		size_t ti;
		size_t tn;

		if (!location)
			continue;

		if (is_global) {
			metric_add_labels("wazuh_logcollector_global_files_location", &one, DATATYPE_UINT, carg, "location", (char *)location);
			metric_add_labels("wazuh_logcollector_global_files_events_total", &events, DATATYPE_INT, carg, "location", (char *)location);
			metric_add_labels("wazuh_logcollector_global_files_bytes_total", &bytes, DATATYPE_INT, carg, "location", (char *)location);
		} else {
			metric_add_labels("wazuh_logcollector_interval_files_location", &one, DATATYPE_UINT, carg, "location", (char *)location);
			metric_add_labels("wazuh_logcollector_interval_files_events", &events, DATATYPE_INT, carg, "location", (char *)location);
			metric_add_labels("wazuh_logcollector_interval_files_bytes", &bytes, DATATYPE_INT, carg, "location", (char *)location);
		}

		if (!json_is_array(targets))
			continue;
		tn = json_array_size(targets);
		for (ti = 0; ti < tn; ti++) {
			json_t *target = json_array_get(targets, ti);
			const char *name = json_string_value(json_object_get(target, "name"));
			int64_t drops = json_integer_value(json_object_get(target, "drops"));
			if (is_global) {
				if (name)
					metric_add_labels("wazuh_logcollector_global_files_targets_name", &one, DATATYPE_UINT, carg, "location", (char *)location);
				metric_add_labels("wazuh_logcollector_global_files_targets_drops_total", &drops, DATATYPE_INT, carg, "location", (char *)location);
			} else {
				if (name)
					metric_add_labels("wazuh_logcollector_interval_files_targets_name", &one, DATATYPE_UINT, carg, "location", (char *)location);
				metric_add_labels("wazuh_logcollector_interval_files_targets_drops", &drops, DATATYPE_INT, carg, "location", (char *)location);
			}
		}
	}
}

void wazuh_stats_parser(char *metrics, size_t size, void *data, char *filename)
{
	int64_t i = 0;
	context_arg *carg = data;
	char metric_name[WAZUH_SIZE];
	uint64_t metric_len;
	if (strstr(filename, "agentd")) {
		strlcpy(metric_name, "wazuh_agentd_", 14);
		metric_len = 13;
	} else if (strstr(filename, "remoted")) {
		strlcpy(metric_name, "wazuh_remoted_", 15);
		metric_len = 14;
	} else if (strstr(filename, "analysisd")) {
		strlcpy(metric_name, "wazuh_analysisd_", 16);
		metric_len = 15;
	} else if (strstr(filename, "logcollector")) {
		strlcpy(metric_name, "wazuh_logcollector_", 20);
		metric_len = 19;
	} else {
		strlcpy(metric_name, "wazuh_", 7);
		metric_len = 6;
	}

	while(i < size)
	{
		if ((carg->log_level ? carg->log_level : ac->log_level) >= L_TRACE)
		{
			char str[255];
			size_t n = strcspn(metrics+i, "\n");
			if (n >= sizeof(str))
				n = sizeof(str) - 1;
			strlcpy(str, metrics+i, n + 1);
			carglog(carg, L_TRACE, "wazuh processing string: %"PRId64" < %zu: '%s'\n", i, size, str);
		}

		if (metrics[i] == '#') {
			carglog(carg, L_DEBUG, "> wazuh skip comment line\n");
			i += strcspn(metrics+i, "\n");
			i += strspn(metrics+i, "\n");
			continue;
		}

		if (metrics[i] == '\n') {
			carglog(carg, L_DEBUG, "> wazuh skip new line\n");
			i += strcspn(metrics+i, "\n");
			i += strspn(metrics+i, "\n");
			continue;
		}

		size_t metric_size = strcspn(metrics+i, "=' ");
		size_t metric_name_rem = sizeof(metric_name) - metric_len;
		size_t metric_copy = metric_size < (metric_name_rem - 1) ? metric_size : (metric_name_rem - 1);
		strlcpy(metric_name + metric_len, metrics+i, metric_copy + 1);

		i += metric_size;
		i += strspn(metrics+i, "=' ");
		char metric_value[WAZUH_SIZE];
		size_t value_size = strcspn(metrics+i, "\n");
		size_t value_copy = value_size < (sizeof(metric_value) - 1) ? value_size : (sizeof(metric_value) - 1);
		strlcpy(metric_value, metrics+i, value_copy + 1);
		carglog(carg, L_DEBUG, "got metric name '%s' = '%s'\n", metric_name, metric_value);

		struct tm tm = {0};
		if (strptime(metric_value, "%Y-%m-%d %H:%M:%S", &tm)) {
			uint64_t val = mktime(&tm);
			carglog(carg, L_DEBUG, "\tvalue is timestamp: %"PRIu64"\n", val);
			wazuh_metric_set(carg, metric_name);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else if (isdigit(*metric_value)) {
			uint64_t val = strtoull(metric_value, NULL, 10);
			carglog(carg, L_DEBUG, "\tvalue is digit: %"PRIu64"\n", val);
			wazuh_metric_set(carg, metric_name);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else if (strstr(metric_value, "connected")) {
			uint64_t val = 1;
			carglog(carg, L_DEBUG, "\tvalue is conn: %"PRIu64"\n", val);
			wazuh_metric_set(carg, metric_name);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else if (strstr(metric_value, "pending")) {
			uint64_t val = 2;
			carglog(carg, L_DEBUG, "\tvalue is pend: %"PRIu64"\n", val);
			wazuh_metric_set(carg, metric_name);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else if (strstr(metric_value, "disconnected")) {
			uint64_t val = 0;
			carglog(carg, L_DEBUG, "\tvalue is disc: %"PRIu64"\n", val);
			wazuh_metric_set(carg, metric_name);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else {
			uint64_t val = 1;
			carglog(carg, L_DEBUG, "\tvalue is label: %"PRIu64"\n", val);
			wazuh_metric_set(carg, metric_name);
			metric_add_labels(metric_name, &val, DATATYPE_UINT, carg, "type", metric_value);
		}



		i += strcspn(metrics+i, "\n");
		i += strspn(metrics+i, "\n");
	}

	carg->parser_status = 1;
}

void wazuh_logcollector_parser_json(char *metrics, size_t size, void *data, char *filename)
{
	context_arg *carg = data;
	json_error_t error;
	json_t *root;
	json_t *global;
	json_t *interval;

	(void)filename;
	wazuh_logcollector_metric_families_set(carg);
	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	root = json_loadb(metrics, size, 0, &error);
	if (!root) {
		carglog(carg, L_ERROR, "wazuh logcollector json error on line %d: %s\n", error.line, error.text);
		carg->parser_status = 0;
		return;
	}

	global = json_object_get(root, "global");
	interval = json_object_get(root, "interval");
	if (json_is_object(global))
		wazuh_logcollector_emit_section(carg, global, 1);
	if (json_is_object(interval))
		wazuh_logcollector_emit_section(carg, interval, 0);

	json_decref(root);
	carg->parser_status = 1;
}

void wazuh_handler(char *metrics, size_t size, context_arg *carg)
{
	char *filename = strdup(carg->host);
	char *dir_name = dirname(filename);

	char *agentd_path = malloc(255);
	snprintf(agentd_path, 254, "%s/wazuh-agentd.state", dir_name);
	read_from_file(agentd_path, 0, wazuh_stats_parser, carg);

	char *remoted_path = malloc(255);
	snprintf(remoted_path, 254, "%s/wazuh-remoted.state", dir_name);
	read_from_file(remoted_path, 0, wazuh_stats_parser, carg);

	char *analysisd_path = malloc(255);
	snprintf(analysisd_path, 254, "%s/wazuh-analysisd.state", dir_name);
	read_from_file(analysisd_path, 0, wazuh_stats_parser, carg);

	char *logcollector_path = malloc(255);
	snprintf(logcollector_path, 254, "%s/wazuh-logcollector.state", dir_name);
	read_from_file(logcollector_path, 0, wazuh_logcollector_parser_json, carg);
	free(filename);
}

void wazuh_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("wazuh");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = wazuh_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"wazuh", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
