#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "main.h"
#define SQUID_LEN 1000

void squid_counters_handler(char *metrics, size_t size, context_arg *carg)
{
	//printf("\n==============\n'%s'\n=============\n", metrics);
	tommy_hashdyn *lbl = NULL;
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
					lbl = malloc(sizeof(*lbl));
					tommy_hashdyn_init(lbl);
				}
				if (carg->log_level > 2)
					printf("'(%"u64"+%"u64"/%"u64")type:%s', ", j, dotlen, len, label_key);
				labels_hash_insert_nocache(lbl, "type", label_key);
			}
			else if (strstr(label_key, "ftp") || strstr(label_key, "http") || strstr(label_key, "all"))
			{
				if (!lbl)
				{
					lbl = malloc(sizeof(*lbl));
					tommy_hashdyn_init(lbl);
				}
				if (carg->log_level > 2)
					printf("'(%"u64"+%"u64"/%"u64")proto:%s', ", j, dotlen, len, label_key);
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
		if (carg->log_level > 2)
		{
			printf(" name:'%s', ", name);
			printf(":");
		}

		i += strspn(metrics+i, " =");

		len = strcspn(metrics+i, "\n");
		strlcpy(value, metrics+i, len+1);

		if (strstr(value, "."))
		{
			double vl = strtod(value, NULL);
			if (carg->log_level > 2)
				printf(" '%s' -> (double) '%lf'\n", value, vl);
			metric_add(name, lbl, &vl, DATATYPE_DOUBLE, carg);
		}
		else
		{
			uint64_t vl = strtoull(value, NULL, 10);
			if (carg->log_level > 2)
				printf(" '%s' -> (uint64_t) '%"u64"'\n", value, vl);
			metric_add(name, lbl, &vl, DATATYPE_UINT, carg);
		}
		i += len;
		lbl = NULL;
	}
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
}

void squid_active_requests_handler(char *metrics, size_t size, context_arg *carg)
{
	//puts("squid_active_requests_handler");
	//puts(metrics);
}

void squid_pconn_handler(char *metrics, size_t size, context_arg *carg)
{
	//puts("squid_pconn_handler");
	//puts(metrics);
}

void squid_mem_handler(char *metrics, size_t size, context_arg *carg)
{
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
		printf("pool '%s'\n", pool);
		obj_sz += strspn(tmp+i+obj_sz, "\t");

		if (!strncmp(pool, "Cumulative allocated volume", 27))
			break;

		for (k = 0, j = obj_sz; j < str_sz; j++, k++)
		{
			obj_sz = strcspn(tmp+i+j, "\t\n");
			strlcpy(obj, tmp+i+j, obj_sz+1);
			printf("\tpool '%s', object: '%s'[%zu]:'%s'\n", pool, objname[k], k, obj);
			j += obj_sz;
			j += strspn(tmp+i+j, "\t\n");
		}

		i += str_sz;
		++i;
		i += strspn(tmp+i, "\n");
	}

	tmp += i;
	puts(tmp);
}

string *squid_counters_mesg(host_aggregator_info *hi, void *arg) {
	return string_init_add(gen_http_query(0, "cache_object://localhost/counters", NULL, hi->host, "alligator", hi->auth, 1, "1.0"), 0, 0);
}

string *squid_info_mesg(host_aggregator_info *hi, void *arg) {
	return string_init_add(gen_http_query(0, "cache_object://localhost/info", NULL, hi->host, "alligator", hi->auth, 1, "1.0"), 0, 0);
}

string *squid_active_requests_mesg(host_aggregator_info *hi, void *arg) {
	return string_init_add(gen_http_query(0, "cache_object://localhost/active_requests", NULL, hi->host, "alligator", hi->auth, 1, "1.0"), 0, 0);
}

string *squid_pconn_mesg(host_aggregator_info *hi, void *arg) {
	return string_init_add(gen_http_query(0, "cache_object://localhost/pconn", NULL, hi->host, "alligator", hi->auth, 1, "1.0"), 0, 0);
}

string *squid_mem_mesg(host_aggregator_info *hi, void *arg) {
	return string_init_add(gen_http_query(0, "cache_object://localhost/mem", NULL, hi->host, "alligator", hi->auth, 1, "1.0"), 0, 0);
}

void squid_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("squid");
	actx->handlers = 5;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = squid_counters_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = squid_counters_mesg;
	strlcpy(actx->handler[0].key,"squid_counters", 255);

	actx->handler[1].name = squid_info_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = squid_info_mesg;
	strlcpy(actx->handler[1].key,"squid_info", 255);

	actx->handler[2].name = squid_active_requests_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = squid_active_requests_mesg;
	strlcpy(actx->handler[2].key,"squid_active_requests", 255);

	actx->handler[3].name = squid_pconn_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = squid_pconn_mesg;
	strlcpy(actx->handler[3].key,"squid_pconn", 255);

	actx->handler[4].name = squid_mem_handler;
	actx->handler[4].validator = NULL;
	actx->handler[4].mesg_func = squid_mem_mesg;
	strlcpy(actx->handler[4].key,"squid_mem", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

