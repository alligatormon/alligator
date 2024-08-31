#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/logs.h"
#include "dstructures/tommy.h"
#include "main.h"
#define SQUID_LEN 1000

void squid_counters_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!strstr(metrics, "aborted_requests") || !strstr(metrics, "sample_time"))
		return;

	alligator_ht *lbl = NULL;
	char name[SQUID_LEN];
	char label_key[SQUID_LEN];
	strlcpy(name, "squid_", 7);
	char value[SQUID_LEN];
	uint64_t len;
	uint64_t dotlen;
	uint64_t endlen;

	len = strspn(metrics, " \t\n\r\n");
	size -= len;
	metrics += len;

	for (uint64_t i = 0; i < size; i++)
	{
		len = strcspn(metrics+i, " =");
		dotlen = strcspn(metrics+i, ". =");

		strlcpy(name+6, metrics+i, dotlen+1);
		//size_t name_len = 6 + dotlen;
		i += dotlen;

		i += strspn(metrics+i, ".");

		//printf("\t");
		for (uint64_t j = 0; j < len; j++)
		{
			i += strspn(metrics+i, ".");

			endlen = strcspn(metrics+i, " =");
			dotlen = strcspn(metrics+i, ".");
			if (dotlen > endlen)
				dotlen = endlen;
			if (!dotlen)
			{
				break;
			}

			strlcpy(label_key, metrics+i, dotlen+1);
			if (strstr(label_key, "bytes") || strstr(label_key, "sent") || strstr(label_key, "recv") || strstr(label_key, "errors") || strstr(label_key, "requests") || strstr(label_key, "hits"))
			{
				if (!lbl)
				{
					lbl = alligator_ht_init(NULL);
				}
				carglog(carg, L_TRACE, "squid '(%"u64"+%"u64"/%"u64")type:%s', ", j, dotlen, len, label_key);
				labels_hash_insert_nocache(lbl, "type", label_key);
			}
			else if (strstr(label_key, "ftp") || strstr(label_key, "http") || strstr(label_key, "all"))
			{
				if (!lbl)
				{
					lbl = alligator_ht_init(NULL);
				}
				carglog(carg, L_TRACE, "squid '(%"u64"+%"u64"/%"u64")proto:%s', ", j, dotlen, len, label_key);
				labels_hash_insert_nocache(lbl, "proto", label_key);
			}
			else
			{
				strcat(name, "_");
				strcat(name, label_key);
				//printf("NO:'(%"u64"+%"u64"/%"u64")%s', ", j, dotlen, len, label_key);
			}
			i += dotlen;
			j += dotlen;
		}
		carglog(carg, L_TRACE, "squid name:'%s', :", name);

		i += strspn(metrics+i, " =");

		len = strcspn(metrics+i, "\n");
		strlcpy(value, metrics+i, len+1);

		if (strstr(value, "."))
		{
			double vl = strtod(value, NULL);
			carglog(carg, L_TRACE, " '%s' -> (double) '%lf'\n", value, vl);
			metric_add(name, lbl, &vl, DATATYPE_DOUBLE, carg);
		}
		else
		{
			uint64_t vl = strtoull(value, NULL, 10);
			carglog(carg, L_TRACE, " '%s' -> (uint64_t) '%"u64"'\n", value, vl);
			metric_add(name, lbl, &vl, DATATYPE_UINT, carg);
		}
		i += len;
		lbl = NULL;
	}
	carg->parser_status = 1;
}

char* squid_info_5min_parser(context_arg *carg, char *field, char *metric, char *cur) {
	double dval;
	char *tmp;
	tmp = strstr(cur, field);
	if (!tmp)
		return cur;
	cur = tmp;
	tmp = strstr(cur, "5min");
	if (!tmp)
		return cur;
	tmp += 5;
	tmp += strcspn(tmp, "0123456789");
	dval = strtod(tmp, &tmp);
	metric_add_auto(metric, &dval, DATATYPE_DOUBLE, carg);
	return tmp;
}

