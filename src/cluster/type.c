#include "cluster/type.h"
#include "common/selector.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "metric/metrictree.h"
#include "metric/labels.h"
#include "metric/filter.h"
#include "main.h"
#include <limits.h>

int cluster_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((cluster_node*)obj)->name;
	return strcmp(s1, s2);
}

oplog_record *oplog_record_init(uint64_t size)
{
	oplog_record *oplog;

	/* begin==end means empty; usable capacity is size-1, so size must be >= 2. */
	if (size < 2)
		size = 2;

	oplog = calloc(1, sizeof(*oplog));
	if (!oplog)
		return NULL;
	oplog->size = size;
	oplog->container = calloc(size, sizeof(oplog_node));
	if (!oplog->container)
	{
		free(oplog);
		return NULL;
	}
	pthread_rwlock_init(&oplog->rwlock, NULL);

	return oplog;
}

void oplog_record_free(oplog_record *oplog)
{
	if (!oplog)
		return;

	pthread_rwlock_wrlock(&oplog->rwlock);
	for (uint64_t i = 0; i < oplog->size; i++)
	{
		string *data = oplog->container[i].data;
		if (data)
			string_free(data);
	}
	free(oplog->container);
	oplog->container = NULL;
	pthread_rwlock_unlock(&oplog->rwlock);
	pthread_rwlock_destroy(&oplog->rwlock);
	free(oplog);
}

static void cluster_oplog_counter_set(const char *name, uint64_t value, const char *cluster, const char *peer)
{
	namespace_struct *ns;
	alligator_ht *hash;
	labels_t *labels_list;
	int64_t ttl = INT64_MAX - UINT32_MAX;

	if (!name || !cluster || !peer)
		return;

	ns = get_namespace_by_carg(NULL);
	if (!ns || !ns->metrictree || !ns->expiretree)
		return;

	hash = alligator_ht_init(NULL);
	labels_hash_insert(hash, "cluster", (char *)cluster);
	labels_hash_insert(hash, "peer", (char *)peer);
	if (!metric_filter_allow(NULL, name, hash))
	{
		labels_hash_free(hash);
		return;
	}

	labels_list = labels_initiate(ns, hash, (char *)name, 0, 0, 0);
	if (!labels_list) {
		/* OOM before initiate consumes hash (no_del=0). */
		labels_hash_free(hash);
		return;
	}
	/* add=0: publish absolute atomic totals (Prometheus counter scrape value). */
	metric_upsert(ns->metrictree, labels_list, DATATYPE_UINT, &value, ns->expiretree, ttl, 0);
}

static void cluster_oplog_stats_flush_one(void *funcarg, void *arg)
{
	cluster_node *cn = arg;
	uint64_t i;

	(void)funcarg;
	if (!cn || !cn->name || !cn->servers)
		return;

	for (i = 0; i < cn->servers_size; i++)
	{
		cluster_server_oplog *peer = &cn->servers[i];
		uint64_t enqueued;
		uint64_t overflow;
		uint64_t applied;

		if (!peer->name || peer->is_me)
			continue;

		enqueued = __atomic_load_n(&peer->stat_enqueued, __ATOMIC_RELAXED);
		overflow = __atomic_load_n(&peer->stat_overflow, __ATOMIC_RELAXED);
		applied = __atomic_load_n(&peer->stat_applied, __ATOMIC_RELAXED);

		cluster_oplog_counter_set("alligator_cluster_oplog_enqueued_total", enqueued, cn->name, peer->name);
		cluster_oplog_counter_set("alligator_cluster_oplog_overflow_total", overflow, cn->name, peer->name);
		cluster_oplog_counter_set("alligator_cluster_oplog_applied_total", applied, cn->name, peer->name);
	}
}

void cluster_oplog_stats_flush(void)
{
	if (!ac || !ac->cluster)
		return;
	alligator_ht_foreach_arg(ac->cluster, cluster_oplog_stats_flush_one, NULL);
}

