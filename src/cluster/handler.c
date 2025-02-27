#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "query/type.h"
#include "common/selector.h"
#include "common/logs.h"
#include "parsers/metric_types.h"
#include "main.h"
#define ROW_LEN 8192
#define LABEL_LEN 4096
#define CLUSTER_HANDLER_REPLICATYPE 0
#define CLUSTER_HANDLER_SERVER 1
#define CLUSTER_HANDLER_DATA 2
#define MAX_METRIC_NAME_SIZE 1024

void cluster_sync_handler(char *metrics, size_t size, context_arg *carg)
{
	carglog(carg, L_DEBUG, "=======\ncluster_sync_handler (%s/%zu) metrics:\n%s\n", carg->key, size, metrics);

	char field[LABEL_LEN];
	char replica[LABEL_LEN];
	char data[LABEL_LEN];
	cluster_node* cn = carg->cluster_node;
	cluster_server_oplog *srvoplog = carg->data;
	r_time time_now = setrtime();
	srvoplog->ttl = time_now.sec + 60;
	char namespacename[255];

	for (uint64_t i = 0; i < size; i++)
	{
		*field = 0;
		uint8_t cluster_type = CLUSTER_HANDLER_REPLICATYPE;
		uint64_t field_size = str_get_next(metrics, field, LABEL_LEN, "\n", &i);
		uint8_t is_primary = 0;
		char metric_name[MAX_METRIC_NAME_SIZE], label_name[MAX_METRIC_NAME_SIZE], label_key[MAX_METRIC_NAME_SIZE];
		*metric_name = 0;
		*replica = 0;
		uint8_t next_is_value = 0;
		double value;
		alligator_ht *lbl = alligator_ht_init(NULL);

		for (uint64_t j = 0; j < field_size; j++)
		{
			str_get_next(field, data, LABEL_LEN, "\t", &j);

			if (cluster_type == CLUSTER_HANDLER_REPLICATYPE)
			{
				if (!strcmp(data, "replica"))
				{
					cluster_type = CLUSTER_HANDLER_SERVER;
				}
				else
				{
					cluster_type = CLUSTER_HANDLER_DATA;
					is_primary = 1;
				}
			}
			else if (cluster_type == CLUSTER_HANDLER_SERVER)
			{
				strlcpy(replica, data, LABEL_LEN);
				cluster_type = CLUSTER_HANDLER_DATA;

				if (!is_primary) {
					uint16_t cur = strlcpy(namespacename, cn->name, 255);
					strlcpy(namespacename + cur, ":", 255);
					strlcpy(namespacename + cur + 1, replica, 255);
					carg->namespace = namespacename;
				}
			}
			else if (cluster_type == CLUSTER_HANDLER_DATA)
			{
				if (!*metric_name)
					strlcpy(metric_name, data, MAX_METRIC_NAME_SIZE);
				else if ((!next_is_value) && (j != field_size)) {
					if (!*data) {
						next_is_value = 1;
						continue; // two tabs before value
					}

					size_t sep = strcspn_n(data, "=", MAX_METRIC_NAME_SIZE-1);
					strlcpy(label_name, data, sep+1);
					strlcpy(label_key, data+sep+1, MAX_METRIC_NAME_SIZE);
					labels_hash_insert_nocache(lbl, label_name, label_key);
				}
				else if (next_is_value) {
					next_is_value = 0;
					value = strtod(data, NULL);
				}
				else {
					uint8_t dt_type = atoi(data);
					if (dt_type == METRIC_TYPE_COUNTER || dt_type == METRIC_TYPE_HISTOGRAM)
						metric_update(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
					else
						metric_add(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);

					carglog(carg, L_DEBUG, "\t\t\treplication metric add/update(%u) %s:%lf to namespace '%s'\n", dt_type, metric_name, value, carg->namespace);
					carg->namespace = NULL;
				}
			}
		}
	}
	carg->parser_status = 1;
}

void cluster_aggregate_sync_handler(char *metrics, size_t size, context_arg *carg)
{
	uint8_t locked = 0;
	char instance[size];
	uint64_t settime;
	uint64_t ttl;
	char *tmp = metrics;
	size_t instance_size;
	r_time time_now = setrtime();

	tmp = strstr(tmp, "locked:");
	if (!tmp)
		return;
	locked = strtod(tmp + 7, &tmp);

	tmp = strstr(tmp, "instance:");
	if (!tmp)
		return;
	instance_size = strcspn(tmp + 9, "\t");
	strlcpy(instance, tmp + 9, instance_size + 1);
	tmp += 9 + instance_size;

	tmp = strstr(tmp, "settime:");
	if (!tmp)
		return;
	settime = strtoull(tmp + 8, &tmp, 10);

	tmp = strstr(tmp, "ttl:");
	if (!tmp)
		return;
	ttl = strtoull(tmp + 4, &tmp, 10);

	carglog(carg, L_DEBUG, "locked: %"PRIu8", instance: %s, settime: %"PRIu64", ttl: %"PRIu64"\n", locked, instance, settime, ttl);

	uint64_t okval = 1;
	cluster_node *cn = carg->data;

	if (ttl < time_now.sec)
	{
		okval = 0;
		metric_add_labels2("aggregator_cluster_primary_server", &okval, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
		metric_add_labels2("aggregator_cluster_primary_ttl", &ttl, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
		metric_add_labels2("aggregator_cluster_primary_settime", &settime, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
		return;
	}

	if (!cn->shared_lock_instance)
		cn->shared_lock_instance = string_new();

	if (string_compare(cn->shared_lock_instance, instance, instance_size) || (settime < cn->shared_lock_set_time))
	{
		string_copy(cn->shared_lock_instance, instance, instance_size);
		carglog(carg, L_DEBUG, "handler set primary %s\n", cn->shared_lock_instance->s);
		cn->shared_lock_set_time = time_now.sec;
		cn->ttl = time_now.sec + 60;
	}
	else
		carglog(carg, L_DEBUG, "set old cluster primary %s to %s\n", cn->shared_lock_instance->s, instance);

	metric_add_labels2("aggregator_cluster_primary_server", &okval, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
	metric_add_labels2("aggregator_cluster_primary_ttl", &ttl, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
	metric_add_labels2("aggregator_cluster_primary_settime", &settime, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
	++cn->update_count;
}