char* squid_info_counter_parser(context_arg *carg, char *field, char *metric, char *cur, int type) {
	char *tmp;

	tmp = strstr(cur, field);
	if (!tmp)
		return cur;
	tmp += strcspn(tmp, "0123456789");

	if (type == DATATYPE_UINT)
	{
		uint64_t val = strtoull(tmp, &tmp, 10);
		metric_add_auto(metric, &val, type, carg);
	}
	else if (type == DATATYPE_INT)
	{
		int64_t val = strtoll(tmp, &tmp, 10);
		metric_add_auto(metric, &val, type, carg);
	}
	else if (type == DATATYPE_DOUBLE)
	{
		double val = strtod(tmp, &tmp);
		metric_add_auto(metric, &val, type, carg);
	}

	return tmp;
}

void squid_info_handler(char *metrics, size_t size, context_arg *carg)
{
	char *cur = metrics;

	cur = squid_info_counter_parser(carg, "Number of clients accessing cache", "squid_client_access_cache", cur, DATATYPE_UINT);
	cur = squid_info_5min_parser(carg, "Hits as \% of all requests", "squid_cache_hit_requests_percent", cur);
	cur = squid_info_5min_parser(carg, "Hits as \% of bytes sent", "squid_cache_hit_bytes_percent", cur);
	cur = squid_info_5min_parser(carg, "Memory hits as \% of hit requests", "squid_memory_hit_requests_percent", cur);
	cur = squid_info_5min_parser(carg, "Disk hits as \% of hit requests", "squid_disk_hit_requests_percent", cur);
	cur = squid_info_counter_parser(carg, "Storage Swap size", "squid_swap_size_kbytes", cur, DATATYPE_UINT);
	cur = squid_info_counter_parser(carg, "Storage Swap capacity", "squid_swap_capacity_used", cur, DATATYPE_DOUBLE);
	cur = squid_info_counter_parser(carg, "Storage Mem size", "squid_mem_size_kbytes", cur, DATATYPE_UINT);
	cur = squid_info_counter_parser(carg, "Storage Mem capacity", "squid_mem_capacity_used", cur, DATATYPE_DOUBLE);
	cur = squid_info_counter_parser(carg, "Mean Object Size", "squid_cache_mean_object_size_kbytes", cur, DATATYPE_DOUBLE);
	cur = squid_info_counter_parser(carg, "Total accounted", "squid_memory_accounted_kbytes", cur, DATATYPE_UINT);
	cur = squid_info_counter_parser(carg, "memPoolAlloc calls", "squid_memory_pool_alloc_calls", cur, DATATYPE_UINT);
	cur = squid_info_counter_parser(carg, "memPoolFree calls", "squid_memory_pool_free_calls", cur, DATATYPE_UINT);
	cur = squid_info_counter_parser(carg, "Number of file desc currently in use", "squid_file_descriptors_used", cur, DATATYPE_UINT);
	carg->parser_status = 1;
}

//void squid_active_requests_handler(char *metrics, size_t size, context_arg *carg)
//{
//	puts("squid_active_requests_handler");
//	puts(metrics);
//}

typedef struct squid_pconn_reqconn {
	uint64_t key;
	uint64_t requests;
	uint64_t connection_count;

	tommy_node node;
} squid_pconn_reqconn;

void squid_hash_tree(void *funcarg, void* arg)
{
	free(arg);
}

int squid_pconn_compare(const void* arg, const void* obj)
{
        uint64_t s1 = *(uint64_t*)arg;
        uint64_t s2 = ((squid_pconn_reqconn*)obj)->key;
        return s1 != s2;
}

