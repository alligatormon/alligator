#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "query/query.h"
#include "metric/namespace.h"
#include "main.h"
#define LABEL_LEN 255
#define CLUSTER_HANDLER_REPLICATYPE 0
#define CLUSTER_HANDLER_SERVER 1
#define CLUSTER_HANDLER_DATA 2

void cluster_sync_handler(char *metrics, size_t size, context_arg *carg)
{
	//if (size > 1)
	//	printf("=======\ncluster_sync_handler (%s/%zu) metrics:\n%s\n", carg->key, size, metrics);

	char field[LABEL_LEN];
	char replica[LABEL_LEN];
	char data[LABEL_LEN];

	for (uint64_t i = 0; i < size; i++)
	{
		*field = 0;
		uint8_t cluster_type = CLUSTER_HANDLER_REPLICATYPE;
		uint64_t field_size = str_get_next(metrics, field, LABEL_LEN, "\n", &i);
		//printf("\t%s\n", field);
		//uint8_t is_primary = 0;
		*replica = 0;

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
					//is_primary = 1;
				}
			}
			else if (cluster_type == CLUSTER_HANDLER_SERVER)
			{
				strlcpy(replica, data, LABEL_LEN);
				cluster_type = CLUSTER_HANDLER_DATA;
			}
			else if (cluster_type == CLUSTER_HANDLER_DATA)
			{
				//printf("\t\t{'primary': %"PRIu8", 'replica': '%s', 'data': '%s'\n", is_primary, replica, data);
			}
		}
	}
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

	if (carg->log_level > 0)
		printf("locked: %"PRIu8", instance: %s, settime: %"PRIu64", ttl: %"PRIu64"\n", locked, instance, settime, ttl);

	uint64_t okval = 1;
	cluster_node *cn = carg->data;

	if (ttl < time_now.sec)
	{
		okval = 0;
		metric_add_labels2("aggregator_cluster_primary_server", &okval, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
		metric_add_labels2("aggregator_cluster_primary_ttl", &ttl, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
		metric_add_labels2("aggregator_cluster_primary_settime", &settime, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
		if (carg->log_level > 1)
			printf("error! primary not found\n");
		return;
	}

	if (!cn->shared_lock_instance)
		cn->shared_lock_instance = string_new();

	if (string_compare(cn->shared_lock_instance, instance, instance_size) || (settime < cn->shared_lock_set_time))
	{
		string_copy(cn->shared_lock_instance, instance, instance_size);
		//printf("handler set primary %s\n", cn->shared_lock_instance->s);
		cn->shared_lock_set_time = time_now.sec;
		cn->ttl = time_now.sec + 60;
	}
	//else
	//	printf("set old cluster primary %s to %s\n", cn->shared_lock_instance->s, instance);

	metric_add_labels2("aggregator_cluster_primary_server", &okval, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
	metric_add_labels2("aggregator_cluster_primary_ttl", &ttl, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
	metric_add_labels2("aggregator_cluster_primary_settime", &settime, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
	++cn->update_count;
}
