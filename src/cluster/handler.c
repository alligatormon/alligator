#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/labels.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "query/type.h"
#include "common/logs.h"
#include "metric/metric_types.h"
#include "main.h"
#define ROW_LEN 8192
#define LABEL_LEN 4096
#define CLUSTER_HANDLER_REPLICATYPE 0
#define CLUSTER_HANDLER_SERVER 1
#define CLUSTER_HANDLER_DATA 2
#define MAX_METRIC_NAME_SIZE 1024

void cluster_sync_handler(char *metrics, size_t size, context_arg *carg)
{
	/* Never %s the payload: a full oplog ring is multi‑MB. */
	carglog(carg, L_DEBUG, "cluster_sync_handler: key=%s size=%zu\n",
		carg && carg->key ? carg->key : "-", size);

	char field[LABEL_LEN];
	char replica[LABEL_LEN];
	char data[LABEL_LEN];
	cluster_node* cn = carg ? carg->cluster_node : NULL;
	cluster_server_oplog *srvoplog = carg ? carg->data : NULL;
	r_time time_now = setrtime();
	uint64_t applied = 0;
	char namespacename[255];

	if (!carg || !cn || !metrics)
		return;
	carg->namespace = NULL;
	if (srvoplog)
		srvoplog->ttl = time_now.sec + 60;

	for (uint64_t i = 0; i < size; i++)
	{
		uint8_t cluster_type = CLUSTER_HANDLER_REPLICATYPE;
		uint64_t line_len = str_get_next(metrics, field, LABEL_LEN, "\n", &i);
		uint8_t is_primary = 0;
		char metric_name[MAX_METRIC_NAME_SIZE], label_name[MAX_METRIC_NAME_SIZE], label_key[MAX_METRIC_NAME_SIZE];
		*metric_name = 0;
		*replica = 0;
		uint8_t next_is_value = 0;
		uint8_t value_seen = 0;
		uint8_t lbl_consumed = 0;
		double value = 0;
		alligator_ht *lbl;

		/* Truncated copy cannot be parsed safely (type/value would be wrong). */
		if (line_len >= LABEL_LEN - 1) {
			carglog(carg, L_WARN,
				"cluster_sync_handler: skip oversized line (%" PRIu64 " bytes, limit %d)\n",
				line_len, LABEL_LEN - 1);
			continue;
		}
		if (!line_len)
			continue;

		lbl = alligator_ht_init(NULL);
		if (!lbl)
			continue;

		for (uint64_t j = 0; j < line_len; j++)
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

				if (!is_primary && cn->name) {
					uint16_t cur = strlcpy(namespacename, cn->name, 255);
					size_t remain = cur < sizeof(namespacename) ? sizeof(namespacename) - cur : 0;
					if (remain > 0) {
						strlcpy(namespacename + cur, ":", remain);
						cur = strnlen(namespacename, sizeof(namespacename));
						remain = cur < sizeof(namespacename) ? sizeof(namespacename) - cur : 0;
						if (remain > 0)
							strlcpy(namespacename + cur, replica, remain);
					}
					carg->namespace = namespacename;
				}
			}
			else if (cluster_type == CLUSTER_HANDLER_DATA)
			{
				if (!*metric_name)
					strlcpy(metric_name, data, MAX_METRIC_NAME_SIZE);
				else if ((!next_is_value) && (j != line_len)) {
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
					value_seen = 1;
				}
				else {
					uint8_t dt_type = atoi(data);
					if (!value_seen || !*metric_name) {
						carglog(carg, L_WARN,
							"cluster_sync_handler: incomplete record for '%s', skip\n",
							*metric_name ? metric_name : "-");
						break;
					}
					if (dt_type == METRIC_TYPE_COUNTER || dt_type == METRIC_TYPE_HISTOGRAM)
						metric_update(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
					else
						metric_add(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
					lbl_consumed = 1;
					++applied;

					carglog(carg, L_DEBUG, "\t\t\treplication metric add/update(%u) %s:%lf to namespace '%s'\n", dt_type, metric_name, value, carg->namespace);
					break; /* lbl ownership transferred; do not touch further fields */
				}
			}
		}
		if (!lbl_consumed)
			labels_hash_free(lbl);
		/* namespacename is stack-local; never leave it on the long-lived sync carg. */
		carg->namespace = NULL;
	}
	if (applied && srvoplog)
		__atomic_fetch_add(&srvoplog->stat_applied, applied, __ATOMIC_RELAXED);
	carg->namespace = NULL;
	carg->parser_status = 1;
}

void cluster_aggregate_sync_handler(char *metrics, size_t size, context_arg *carg)
{
	uint8_t locked = 0;
	char instance[ROW_LEN];
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
	size_t copy_size = instance_size < (sizeof(instance) - 1) ? instance_size : (sizeof(instance) - 1);
	strlcpy(instance, tmp + 9, copy_size + 1);
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
		metric_add_labels2("alligator_cluster_primary_server", &okval, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
		metric_add_labels2("alligator_cluster_primary_ttl", &ttl, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
		metric_add_labels2("alligator_cluster_primary_settime", &settime, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
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

	metric_add_labels2("alligator_cluster_primary_server", &okval, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
	metric_add_labels2("alligator_cluster_primary_ttl", &ttl, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
	metric_add_labels2("alligator_cluster_primary_settime", &settime, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", cn->name);
	++cn->update_count;
}
