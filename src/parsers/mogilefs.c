#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#define MOGILEFS_METRIC_SIZE 256
#define MOGILEFS_FIELD_SIZE 1024

void mogilefs_full_item_hash(char *metrics, size_t size, context_arg *carg, char *name, char *itemname)
{
	if (carg->log_level > 0)
		printf("mogilefs_full_item_hash '%s' get params:\n'%s'\n", name, metrics);

	uint64_t len;
	char field[MOGILEFS_METRIC_SIZE];
	char item[MOGILEFS_METRIC_SIZE];
	char key[MOGILEFS_METRIC_SIZE];
	char value[MOGILEFS_METRIC_SIZE];
	char metric_name[MOGILEFS_METRIC_SIZE];

	uint64_t mnamesize = strlcpy(metric_name, name, MOGILEFS_METRIC_SIZE);

	for (uint64_t i = 0; i < size; i++)
	{
		uint64_t field_len = str_get_next(metrics, field, MOGILEFS_METRIC_SIZE, "&\r", &i);
		uint8_t itemid_offset = strcspn(field, "_&");
		if (itemid_offset == field_len)
		{
		
			if (carg->log_level > 1)
				printf("field is '%s', no value, continue\n", field);

			continue;
		}

		char *param = field + itemid_offset + 1;

		if (carg->log_level > 1)
			printf("field is '%s', param '%s'\n", field, param);

		len = strcspn(param, "=");
		strlcpy(item, field, itemid_offset + 1);
		strlcpy(key, param, len + 1);
		uint64_t valsize = strlcpy(value, param + len + 1, MOGILEFS_METRIC_SIZE);
		strlcpy(metric_name + mnamesize, key, MOGILEFS_METRIC_SIZE - mnamesize);

		int type = metric_value_validator(value, valsize);

		if (carg->log_level > 1)
			printf("item is '%s:%s' key is '%s' value is '%s', metric_name is '%s', type: %d\n", itemname, item, key, value, metric_name, type);
		if (!type)
		{
			uint64_t val = 1;
			metric_add_labels2(metric_name, &val, DATATYPE_UINT, carg, itemname, item, key, value);
		}
		else if (type == DATATYPE_DOUBLE)
		{
			double val = strtod(value, NULL);
			metric_add_labels(metric_name, &val, type, carg, itemname, item);
		}
		else
		{
			int64_t val = strtoll(value, NULL, 10);
			metric_add_labels(metric_name, &val, DATATYPE_INT, carg, itemname, item);
		}
	}
}

void mogilefs_device_list(char *metrics, size_t size, context_arg *carg)
{
	// OK dev1_status=alive&dev2_utilization=-&dev1_observed_state=writeable&dev1_devid=1&devices=2&dev2_weight=100&dev1_mb_asof=&dev1_mb_free=50712&dev2_reject_bad_md5=1&dev1_reject_bad_md5=1&dev2_mb_total=56752&dev1_weight=100&dev2_mb_used=6040&dev2_mb_asof=&dev1_utilization=-&dev1_mb_total=56752&dev2_observed_state=writeable&dev1_mb_used=6040&dev2_mb_free=50712&dev2_devid=2&dev2_status=alive&dev1_hostid=1&dev2_hostid=1

	if (carg->log_level > 0)
		printf("mogilefs_device_list get params:\n'%s'\n", metrics);

	if (strncmp(metrics, "OK", 2))
	{
		uint64_t val = 0;
		metric_add_auto("mogilefs_device_list_success", &val, DATATYPE_UINT, carg);
		return;
	}

	mogilefs_full_item_hash(metrics + 3, size - 3, carg, "mogilefs_device_list_", "device");
}

