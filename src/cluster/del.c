#include "cluster/type.h"
#include <stdlib.h>
#include "main.h"

void cluster_object_del(cluster_node *cn)
{
	if (!cn)
		return;

	if (cn->name)
		free(cn->name);
	if (cn->servers)
	{
		uint64_t servers_size = cn->servers_size;
		for (uint64_t i = 0; i < servers_size; i++)
		{
			if (cn->servers[i].name)
				free(cn->servers[i].name);
			oplog_record_free(cn->servers[i].oprec);
		}
		free(cn->servers);
	}
	if (cn->sharding_key)
	{
		uint64_t sharding_key_size = cn->sharding_key_size;
		for (uint64_t i = 0; i < sharding_key_size; i++)
		{
			if (cn->sharding_key[i])
				free(cn->sharding_key[i]);
		}
		free(cn->sharding_key);
	}

	if (cn->shared_lock_instance)
		string_free(cn->shared_lock_instance);

	maglev_loopup_item_clean(&cn->m_maglev_hash, 0);
	maglev_loopup_item_clean(&cn->m_maglev_hash, 1);

	free(cn);
}

void cluster_del_json(json_t *cluster)
{
	json_t *jname = json_object_get(cluster, "name");
	if (!jname)
		return;
	char *name = (char*)json_string_value(jname);

	cluster_node *cn = alligator_ht_search(ac->cluster, cluster_compare, name, tommy_strhash_u32(0, name));
	alligator_ht_remove_existing(ac->cluster, &(cn->node));
	cluster_object_del(cn);
}

void cluster_foreach_del_object(void *funcarg, void* arg)
{
	cluster_node *cn = arg;

	cluster_object_del(cn);
}

void cluster_del_all()
{
	alligator_ht_foreach_arg(ac->cluster, cluster_foreach_del_object, NULL);
	alligator_ht_done(ac->cluster);
	free(ac->cluster);
}
