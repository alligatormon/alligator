#include <stdio.h>
#include <string.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "query/query.h"
#include "main.h"
#define MC_NAME_SIZE 255

void memcached_query(char *metrics, size_t size, context_arg *carg)
{
	if (carg->log_level > 0)
		puts(metrics);

	char metricname[MC_NAME_SIZE];
	char metricvalue[MC_NAME_SIZE];
	for (uint64_t i = 0; i < size; i++)
	{
		uint64_t endline = strcspn(metrics + i, "\r\n");
		uint64_t cur = i;
		cur += strcspn(metrics + cur, " \n");
		cur += strspn(metrics + cur, " \n");
		uint64_t copysize = strcspn(metrics + cur, " \n");
		if (!copysize)
		{
			if (carg->log_level > 0)
				printf("metric name size is 0, error\n");
			break;
		}
		//printf("cur = %d, copysize = %d\n", cur, copysize);

		strlcpy(metricname, metrics + cur, copysize + 1);
		if (carg->log_level > 0)
			printf("metric name is %s\n", metricname);

		metric_name_normalizer(metricname, copysize);

		i += endline;
		i += strspn(metrics + i, "\r\n");
		cur = i;

		copysize = strcspn(metrics + cur, " \n");
		if (!copysize)
		{
			if (carg->log_level > 0)
				printf("metric value size is 0, error\n");
			break;
		}
		//printf("cur = %d, copysize = %d\n", cur, copysize);
		strlcpy(metricvalue, metrics + cur, copysize + 1);
		if (carg->log_level > 0)
			printf("metric value is %s\n", metricvalue);

		double mval = strtod(metricvalue, NULL);
		metric_add_auto(metricname, &mval, DATATYPE_DOUBLE, carg);

		i += copysize;
		i += strspn(metrics + cur, "\r\n");
	}
}

void memcached_cachedump(char *metrics, size_t size, context_arg *carg)
{
	string *get_query = string_new();
	string_cat(get_query, "get", 3);
	char field[MC_NAME_SIZE];

	uint64_t copysize;
	for (uint64_t i = 0; i < size;)
	{
		uint64_t endline = strcspn(metrics + i, "\r\n");
		uint64_t cur = i;

		strlcpy(field, metrics + cur, endline + 1);
		// ITEM third_metric
		if (carg->log_level > 1)
		printf("field is '%s'\n", field);
		if (!strncmp(field, "ITEM", 4))
		{
			if (carg->log_level > 2)
				puts("is item");
			copysize = strcspn(field + 5, " \t\n");
			string_cat(get_query, " ", 1);
			string_cat(get_query, field + 5, copysize);
		}

		i += endline;
		i += strspn(metrics + i, "\r\n");
	}

	string_cat(get_query, "\r\n", 2);
	copysize = strcspn(get_query->s, "\r\n") + strlen (carg->host) + 16;
	char *key = malloc(copysize+1);
	snprintf(key, copysize, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest->sin_port), get_query->s);

	if (carg->log_level > 0)
		printf("memcached glob get query is\n'%s'\nkey '%s'\n", get_query->s, key);
	try_again(carg, get_query->s, get_query->l, memcached_query, "memcached_query", NULL, key, carg->data);
}

