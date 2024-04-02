#include "cluster/type.h"
#include "common/json_parser.h"
#include <string.h>
#include "main.h"

int cluster_sort_compare(const void *a1, const void *a2)
{
	cluster_server_oplog *s1 = (cluster_server_oplog*)a1;
	cluster_server_oplog *s2 = (cluster_server_oplog*)a2;

	return strcmp(s1->name, s2->name);
}

void cluster_push_json(json_t *cluster)
{
	cluster_node *cn = calloc(1, sizeof(*cn));

	json_t *jname = json_object_get(cluster, "name");
	if (!jname)
		return;
	char *name = (char*)json_string_value(jname);
	cn->name = strdup(name);

	cn->size = 1000;
	cn->size = config_get_intstr_json(cluster, "size");

	json_t *jservers = json_object_get(cluster, "servers");
	if (jservers)
	{
		uint64_t servers_size = cn->servers_size = json_array_size(jservers);
		maglev_init(&cn->m_maglev_hash);
		int rc = 0;
		uint64_t hash_size = (servers_size * 100);
		while (!rc)
		{
			++hash_size;
			rc = is_maglev_prime(hash_size);
		}
		rc = maglev_update_service(&cn->m_maglev_hash, servers_size, hash_size);
		if (rc)
		{
			free(cn);
			return;
		}

		cn->servers = calloc(1, sizeof(cluster_server_oplog) * servers_size);
		for (uint64_t i = 0, j = 0; i < servers_size; i++, j++)
		{
			json_t *servers = json_array_get(jservers, i);
			char *data = (char*)json_string_value(servers);
			if (data)
			{
				cn->servers[j].name = strdup(data);
				cn->servers[j].oprec = oplog_record_init(cn->size);
				cn->servers[j].index = j;
				rc = maglev_add_node(&cn->m_maglev_hash, cn->servers[j].name, &cn->servers[j]);
				if (rc)
				{
					if (ac->log_level > 0)
						printf("maglev_add_node error: name %s is not unique\n", data);
				}
			}
		}

		maglev_create_ht(&cn->m_maglev_hash);
		maglev_swap_entry(&cn->m_maglev_hash);
		qsort(cn->servers, servers_size, sizeof(*cn->servers), cluster_sort_compare);
	}

	json_t *jsharding_key = json_object_get(cluster, "sharding_key");
	if (jsharding_key)
	{
		uint64_t sharding_key_size = cn->sharding_key_size = json_array_size(jsharding_key);
		cn->sharding_key = calloc(1, sizeof(void*) * sharding_key_size);
		for (uint64_t i = 0, j = 0; i < sharding_key_size; i++)
		{
			json_t *sharding_key = json_array_get(jsharding_key, i);
			char *data = (char*)json_string_value(sharding_key);
			if (data)
			{
				if (!strcmp(data, "__name__"))
					cn->sharding_key[j++] = NULL;
				else
					cn->sharding_key[j++] = strdup(data);
			}
		}
	}

	json_t *type_key = json_object_get(cluster, "type");
	if (type_key)
	{
		char *data = (char*)json_string_value(type_key);
		if (!strcmp(data, "sharedlock"))
			cn->type = CLUSER_TYPE_SHAREDLOCK;
		else if (!strcmp(data, "oplog"))
			cn->type = CLUSER_TYPE_OPLOG;
	}

	cn->timeout = 5000;
	cn->timeout = config_get_intstr_json(cluster, "timeout");

	cn->replica_factor = 0;
	cn->replica_factor = config_get_intstr_json(cluster, "replica_factor");

	if (ac->log_level > 0)
		printf("create cluster node name '%s'\n", cn->name);

	alligator_ht_insert(ac->cluster, &(cn->node), cn, tommy_strhash_u32(0, cn->name));
}
