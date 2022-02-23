#include <stdint.h>
#include "common/selector.h"
#include "cluster/type.h"
#include "events/context_arg.h"
#define CLUSTER_NOT_DEFINED_REPLICA "# not defined 'replica' http arg"
#define CLUSTER_NOT_DEFINED_NAME "# not defined 'name' http arg"
#define CLUSTER_API_NOT_FOUND "# cluster not found"
#define CLUSTER_API_OPLOG_IS_NULL "# cluster oplog is null for instance"

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
		string_cat(oplog, CLUSTER_NOT_DEFINED_REPLICA, strlen(CLUSTER_NOT_DEFINED_REPLICA));
		return oplog;
	}

	if (!name)
	{
		string_cat(oplog, CLUSTER_NOT_DEFINED_NAME, strlen(CLUSTER_NOT_DEFINED_NAME));
		return oplog;
	}

	cluster_node *cn = cluster_get(name);
	if (!cn)
	{
		string_cat(oplog, CLUSTER_API_NOT_FOUND, strlen(CLUSTER_API_NOT_FOUND));
		return oplog;
	}

	cluster_server_oplog *srvoplog = get_server_oplog_by_node(cn, replica);
	if (!srvoplog)
	{
		string_cat(oplog, CLUSTER_API_OPLOG_IS_NULL, strlen(CLUSTER_API_OPLOG_IS_NULL));
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