void squid_pconn_handler(char *metrics, size_t read_size, context_arg *carg)
{
	uint64_t pos = 0;
	uint64_t requests_pos = 0;
	char pool[255];
	uint64_t copy_size;

	char *debug_req;
	char *debug_table;

	uint64_t hash_table_offset = 0;
	alligator_ht *hash = calloc(1, sizeof(*hash));

	//printf("pos %"PRIu64" / read_size %zu\n", pos, read_size);
	for (pos = 0; pos < read_size; pos++)
	{
		char *tmp = strstr(metrics+pos, "\nby");
		//printf("tmp %p %"PRIu64"\n", tmp, pos);
		if (!tmp)
			break;

		tmp += 3;
		tmp += strspn(tmp, " \t\r\n");
		copy_size = strcspn(tmp, " \t\r\n");
		strlcpy(pool, tmp, copy_size+1);

		//printf("kid: %s/%"PRIu64": %p\n", pool, copy_size, tmp);

		tmp = strstr(tmp, "Connection Count");
		if (!tmp)
			break;

		tmp += 16;
		tmp += strspn(tmp, " -\t\r\n");

		char *hashtable = strstr(tmp, "\n\n");
		hash_table_offset = (hashtable - tmp);
		if (carg->log_level > 1)
		{
			debug_req = strndup(tmp, hash_table_offset);
			carglog(carg, L_TRACE, "==== squid debug req ===\n'%s'\n", debug_req);
			free(debug_req);
		}

		char* tmp2 = tmp;
		tmp2 += strcspn(tmp2, "\n");
		tmp2 += strspn(tmp2, "\n");
		uint64_t requests;
		uint64_t connection_count;
		alligator_ht_init(hash);
		uint64_t i;
		for (i = 0, requests_pos = 0; requests_pos < hash_table_offset; requests_pos++, i++)
		{
			tmp2 += strcspn(tmp2, " \t\r\n");
			tmp2 += strspn(tmp2, " \t\r\n");
			requests = strtoull(tmp2, &tmp2, 10);
			carglog(carg, L_TRACE, "\tsquid pool '%s'[%"PRIu64"] (%"PRIu64"/%"PRIu64"), requests %"PRIu64"\n", pool, i, requests_pos, hash_table_offset, requests);

			tmp2 += strcspn(tmp2, " \t\r\n");
			tmp2 += strspn(tmp2, " \t\r\n");
			connection_count = strtoull(tmp2, &tmp2, 10);
			carglog(carg, L_TRACE, "\tsquid pool '%s'[%"PRIu64"], connection_count %"PRIu64"\n", pool, i, connection_count);

			squid_pconn_reqconn *pconn = malloc(sizeof(*pconn));
			pconn->requests = requests;
			pconn->connection_count = connection_count;
			pconn->key = i;
			alligator_ht_insert(hash, &(pconn->node), pconn, pconn->key);

			requests_pos = (tmp2 - tmp);
		}

		tmp = hashtable + 10;
		char *end = strstr(tmp, "} by kid");
		hash_table_offset = (end - tmp);
		if (carg->log_level > 1)
		{
			debug_table = strndup(tmp, hash_table_offset);
			carglog(carg, L_TRACE, "==== squid debug table ===\n'%s'\n", debug_table);
			free(debug_table);
		}

		tmp2 = tmp;
		char endpoint[255];
		char hostname[255];

		for (requests_pos = 0; requests_pos < hash_table_offset; requests_pos++)
		{
			tmp2 = strstr(tmp2, "item");
			if (!tmp2)
				break;
			if ((tmp2 - tmp) >= hash_table_offset)
				break;

			tmp2 += 4;
			tmp2 += strcspn(tmp2, " \t");
			tmp2 += strspn(tmp2, " \t");
			uint64_t key = strtoull(tmp2, &tmp2, 10);

			tmp2 += strcspn(tmp2, " \t:");
			tmp2 += strspn(tmp2, " \t:");

			uint64_t sep = strcspn(tmp2, "/");
			strlcpy(endpoint, tmp2, sep+1);
			sep += strspn(tmp2+sep, "/");

			tmp2 += sep;
			sep = strcspn(tmp2, "\r\n \t");
			strlcpy(hostname, tmp2, sep+1);
			sep = strspn(tmp2+sep, "\r\n \t");

			squid_pconn_reqconn *pconn = alligator_ht_search(hash, squid_pconn_compare, &key, key);
			if (pconn)
			{
				carglog(carg, L_TRACE, "\tsquid KEY %"PRIu64", endpoint='%s', hostname='%s', pconn: %p, requests: %"PRIu64", connection_count: %"PRIu64"\n", key, endpoint, hostname, pconn, pconn->requests, pconn->connection_count);
				metric_add_labels3("squid_persistence_connections_requests_count", &pconn->requests, DATATYPE_UINT, carg, "endpoint", endpoint, "hostname", hostname, "pool", pool);
				metric_add_labels3("squid_persistence_connections_connect_count", &pconn->connection_count, DATATYPE_UINT, carg, "endpoint", endpoint, "hostname", hostname, "pool", pool);
			}
			else
			{
				carglog(carg, L_TRACE, "\tsquid KEY %"PRIu64", endpoint='%s', hostname='%s', pconn: %p", key, endpoint, hostname, pconn);
			}

			tmp2 += sep;
			requests_pos = tmp2 - tmp;
		}

		alligator_ht_foreach_arg(hash, squid_hash_tree, NULL);
		alligator_ht_done(hash);
		pos = (tmp2 - metrics);
	}
	free(hash);
	carg->parser_status = 1;
}

