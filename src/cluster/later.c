#include "events/context_arg.h"
#include "cluster/get.h"

int cluster_come_later(context_arg *carg)
{
	if (!carg->cluster)
		return 0;

	cluster_node *cn = carg->cluster_node;
	if (!cn)
		cn = cluster_get(carg->cluster);

	if (!cn)
		return 0;

	printf("LATER update_count is %"PRIu64"\n", cn->update_count);

	if (cn->update_count < (cn->servers_size * 8))
		return 0;

	r_time time_now = setrtime();

	printf("cluster_come_later check: '%s':'%s' && %d %d, result: %d\n", cn->shared_lock_instance ? cn->shared_lock_instance->s : NULL, carg->instance, (cn->ttl >= time_now.sec), carg->parser_status, ((string_compare(cn->shared_lock_instance, carg->instance, strlen(carg->instance))) && (cn->ttl >= time_now.sec)));
	if ((string_compare(cn->shared_lock_instance, carg->instance, strlen(carg->instance))) && (cn->ttl >= time_now.sec))
		return 1;

	cn->parser_status = carg->parser_status;
	//printf("SET parser_status to %d\n", cn->parser_status);

	return 0;
}