void mogilefs_fsck_status(char *metrics, size_t size, context_arg *carg)
{
	// OK end_fid=0&stop_time=0&running=0&policy_only=0&max_logid=0&host=&current_time=1622392476&max_fid_checked=0&start_time=0
	// OK stop_time=0&end_fid=0&max_fid_checked=0&current_time=1622393181&host=&max_logid=0&start_time=0&running=0&policy_only=0

	if (strncmp(metrics, "OK", 2))
	{
		if (carg->log_level > 0)
			printf("fsck status return: %s, return\n", metrics);

		uint64_t val = 0;
		metric_add_auto("mogilefs_fsck_running", &val, DATATYPE_UINT, carg);
		return;
	}

	uint64_t val = 1;
	metric_add_auto("mogilefs_fsck_running", &val, DATATYPE_UINT, carg);

	char field[MOGILEFS_METRIC_SIZE];
	char metric_name[MOGILEFS_METRIC_SIZE];
	char host[MOGILEFS_METRIC_SIZE];
	*host = 0;
	strlcpy(metric_name, "mogilefs_fsck_", 15);

	char *tmp = metrics + 3;
	uint64_t sz = size - 3;
	uint64_t len;

	uint64_t i = 0;

	for (; i < sz; i++)
	{
		uint64_t field_len = str_get_next(tmp, field, MOGILEFS_METRIC_SIZE, "&", &i);

		if (carg->log_level > 1)
			printf("field is '%s'\n", field);

		len = strcspn(field, "=");
		strlcpy(metric_name + 14, field, len + 1);

		if (carg->log_level > 1)
			printf("metric is '%s'\n", metric_name);

		if (!strncmp(field, "host", 4))
		{
			val = 1;
			strlcpy(host, field + len + 1, field_len - len - 1);

			if (carg->log_level > 1)
				printf("host is '%s'\n", host);

			if (*host)
				metric_add_labels(metric_name, &val, DATATYPE_UINT, carg, "host", field);
		}
		else
		{
			val = strtoull(field + len, NULL, 10);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		}
	}
}

void mogilefs_rebalance_status(char *metrics, size_t size, context_arg *carg)
{
	if (strncmp(metrics, "OK", 2))
	{
		if (carg->log_level > 0)
			printf("rebalance status return: %s, return\n", metrics);

		uint64_t val = 0;
		metric_add_auto("mogilefs_rebalance_running", &val, DATATYPE_UINT, carg);
		return;
	}

	uint64_t val = 1;
	metric_add_auto("mogilefs_rebalance_running", &val, DATATYPE_UINT, carg);

	char field[MOGILEFS_METRIC_SIZE];
	char metric_name[MOGILEFS_METRIC_SIZE];
	strlcpy(metric_name, "mogilefs_rebalance_", 15);

	char *tmp = metrics + 3;
	uint64_t sz = size - 3;
	uint64_t len;

	uint64_t i = 0;

	for (; i < sz; i++)
	{
		str_get_next(tmp, field, MOGILEFS_METRIC_SIZE, "&", &i);

		if (carg->log_level > 1)
			printf("field is '%s'\n", field);

		len = strcspn(field, "=");
		strlcpy(metric_name + 14, field, len + 1);

		if (carg->log_level > 1)
			printf("metric is '%s'\n", metric_name);

		if (!strncmp(field, "source_devs", 11) || !strncmp(field, "sdev_limit", 10)) { }
		else
		{
			val = strtoull(field + len, NULL, 10);
			metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
		}
	}
}

void mogilefs_host_list(char *metrics, size_t size, context_arg *carg)
{
	//OK host1_altip=&host1_http_get_port=&hosts=1&host1_hostip=127.0.0.1&host1_hostname=mogilestorage&host1_http_port=7500&host1_status=alive&host1_hostid=1&host1_altmask=

	if (carg->log_level > 0)
		printf("mogilefs_host_list get params:\n'%s'\n", metrics);

	if (strncmp(metrics, "OK", 2))
	{
		uint64_t val = 0;
		metric_add_auto("mogilefs_host_list_success", &val, DATATYPE_UINT, carg);
		return;
	}

	mogilefs_full_item_hash(metrics + 3, size - 3, carg, "mogilefs_host_list_", "host");
}