void squid_mem_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!strstr(metrics, "Current memory usage"))
		return;

	char *tmp = metrics;

	// skip string
	tmp += strcspn(tmp, "\n");
	tmp += strspn(tmp, "\n");
	
	// skip string
	tmp += strcspn(tmp, "\n");
	tmp += strspn(tmp, "\n");
	
	// skip string
	tmp += strcspn(tmp, "\n");
	tmp += strspn(tmp, "\n");

	uint64_t i;
	uint64_t j;
	uint64_t k;
	size_t sz = size - (tmp - metrics);
	size_t str_sz;
	size_t obj_sz;
	char obj[255];
	char pool[255];

	char *objname[] = { "object_size_bytes", "kilobytes_per_chunk", "objects_per_chunk", "", "chunk_used", "chunk_free", "chunk_part", "fragmentation_percent", "", "memory_allocated_kb", "memory_in_use_high_kb", "memory_in_use_high_hrs", "memory_in_use_total_percent", "memory_in_use_number", "memory_in_use_kb", "memory_idle_high_kb", "memory_idle_high_hrs", "memory_idle_allocate_percent", "allocations_saved_number", "allocations_saved_kb", "allocations_saved_high_kb", "rate_number", "rate_number_count_percent", "rate_number_vol_percent", "rate_number_per_second" };

	for (i = 0; i < sz;)
	{
		str_sz = strcspn(tmp+i, "\n");
		obj_sz = strcspn(tmp+i, "\t");
		strlcpy(pool, tmp+i, obj_sz+1);

		carglog(carg, L_TRACE, "squid pool '%s'\n", pool);

		obj_sz += strspn(tmp+i+obj_sz, "\t");

		if (!strncmp(pool, "Cumulative allocated volume", 27))
			break;

		for (k = 0, j = obj_sz; j < str_sz; j++, k++)
		{
			obj_sz = strcspn(tmp+i+j, "\t\n");
			strlcpy(obj, tmp+i+j, obj_sz+1);
			carglog(carg, L_TRACE, "\tsquid pool '%s', object: '%s'[%zu]:'%s'\n", pool, objname[k], k, obj);
			j += obj_sz;
			j += strspn(tmp+i+j, "\t\n");
		}

		i += str_sz;
		++i;
		i += strspn(tmp+i, "\n");
	}

	tmp += i;
	//puts(tmp);
	carg->parser_status = 1;
}

void squid_comm_epoll_incoming_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp = metrics;
	char pool_str[255];
	uint64_t loops_uint;
	for (; (tmp - metrics) < size;)
	{
		char *loops = strstr(tmp, "Total number of epoll(2) loops:");
		if (!loops)
			break;
		loops += 30;
		loops += strcspn(loops, " \t");
		loops += strspn(loops, " \t");
		loops_uint = strtoull(loops, NULL, 10);

		char *pool = strstr(tmp, "by");
		if (pool)
		{
			pool += 2;
			pool += strcspn(pool, " \t");
			pool += strspn(pool, " \t");
			size_t pool_size = strcspn(pool, " \t\n\r");
			strlcpy(pool_str, pool, pool_size+1);
			carglog(carg, L_TRACE, "\tsquid_comm_epoll_incoming_loops '%s', loop %"u64"\n", pool_str, loops_uint);
			metric_add_labels("squid_comm_epoll_incoming_loops", &loops_uint, DATATYPE_UINT, carg, "pool", pool_str);
		}
		else
		{
			carglog(carg, L_TRACE, "squid_comm_epoll_incoming_loops %"u64"\n", loops_uint);
			metric_add_auto("squid_comm_epoll_incoming_loops", &loops_uint, DATATYPE_UINT, carg);
		}

		tmp = loops;
	}
	carg->parser_status = 1;
}

