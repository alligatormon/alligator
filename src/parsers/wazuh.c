#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <time.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/validator.h"
#include "common/http.h"
#include "common/logs.h"
#include "events/fs_read.h"
#include "main.h"
#define WAZUH_SIZE 1000

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
		if (carg->log_level > 3)
		{
			char str[255];
			strlcpy(str, metrics+i, strcspn(metrics+i, "\n")+1);
			printf("wazuh processing string: %"d64" < %zu: '%s'\n", i, size, str);
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
		strlcpy(metric_name + metric_len, metrics+i, metric_size+1);

		i += metric_size;
		i += strspn(metrics+i, "=' ");
		char metric_value[WAZUH_SIZE];
		size_t value_size = strcspn(metrics+i, "\n");
		strlcpy(metric_value, metrics+i, value_size+1);
		carglog(carg, L_INFO, "got metric name '%s' = '%s'\n", metric_name, metric_value);

		struct tm tm = {0};
		if (strptime(metric_value, "%Y-%m-%d %H:%M:%S", &tm)) {
			uint64_t val = mktime(&tm);
			carglog(carg, L_INFO, "\tvalue is timestamp: %"PRIu64"\n", val);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else if (isdigit(*metric_value)) {
			uint64_t val = strtoull(metric_value, NULL, 10);
			carglog(carg, L_INFO, "\tvalue is digit: %"PRIu64"\n", val);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else if (strstr(metric_value, "connected")) {
			uint64_t val = 1;
			carglog(carg, L_INFO, "\tvalue is conn: %"PRIu64"\n", val);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else if (strstr(metric_value, "pending")) {
			uint64_t val = 2;
			carglog(carg, L_INFO, "\tvalue is pend: %"PRIu64"\n", val);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else if (strstr(metric_value, "disconnected")) {
			uint64_t val = 0;
			carglog(carg, L_INFO, "\tvalue is disc: %"PRIu64"\n", val);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		} else {
			uint64_t val = 1;
			carglog(carg, L_INFO, "\tvalue is label: %"PRIu64"\n", val);
			metric_add_labels(metric_name, &val, DATATYPE_UINT, carg, "type", metric_value);
		}



		i += strcspn(metrics+i, "\n");
		i += strspn(metrics+i, "\n");
	}

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

	//char *logcollector_path = malloc(255);
	//snprintf(logcollector_path, 254, "%s/wazuh-logcollector.state", carg->host);
	//read_from_file(logcollector_path, 0, wazuh_logcollector_parser, carg);
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
