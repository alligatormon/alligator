#include <stdint.h>
#include "common/selector.h"
#include "cluster/type.h"
#include "events/context_arg.h"
#include "metric/namespace.h"
#define CLUSTER_NOT_DEFINED_REPLICA "# not defined 'replica' http arg\n"
#define CLUSTER_NOT_DEFINED_NAME "# not defined 'name' http arg\n"
#define CLUSTER_API_NOT_FOUND "# cluster not found\n"
#define CLUSTER_API_OPLOG_IS_NULL "# cluster oplog is null for instance\n"
#define CLUSTER_API_SHAREDLOCK_IS_NULL "\n"

cluster_server_oplog *get_server_oplog_by_node(cluster_node *cn, char *replica)
{
	uint64_t min = 0;
	uint64_t max = cn->servers_size - 1;
	while (min <= max)
	{
		uint64_t new = (min + max) / 2;
		int rc = strcmp(cn->servers[new].name, replica);

	 	if (!rc)
			return &cn->servers[new];
		if (rc < 0)
			min = new + 1;
		else
			max = new - 1;
	}

	return NULL;
}

string* cluster_get_server_data(char *replica, char *name)
{
	string *oplog = NULL;

	if (!replica)
	{
		oplog = string_init_dupn(CLUSTER_NOT_DEFINED_REPLICA, strlen(CLUSTER_NOT_DEFINED_REPLICA));
		return oplog;
	}

	if (!name)
	{
		oplog = string_init_dupn(CLUSTER_NOT_DEFINED_NAME, strlen(CLUSTER_NOT_DEFINED_NAME));
		return oplog;
	}

	cluster_node *cn = cluster_get(name);
	if (!cn)
	{
		oplog = string_init_dupn(CLUSTER_API_NOT_FOUND, strlen(CLUSTER_API_NOT_FOUND));
		return oplog;
	}

	cluster_server_oplog *srvoplog = get_server_oplog_by_node(cn, replica);
	if (!srvoplog)
	{
		oplog = string_init_dupn(CLUSTER_API_OPLOG_IS_NULL, strlen(CLUSTER_API_OPLOG_IS_NULL));
		return oplog;
	}

	oplog = oplog_record_get_string(srvoplog->oprec);

	return oplog;
}

string* cluster_shift_server_data(char *replica, char *name)
{
	string *oplog = NULL;

	if (!replica)
		return NULL;

	if (!name)
		return NULL;

	cluster_node *cn = cluster_get(name);
	if (!cn)
		return oplog;

	cluster_server_oplog *srvoplog = get_server_oplog_by_node(cn, replica);
	if (!srvoplog)
		return oplog;

	oplog = oplog_record_shift_string(srvoplog->oprec);

	return oplog;
}

string *cluster_get_sharedlock_string(cluster_node *cn, uint64_t locked)
{
	string *retlock = string_new();
	string_cat(retlock, "locked:", 7);
	string_uint(retlock, locked);
	string_cat(retlock, "\tinstance:", 10);
	string_string_cat(retlock, cn->shared_lock_instance);
	string_cat(retlock, "\tsettime:", 9);
	string_uint(retlock, cn->shared_lock_set_time);
	string_cat(retlock, "\tttl:", 5);
	string_uint(retlock, cn->ttl);
	string_cat(retlock, "\n", 1);
	return retlock;
}

string* cluster_get_sharedlock(char *name)
{
	string *retlock = NULL;

	if (!name)
	{
		retlock = string_init_dup(CLUSTER_NOT_DEFINED_NAME);
		//string_cat(retlock, CLUSTER_NOT_DEFINED_NAME, strlen(CLUSTER_NOT_DEFINED_NAME));
		return retlock;
	}

	cluster_node *cn = cluster_get(name);
	if (!cn)
	{
		retlock = string_init_dup(CLUSTER_API_NOT_FOUND);
		//string_cat(retlock, CLUSTER_API_NOT_FOUND, strlen(CLUSTER_API_NOT_FOUND));
		return retlock;
	}

	string *instance = cn->shared_lock_instance;
	if (!instance)
	{
		retlock = string_init_dup(CLUSTER_API_SHAREDLOCK_IS_NULL);
		//string_cat(retlock, CLUSTER_API_SHAREDLOCK_IS_NULL, strlen(CLUSTER_API_SHAREDLOCK_IS_NULL));
		return retlock;
	}

	retlock = cluster_get_sharedlock_string(cn, (uint64_t)0);

	return retlock;
}

string* cluster_set_sharedlock(char *replica, char *name, void *data)
{
	string *retlock= NULL;
	context_arg *carg = data;

	if (!replica)
		return NULL;

	if (!name)
		return NULL;

	cluster_node *cn = cluster_get(name);
	if (!cn)
		return retlock;

	r_time time_now = setrtime();
	uint64_t locked = 0;
	uint64_t okval = 1;
	++cn->update_count;
	//printf("ttl is %d time now is %d, result is %d\n", cn->ttl, time_now.sec, (cn->ttl < time_now.sec));
	if (cn->ttl < time_now.sec)
	{
		if (!cn->shared_lock_instance)
			cn->shared_lock_instance = string_new();

		string_copy(cn->shared_lock_instance, replica, strlen(replica));
		locked = 1;
	}

	if (!string_compare(cn->shared_lock_instance, replica, strlen(replica)))
	{
		cn->shared_lock_set_time = time_now.sec;
		cn->ttl = time_now.sec + 60;
	}

	metric_add_labels2("aggregator_cluster_primary_server", &okval, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", name);
	metric_add_labels2("aggregator_cluster_primary_ttl", &cn->ttl, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", name);
	metric_add_labels2("aggregator_cluster_primary_settime", &cn->shared_lock_set_time, DATATYPE_UINT, carg, "primary", cn->shared_lock_instance->s, "cluster", name);

	retlock = cluster_get_sharedlock_string(cn, locked);

	return retlock;
}