void squid_forward_handler(char *metrics, size_t size, context_arg *carg)
{
	char pool[255];
	size_t pool_size;

	char *tmp = strstr(metrics, "by");
	if (!tmp)
	{
		strlcpy(pool, "kid1", 5);
		tmp = metrics;
	}
	else
	{
		tmp += strcspn(tmp, " \t\r\n");
		tmp += strspn(tmp, " \t\r\n");
	}

	uint64_t i = 0;
	uint64_t j = 0;
	uint64_t k = 0;
	uint64_t counter = 0;
	uint64_t ind = 0;
	uint64_t size2;
	uint64_t size3;
	char *tmp2;
	for (i = 0; i < size; i++)
	{
		pool_size = strcspn(tmp, " \r\n\t");
		strlcpy(pool, tmp, pool_size+1);
		carglog(carg, L_TRACE, "squid pool '%s'\n", pool);

		tmp = strstr(tmp, "Status");
		if (!tmp)
			return;
		tmp += strcspn(tmp, "\n");
		tmp += strspn(tmp, "\n");

		tmp2 = strstr(tmp, "\n\nby");
		size2 = tmp2 - tmp;
		tmp2 = tmp;
		for (j = 0; j < size2; j++)
		{
			uint64_t code = strtoull(tmp2, &tmp2, 10);
			if (!code)
				break;

			carglog(carg, L_TRACE, "\tsquid code: %"u64" (%"u64"/%"u64")\n", code, j, size2);

			size3 = strcspn(tmp2, "\n");

			k = strcspn(tmp2, " \t");
			k = strspn(tmp2+k, " \t");

			for (ind = 0; k < size3; ind++)
			{
				counter = strtoull(tmp2+k, NULL, 10);
				k += strcspn(tmp2+k, " \t");
				k += strspn(tmp2+k, " \t");

				char try[21];
				char scode[21];
				snprintf(try, 21, "%"u64, ind);
				snprintf(scode, 21, "%"u64, code);
				carglog(carg, L_TRACE, "\t\tsquid counter[%"u64"]: %"u64"\n", ind, counter);
				metric_add_labels3("squid_forward_code", &counter, DATATYPE_UINT, carg, "pool", pool, "try", try, "code", scode);
			}

			tmp2 += size3;
			j = tmp2 - tmp;
		}

		tmp = strstr(tmp, "\n\nby");
		if (!tmp)
			return;
		tmp += 4;
		tmp += strspn(tmp, " \t\r\n");

		i = tmp - metrics;
	}
	carg->parser_status = 1;
}