int oplog_record_insert(oplog_record *oplog, string *data)
{
	int overflow = 0;

	if (!oplog || !data || !oplog->size || !oplog->container)
		return -1;

	pthread_rwlock_wrlock(&oplog->rwlock);
	oplog_node *opnode = &oplog->container[oplog->end];
	if (!opnode->data)
	{
		opnode->data = string_new();
		if (!opnode->data)
		{
			pthread_rwlock_unlock(&oplog->rwlock);
			return -1;
		}
	}

	string_string_copy(opnode->data, data);

	/* move end */
	oplog->end = (oplog->end + 1) % oplog->size;

	/* ring full: drop exactly one oldest entry */
	if (oplog->end == oplog->begin) {
		oplog->begin = (oplog->begin + 1) % oplog->size;
		overflow = 1;
	}
	pthread_rwlock_unlock(&oplog->rwlock);

	return overflow;
}

/* Cap one /oplog response so the peer's uv loop is not stuck applying a full
 * ring (size can be 1e5 entries / tens of MB) in a single cluster_sync_handler. */
#ifndef OPLOG_SYNC_MAX_BYTES
#define OPLOG_SYNC_MAX_BYTES (1024u * 1024u)
#endif

string* oplog_record_get_string(oplog_record *oplog)
{
	string *ret;

	if (!oplog)
		return NULL;

	pthread_rwlock_rdlock(&oplog->rwlock);
	ret = string_new();
	if (!ret)
	{
		pthread_rwlock_unlock(&oplog->rwlock);
		return NULL;
	}

	for (uint64_t i = 0; i < oplog->size; i++)
	{
		uint64_t index = (i + oplog->begin) % oplog->size;
		string *entry;
		if (index == oplog->end)
			break;

		entry = oplog->container[index].data;
		if (!entry || !entry->l)
			continue;
		if (ret->l + entry->l > OPLOG_SYNC_MAX_BYTES) {
			/* Do not ship an oversized first record via GET; leave it for shift. */
			if (!ret->l)
				glog(L_WARN, "oplog get: head entry %zu bytes exceeds sync budget %u, stop\n",
					entry->l, OPLOG_SYNC_MAX_BYTES);
			break;
		}
		string_string_cat(ret, entry);
	}
	pthread_rwlock_unlock(&oplog->rwlock);

	return ret;
}

string* oplog_record_shift_string(oplog_record *oplog)
{
	string *ret;
	uint64_t new_begin;

	if (!oplog)
		return NULL;

	pthread_rwlock_wrlock(&oplog->rwlock);
	ret = string_new();
	if (!ret)
	{
		pthread_rwlock_unlock(&oplog->rwlock);
		return NULL;
	}
	new_begin = oplog->begin;

	for (uint64_t i = 0; i < oplog->size; i++)
	{
		uint64_t index = (i + oplog->begin) % oplog->size;
		string *entry;
		if (index == oplog->end)
			break;

		entry = oplog->container[index].data;
		if (entry && entry->l) {
			if (ret->l + entry->l > OPLOG_SYNC_MAX_BYTES) {
				if (ret->l) {
					/* Keep this entry for the next sync. */
					break;
				}
				/*
				 * Single entry exceeds budget: still shift it alone so the ring
				 * cannot stall forever on one poison record.
				 */
				glog(L_WARN, "oplog shift: head entry %zu bytes exceeds sync budget %u, shipping alone\n",
					entry->l, OPLOG_SYNC_MAX_BYTES);
				string_string_cat(ret, entry);
				new_begin = (index + 1) % oplog->size;
				break;
			}
			string_string_cat(ret, entry);
		}
		new_begin = (index + 1) % oplog->size;
	}

	oplog->begin = new_begin;
	pthread_rwlock_unlock(&oplog->rwlock);

	return ret;
}

string* maglev_key_make(cluster_node *cn, char* name, alligator_ht *lbl) {
	string *hash_str = string_new();

	if (cn->sharding_key)
	{
		uint64_t sharding_key_size = cn->sharding_key_size;
		for (uint64_t i = 0; i < sharding_key_size; i++)
		{
			labels_container *labelscont = NULL;
			if (!cn->sharding_key[i]) // alias for __name__: NULL
				string_cat(hash_str, name, strlen(name));
			else if ((labelscont = alligator_ht_search(lbl, labels_hash_compare, cn->sharding_key[i], tommy_strhash_u32(0, cn->sharding_key[i]))))
				string_cat(hash_str, labelscont->key, strlen(labelscont->key));
		}
	}
	else
		string_cat(hash_str, name, strlen(name));

	return hash_str;
}