void memcached_stats_items(char *metrics, size_t size, context_arg *carg)
{
	string *slab_query = string_new();
	char field[MC_NAME_SIZE];

	for (uint64_t i = 0; i < size;)
	{
		uint64_t endline = strcspn(metrics + i, "\r\n");
		uint64_t cur = i;

		strlcpy(field, metrics + cur, endline + 1);
		char *number_str = NULL;
		if ((number_str = strstr(field, ":number")))
		{
			uint64_t slab_id = strtoull(field + 11, NULL, 10);
			uint64_t number = strtoull(number_str + 8, NULL, 10);
			string_cat(slab_query, "stats cachedump ", 16);
			string_uint(slab_query, slab_id);
			string_cat(slab_query, " ", 1);
			string_uint(slab_query, number);
			string_cat(slab_query, "\r\n", 2);
		}

		i += endline;
		i += strspn(metrics + i, "\r\n");
	}

	uint8_t copysize = strcspn(slab_query->s, "\r\n") + strlen (carg->host) + 16;
	char *key = malloc(copysize+1);
	snprintf(key, copysize, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest->sin_port), slab_query->s);

	if (carg->log_level > 0)
		printf("query is\n'%s'\nkey '%s'\n", slab_query->s, key);
	try_again(carg, slab_query->s, slab_query->l, memcached_cachedump, "memcached_cachedump", NULL, key, carg->data);
}

void memcached_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	query_node *qn = arg;

	if (carg->log_level > 1)
	{
		puts("+-+-+-+-+-+-+-+");
		printf("run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	}

	uint64_t writelen = 0;
	char *write_comm = NULL;
	void *func = NULL;
	char *funcname = NULL;
	if (!strncmp(qn->expr, "glob", 4))
	{
		writelen = strlen("stats items\r\n");
		write_comm = malloc(writelen + 1);
		strlcpy(write_comm, "stats items\r\n", writelen + 1);
		func = memcached_stats_items;
		funcname = "memcached_stats_items";
	}
	else
	{
		writelen = strlen(qn->expr) + 2; // "get KEY1 KEY2 KEY3\r\n"
		write_comm = malloc(writelen + 1);
		strlcpy(write_comm, qn->expr, writelen - 1);
		strcat(write_comm, "\r\n");
		func = memcached_query;
		funcname = "memcached_query";
	}

	char *key = malloc(255);
	snprintf(key, 255, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest->sin_port), write_comm);
	key[strlen(key) - 1] = 0;

	try_again(carg, write_comm, writelen, func, funcname, NULL, key, carg->data);
}

void memcached_handler(char *metrics, size_t size, context_arg *carg)
{
	char *cur = metrics;
	char name[MC_NAME_SIZE+10];
	strcpy(name, "memcached_");
	uint64_t name_size;
	uint64_t msize;

	while (cur-metrics < size)
	{
		cur = strstr(cur, "STAT ");
		if (!cur)
			break;

		cur += 5;
		msize = strcspn(cur, " \t\r\n");
		name_size = MC_NAME_SIZE > msize+1 ? msize+1 : MC_NAME_SIZE;
		strlcpy(name+10, cur, name_size);
		if (!strncmp(name, "memcached_pid", 13))
			continue;

		cur += msize;
		msize = strspn(cur, " \t\r\n");
		cur += msize;
		msize = strcspn(cur, " \t\r\n");

		int rc = metric_value_validator(cur, msize);
		if (rc == DATATYPE_INT)
		{
			int64_t mval = strtoll(cur, &cur, 10);
			metric_add_auto(name, &mval, rc, carg);
		}
		else if (rc == DATATYPE_UINT)
		{
			uint64_t mval = strtoll(cur, &cur, 10);
			metric_add_auto(name, &mval, rc, carg);
		}
		else if (rc == DATATYPE_DOUBLE)
		{
			double mval = strtod(cur, &cur);
			metric_add_auto(name, &mval, rc, carg);
		}
	}

	if (carg->name)
	{
		query_ds *qds = query_get(carg->name);
		if (carg->log_level > 1)
			printf("found queries for datasource: %s: %p\n", carg->name, qds);
		if (qds)
		{
			tommy_hashdyn_foreach_arg(qds->hash, memcached_queries_foreach, carg);
		}
	}
}

string* memcached_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(strdup("stats\n"), 0, 0);
}

void memcached_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("memcached");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = memcached_handler;
	//actx->handler[0].validator = memcached_validator;
	actx->handler[0].mesg_func = memcached_mesg;
	strlcpy(actx->handler[0].key,"memcached", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