void squid_fqdncache_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp = metrics;
	uint64_t tmp_size;
	char pool[255];

	uint64_t inuse, cached, requests, hits, nhits, misses;

	for (uint64_t i = 0; i < size; i++)
	{
		tmp = strstr(tmp, "by");
		if (!tmp)
			break;

		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		tmp_size = strcspn(tmp, " \t\r\n{");
		strlcpy(pool, tmp, tmp_size+1);

		tmp = strstr(tmp, "FQDNcache Entries In Use:");
		if (!tmp)
			break;
		tmp += 25;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		inuse = strtoull(tmp, &tmp, 10);

		tmp = strstr(tmp, "FQDNcache Entries Cached:");
		if (!tmp)
			break;
		tmp += 25;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		cached = strtoull(tmp, &tmp, 10);

		tmp = strstr(tmp, "FQDNcache Requests:");
		if (!tmp)
			break;
		tmp += 19;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		requests = strtoull(tmp, &tmp, 10);

		tmp = strstr(tmp, "FQDNcache Hits:");
		if (!tmp)
			break;
		tmp += 15;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		hits = strtoull(tmp, &tmp, 10);

		tmp = strstr(tmp, "FQDNcache Negative Hits:");
		if (!tmp)
			break;
		tmp += 24;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		nhits = strtoull(tmp, &tmp, 10);

		tmp = strstr(tmp, "FQDNcache Misses:");
		if (!tmp)
			break;
		tmp += 17;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		misses = strtoull(tmp, &tmp, 10);

		carglog(carg, L_TRACE, "squid pool %s, in use: %"u64", cached: %"u64", requests: %"u64", hits: %"u64", negative hits: %"u64", misses: %"u64"\n", pool, inuse, cached, requests, hits, nhits, misses);

		metric_add_labels("squid_fqdn_cache_entries_in_use", &inuse, DATATYPE_UINT, carg, "pool", pool);
		metric_add_labels("squid_fqdn_cache_entries_cached", &cached, DATATYPE_UINT, carg, "pool", pool);
		metric_add_labels("squid_fqdn_cache_requests", &requests, DATATYPE_UINT, carg, "pool", pool);
		metric_add_labels("squid_fqdn_hits", &hits, DATATYPE_UINT, carg, "pool", pool);
		metric_add_labels("squid_fqdn_negative_hits", &nhits, DATATYPE_UINT, carg, "pool", pool);
		metric_add_labels("squid_fqdn_misses", &misses, DATATYPE_UINT, carg, "pool", pool);

		tmp = strstr(tmp, "by");
		if (!tmp)
			break;

		++tmp;

		i = tmp - metrics;
	}
	carg->parser_status = 1;
}

void squid_diskd_stats(char *str, char **ret, char *stat, context_arg *carg)
{
	*ret = str;
	char *tmp = strstr(str, stat);
	if (!tmp)
		return;

	char metric_name[255];
	snprintf(metric_name, 255, "squid_disk_%s", stat);

	tmp += strcspn(tmp, " \t");
	tmp += strspn(tmp, " \t");
	
	uint64_t cnt = strtoull(tmp, &tmp, 10);
	metric_add_labels(metric_name, &cnt, DATATYPE_UINT, carg, "type", "ops");

	cnt = strtoull(tmp, &tmp, 10);
	metric_add_labels(metric_name, &cnt, DATATYPE_UINT, carg, "type", "success");

	cnt = strtoull(tmp, &tmp, 10);
	metric_add_labels(metric_name, &cnt, DATATYPE_UINT, carg, "type", "fail");

	*ret = str;
}

void squid_diskd_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t cnt;
	char *tmp;

	tmp = strstr(metrics, "sent_count");
	if (tmp)
	{
		cnt = strtoull(tmp, &tmp, 10);
		metric_add_auto("squid_disk_sent_count", &cnt, DATATYPE_UINT, carg);
	}
	else
		return;

	tmp = strstr(tmp, "recv_count");
	if (tmp)
	{
		cnt = strtoull(tmp, &tmp, 10);
		metric_add_auto("squid_disk_recv_count", &cnt, DATATYPE_UINT, carg);
	}
	else
		return;

	tmp = strstr(tmp, "max_away");
	if (tmp)
	{
		cnt = strtoull(tmp, &tmp, 10);
		metric_add_auto("squid_disk_max_away", &cnt, DATATYPE_UINT, carg);
	}
	else
		return;

	tmp = strstr(tmp, "max_shmuse");
	if (tmp)
	{
		cnt = strtoull(tmp, &tmp, 10);
		metric_add_auto("squid_disk_max_shmuse", &cnt, DATATYPE_UINT, carg);
	}
	else
		return;

	tmp = strstr(tmp, "open_fail_queue_len");
	if (tmp)
	{
		cnt = strtoull(tmp, &tmp, 10);
		metric_add_auto("squid_disk_open_fail_queue_len", &cnt, DATATYPE_UINT, carg);
	}
	else
		return;

	tmp = strstr(tmp, "block_queue_len");
	if (tmp)
	{
		cnt = strtoull(tmp, &tmp, 10);
		metric_add_auto("squid_disk_block_queue_len", &cnt, DATATYPE_UINT, carg);
	}
	else
		return;

	squid_diskd_stats(tmp, &tmp, "open", carg);
	squid_diskd_stats(tmp, &tmp, "create", carg);
	squid_diskd_stats(tmp, &tmp, "close", carg);
	squid_diskd_stats(tmp, &tmp, "unlink", carg);
	squid_diskd_stats(tmp, &tmp, "read", carg);
	squid_diskd_stats(tmp, &tmp, "write", carg);
	carg->parser_status = 1;
}

