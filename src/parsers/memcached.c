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

void memcached_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	query_node *qn = arg;

	if (carg->log_level > 1)
	{
		puts("+-+-+-+-+-+-+-+");
		printf("run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	}

	uint64_t writelen = strlen(qn->expr) + 2; // "get KEY1 KEY2 KEY3\r\n"
	char *write_comm = malloc(writelen + 1);
	strlcpy(write_comm, qn->expr, writelen - 1);
	strcat(write_comm, "\r\n");

	char *key = malloc(255);
	snprintf(key, 255, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest->sin_port), write_comm);
	key[strlen(key) - 1] = 0;

	try_again(carg, write_comm, writelen, memcached_query, "memcached_query", NULL, key, carg->data);
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
