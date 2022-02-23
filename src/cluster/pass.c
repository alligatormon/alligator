#include "main.h"
#include "cluster/type.h"
#include "events/context_arg.h"
#include "metric/metrictree.h"
#include "metric/labels.h"
#include "cluster/get.h"
void cluster_replica_handler(char *data, size_t size, context_arg *carg);

void cluster_generate_labels(void *funcarg, void* arg)
{
	labels_container *labelscont = arg;
	if (!labelscont)
		return;

	string *res = funcarg;

	string_cat(res, labelscont->key, strlen(labelscont->key));
	string_cat(res, labelscont->key, strlen(labelscont->key));
}

void alligator_labels_serialize_to_replication(void *funcarg, void *arg)
{
	labels_container *labelscont = arg;
	string *str = funcarg;

	if (labelscont && labelscont->name && labelscont->key)
	{
		string_cat(str, labelscont->name, strlen(labelscont->name));
		string_cat(str, "=", 1);
		string_cat(str, labelscont->key, strlen(labelscont->key));
		string_cat(str, "\t", 1);
	}
}

uint8_t cluster_pass(context_arg *carg, char *name, alligator_ht *hash, void* value, int8_t type)
{
	if (!carg)
		return 0;

	if (!carg->cluster)
		return 0;

	cluster_node* cn = get_cluster_node_from_carg(carg);

	string *hash_str = string_new();

	if (cn->sharding_key)
	{
		uint64_t sharding_key_size = cn->sharding_key_size;
		for (uint64_t i = 0; i < sharding_key_size; i++)
		{
			labels_container *labelscont = NULL;
			if (!cn->sharding_key[i]) // alias for __name__: NULL
				string_cat(hash_str, name, strlen(name));
			else if ((labelscont = alligator_ht_search(hash, labels_hash_compare, cn->sharding_key[i], tommy_strhash_u32(0, cn->sharding_key[i]))))
				string_cat(hash_str, labelscont->key, strlen(labelscont->key));
		}
	}
	else 
		string_cat(hash_str, name, strlen(name));

	cluster_server_oplog *srvoplog = maglev_lookup_node(&cn->m_maglev_hash, hash_str->s, hash_str->l);

	if (carg->log_level > 0)
		printf("metric name '%s', hash '%s', rf %"PRIu64"\n", name, hash_str->s, cn->replica_factor);

	string_free(hash_str);

	uint8_t retcode = 1;
	uint64_t i = 0;
	if (srvoplog->is_me)
	{
		retcode = 0;
		i = 1;
	}
	string *oplog = string_new();
	for (; i < cn->replica_factor; i++)
	{
		string_null(oplog);
		uint64_t current_index = (srvoplog->index + i) % cn->servers_size;
		if (i) // replica
		{
			string_cat(oplog, "replica", 7);
			string_cat(oplog, "\t", 1);
			string_cat(oplog, cn->servers[current_index].name, strlen(cn->servers[current_index].name));
			string_cat(oplog, "\t", 1);
		}
		else
		{
			string_cat(oplog, "primary", 7);
			string_cat(oplog, "\t", 1);
		}

		string_cat(oplog, name, strlen(name));
		string_cat(oplog, "\t", 1);
		alligator_ht_foreach_arg(hash, alligator_labels_serialize_to_replication, oplog);
		string_cat(oplog, "\t", 1);
		string_number(oplog, value, type);
		string_cat(oplog, "\n", 1);
		oplog_record_insert(cn->servers[(srvoplog->index + i) % cn->servers_size].oprec, oplog);
	}
	string_free(oplog);

	return retcode;
}