string *squid_counters_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) {
	return string_init_add_auto(gen_http_query(0, "cache_object://localhost/counters", NULL, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

string *squid_info_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) {
	return string_init_add_auto(gen_http_query(0, "cache_object://localhost/info", NULL, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

//string *squid_active_requests_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) {
//	return string_init_add_auto(gen_http_query(0, "cache_object://localhost/active_requests", NULL, hi->host, "alligator", hi->auth, "1.0", NULL));
//}

string *squid_pconn_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) {
	return string_init_add_auto(gen_http_query(0, "cache_object://localhost/pconn", NULL, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

string *squid_mem_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) {
	return string_init_add_auto(gen_http_query(0, "cache_object://localhost/mem", NULL, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

string *squid_comm_epoll_incoming_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) {
	return string_init_add_auto(gen_http_query(0, "cache_object://localhost/comm_epoll_incoming", NULL, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

string *squid_forward_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) {
	return string_init_add_auto(gen_http_query(0, "cache_object://localhost/forward", NULL, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

string *squid_fqdncache_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) {
	return string_init_add_auto(gen_http_query(0, "cache_object://localhost/fqdncache", NULL, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

string *squid_diskd_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) {
	return string_init_add_auto(gen_http_query(0, "cache_object://localhost/diskd", NULL, hi->host, "alligator", hi->auth, "1.0", env, proxy_settings, NULL));
}

void squid_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("squid");
	actx->handlers = 8;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = squid_counters_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = squid_counters_mesg;
	strlcpy(actx->handler[0].key,"squid_counters", 255);

	actx->handler[1].name = squid_info_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = squid_info_mesg;
	strlcpy(actx->handler[1].key,"squid_info", 255);

	actx->handler[2].name = squid_pconn_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = squid_pconn_mesg;
	strlcpy(actx->handler[2].key,"squid_pconn", 255);

	actx->handler[3].name = squid_mem_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = squid_mem_mesg;
	strlcpy(actx->handler[3].key,"squid_mem", 255);

	actx->handler[4].name = squid_comm_epoll_incoming_handler;
	actx->handler[4].validator = NULL;
	actx->handler[4].mesg_func = squid_comm_epoll_incoming_mesg;
	strlcpy(actx->handler[4].key,"squid_comm_epoll_incoming", 255);

	actx->handler[5].name = squid_forward_handler;
	actx->handler[5].validator = NULL;
	actx->handler[5].mesg_func = squid_forward_mesg;
	strlcpy(actx->handler[5].key,"squid_forward", 255);

	actx->handler[6].name = squid_fqdncache_handler;
	actx->handler[6].validator = NULL;
	actx->handler[6].mesg_func = squid_fqdncache_mesg;
	strlcpy(actx->handler[6].key,"squid_fqdncache", 255);

	actx->handler[7].name = squid_diskd_handler;
	actx->handler[7].validator = NULL;
	actx->handler[7].mesg_func = squid_diskd_mesg;
	strlcpy(actx->handler[7].key,"squid_diskd", 255);

	//actx->handler[2].name = squid_active_requests_handler;
	//actx->handler[2].validator = NULL;
	//actx->handler[2].mesg_func = squid_active_requests_mesg;
	//strlcpy(actx->handler[2].key,"squid_active_requests", 255);


	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

