#include "cluster/type.h"
#include "main.h"

cluster_node* cluster_get(char *name)
{
	if (!name)
		return NULL;

	cluster_node *cn = alligator_ht_search(ac->cluster, cluster_compare, name, tommy_strhash_u32(0, name));
	if (cn)
		return cn;
	else
		return NULL;
}

cluster_node *get_cluster_node_from_carg(context_arg *carg)
{
	cluster_node* cn = carg->cluster_node;
	if (!cn)
	{
		cn = carg->cluster_node = cluster_get(carg->cluster);
		if (!cn)
			return 0;

		for (uint64_t i = 0; i < cn->servers_size; i++)
		{
			if (carg->instance && !strcmp(cn->servers[i].name, carg->instance))
				cn->servers[i].is_me = 1;
		}
	}

	return cn;
}
