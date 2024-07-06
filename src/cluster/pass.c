#include "main.h"
#include "cluster/type.h"
#include "events/context_arg.h"
#include "common/selector.h"
#include "metric/metrictree.h"
#include "metric/labels.h"
#include "common/logs.h"
#include "parsers/metric_types.h"
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

uint8_t cluster_pass(context_arg *carg, char *name, alligator_ht *lbl, void* value, int8_t type, uint64_t dt_type)
{
	if (!carg)
		return 0;

	if (!carg->cluster)
		return 0;

	if (!carg->instance)
		return 0;

	cluster_node* cn = get_cluster_node_from_carg(carg);
	if (!cn)
	{
		carglog(carg, L_ERROR, "No cluster with name %s\n", carg->cluster);
		return 0;
	}

	if (cn->type == CLUSER_TYPE_SHAREDLOCK)
		return 0;

	string *hash_str = maglev_key_make(cn, name, lbl);
	cluster_server_oplog *srvoplog = maglev_lookup_node(&cn->m_maglev_hash, hash_str->s, hash_str->l);

	carglog(carg, L_DEBUG, "cluster_pass metric name '%s', hash '%s', rf %"PRIu64", isme: %d\n", name, hash_str->s, cn->replica_factor, srvoplog->is_me);

	string_free(hash_str);

	uint8_t retcode = 1;
	uint64_t i = 0;
	uint64_t primary_number;
	char namespacename[255];
	if (srvoplog->is_me)
	{
		primary_number = 0;
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
			string_cat(oplog, srvoplog->name, strlen(srvoplog->name));
			string_cat(oplog, "\t", 1);
			carglog(carg, L_DEBUG, "\tcluster_pass pass metric to replica:%s with master:%s\n", cn->servers[current_index].name, srvoplog->name);
		}
		else
		{
			carglog(carg, L_DEBUG, "\tcluster_pass pass metric to primary:%s\n", cn->servers[current_index].name);
			string_cat(oplog, "primary", 7);
			string_cat(oplog, "\t", 1);
		}


		if (cn->servers[current_index].is_me) {
			if (i) {
				uint16_t cur = strlcpy(namespacename, cn->name, 255);
				strlcpy(namespacename + cur, ":", 255);
				strlcpy(namespacename + cur + 1, srvoplog->name, 255);

				carg->namespace = namespacename;
			}

			carglog(carg, L_DEBUG, "\tit's ME! namespace is '%s'\n", carg->namespace);

			alligator_ht *hash = labels_dup(lbl);
			if (dt_type == METRIC_TYPE_COUNTER || dt_type == METRIC_TYPE_HISTOGRAM)
				metric_update(name, hash, value, DATATYPE_DOUBLE, carg);
			else
				metric_add(name, hash, value, DATATYPE_DOUBLE, carg);
			double dvalue = *(double*)value;
			if (carg->log_level > 0)
			carglog(carg, L_DEBUG, "\t\tcluster_pass metric add %s:%lf:%d to namespace '%s'\n", name, dvalue, type, carg->namespace);
			carg->namespace = NULL;
		}
		else {
			string_cat(oplog, name, strlen(name));
			string_cat(oplog, "\t", 1);
			alligator_ht_foreach_arg(lbl, alligator_labels_serialize_to_replication, oplog);
			string_cat(oplog, "\t", 1);
			string_number(oplog, value, type);
			string_cat(oplog, "\t", 1);
			string_number(oplog, &dt_type, DATATYPE_UINT);
			string_cat(oplog, "\n", 1);
			carglog(carg, L_DEBUG, "\t\tcluster_pass pass metric '%s' to [%zu]: %s\n", oplog->s, (srvoplog->index + i) % cn->servers_size, cn->servers[(srvoplog->index + i) % cn->servers_size].name);
			oplog_record_insert(cn->servers[(srvoplog->index + i) % cn->servers_size].oprec, oplog);
		}
	}
	string_free(oplog);

	return retcode;
}