void mogilefs_cmd_items(char *metrics, size_t size, context_arg *carg, char *name)
{
	if (carg->log_level > 0)
		printf("mogilefs_cmd_items '%s' get params:\n'%s'\n", name, metrics);

	char field[MOGILEFS_METRIC_SIZE];
	char *value;
	char metric_name[MOGILEFS_METRIC_SIZE];

	uint64_t mnamesize = strlcpy(metric_name, name, MOGILEFS_METRIC_SIZE);

	for (uint64_t i = 0; i < size; i += 2)
	{
		str_get_next(metrics, field, MOGILEFS_METRIC_SIZE, "\r\n", &i);
		if (strstr(field, " pids"))
			continue;

		uint8_t item_offset = strcspn(field, "0123456789");
		if (item_offset < 2)
			continue;

		prometheus_metric_name_normalizer(field, item_offset - 1);
		strlcpy(metric_name + mnamesize, field, item_offset);
		value = field + item_offset;

		int type = metric_value_validator(value, strlen(value));

		if (ac->log_level > 1)
			printf("field is '%s', metric_name is '%s', val '%s', type '%d'\n", field, metric_name, value, type);

		if (type == DATATYPE_DOUBLE)
		{
			double val = strtod(value, NULL);
			metric_add_auto(metric_name, &val, DATATYPE_DOUBLE, carg);
		}
		else
		{
			int64_t val = strtoll(value, NULL, 10);
			metric_add_auto(metric_name, &val, DATATYPE_INT, carg);
		}
	}
}

void mogilefs_stats(char *metrics, size_t size, context_arg *carg)
{
	if (carg->log_level > 0)
		printf("mogilefs_stats get params:\n'%s'\n", metrics);

	mogilefs_cmd_items(metrics, size, carg, "mogilefs_stats_");
}

void mogilefs_jobs(char *metrics, size_t size, context_arg *carg)
{
	if (carg->log_level > 0)
		printf("mogilefs_jobs get params:\n'%s'\n", metrics);

	mogilefs_cmd_items(metrics, size, carg, "mogilefs_jobs_");
}

void mogilefs_queue(char *metrics, size_t size, context_arg *carg)
{
	if (carg->log_level > 0)
		printf("mogilefs_queue get params:\n'%s'\n", metrics);

	mogilefs_cmd_items(metrics, size, carg, "mogilefs_queue_");
}

string* mogilefs_device_list_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(strdup("get_devices\r\n"), 0, 0);
}

string* mogilefs_fsck_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(strdup("fsck_status\r\n"), 0, 0);
}

string* mogilefs_rebalance_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(strdup("rebalance_status\r\n"), 0, 0);
}

string* mogilefs_host_list_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(strdup("get_hosts\r\n"), 0, 0);
}

string* mogilefs_stats_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(strdup("!stats\r\n"), 0, 0);
}

string* mogilefs_jobs_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(strdup("!jobs\r\n"), 0, 0);
}

string* mogilefs_queue_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(strdup("!queue\r\n"), 0, 0);
}

void mogilefs_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("mogilefs");
	actx->handlers = 7;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = mogilefs_fsck_status;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = mogilefs_fsck_status_mesg;
	strlcpy(actx->handler[0].key,"mogilefs_fsck_status", 255);

	actx->handler[1].name = mogilefs_rebalance_status;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = mogilefs_rebalance_status_mesg;
	strlcpy(actx->handler[1].key,"mogilefs_rebalance_status", 255);

	actx->handler[2].name = mogilefs_host_list;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = mogilefs_host_list_mesg;
	strlcpy(actx->handler[2].key,"mogilefs_host_list", 255);

	actx->handler[3].name = mogilefs_device_list;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = mogilefs_device_list_mesg;
	strlcpy(actx->handler[3].key,"mogilefs_device_list", 255);

	actx->handler[4].name = mogilefs_stats;
	actx->handler[4].validator = NULL;
	actx->handler[4].mesg_func = mogilefs_stats_mesg;
	strlcpy(actx->handler[4].key,"mogilefs_stats", 255);

	actx->handler[5].name = mogilefs_jobs;
	actx->handler[5].validator = NULL;
	actx->handler[5].mesg_func = mogilefs_jobs_mesg;
	strlcpy(actx->handler[5].key,"mogilefs_jobs", 255);


	actx->handler[6].name = mogilefs_queue;
	actx->handler[6].validator = NULL;
	actx->handler[6].mesg_func = mogilefs_queue_mesg;
	strlcpy(actx->handler[6].key,"mogilefs_queue", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
