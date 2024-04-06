#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "parsers/multiparser.h"
#include "query/query.h"
#include "main.h"

#define REDIS_NAME_SIZE 100

void redis_keys_free(char **redis_keys)
{
	uint64_t i = 0;
	while (redis_keys[i])
	{
		free(redis_keys[i++]);
	}

	free(redis_keys);
}

void redis_query(char *metrics, size_t size, context_arg *carg)
{
	if (carg->log_level > 0)
		puts(metrics);

	char **redis_keys = carg->data;

	char metricvalue[REDIS_NAME_SIZE];
	uint64_t i = strcspn(metrics, "\r\n");
	i += strspn(metrics + i, "\r\n");
	for (uint64_t j = 0; i < size; i++, j++)
	{
		uint64_t endline = strcspn(metrics + i, "\r\n");
		uint64_t cur = i;
		cur += strcspn(metrics + cur, " \n");
		cur += strspn(metrics + cur, " \n");
		if (carg->log_level > 0)
			printf("metric name is %s\n", redis_keys[j]);

		metric_name_normalizer(redis_keys[j], strlen(redis_keys[j]));

		i += endline;
		i += strspn(metrics + i, "\r\n");
		endline = strcspn(metrics + i, "\r\n");
		cur = i;

		uint64_t copysize = strcspn(metrics + cur, "\r");
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

		if (metric_value_validator(metricvalue, copysize-1))
		{
			double mval = strtod(metricvalue, NULL);
			metric_add_auto(redis_keys[j], &mval, DATATYPE_DOUBLE, carg);
		}
		else
		{
			multicollector(NULL, metricvalue, copysize, carg);
		}

		i += endline;
		i += strspn(metrics + i, "\r\n");
	}

	redis_keys_free(redis_keys);
	carg->parser_status = 1;
}

void redis_keysdump(char *metrics, size_t size, context_arg *carg)
{
	string *get_query = string_new();
	string_cat(get_query, "MGET", 4);
	query_node *qn = carg->data;
	uint64_t copysize;
	char field[REDIS_NAME_SIZE];

	uint64_t num_elements = strtoull(metrics+1, NULL, 10);
	if (!num_elements)
	{
		if (carg->log_level > 1)
			printf("num of elements %"u64", error\n", num_elements);
		return;
	}
	char **redis_keys = calloc(1, (num_elements + 1) * sizeof(void*));
	uint64_t j = 0;

	for (uint64_t i = 0; i < size;)
	{
		uint64_t endline = strcspn(metrics + i, "\r\n");
		uint64_t cur = i;

		strlcpy(field, metrics + cur, endline + 1);
		// ITEM third_metric
		if ((*field != '$') && (*field != '*'))
		{
			if (carg->log_level > 2)
				puts("is item");

			copysize = strcspn(field, " \t\n");
			string_cat(get_query, " ", 1);
			string_cat(get_query, field, copysize);
			redis_keys[j++] = strndup(field, copysize);
		}

		redis_keys[j] = 0;

		i += endline;
		i += strspn(metrics + i, "\r\n");
	}

	string_cat(get_query, "\r\n", 2);

	char *key = malloc(512);
	snprintf(key, 512, "redis_query(tcp://%s:%u)/%s", carg->host, htons(carg->dest.sin_port), qn->expr);

	if (carg->log_level > 0)
		printf("redis glob get query is\n'%s'\nkey '%s'\n", get_query->s, key);
	try_again(carg, get_query->s, get_query->l, redis_query, "redis_query", NULL, key, redis_keys);
	carg->parser_status = 1;
}

void redis_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	query_node *qn = arg;

	if (carg->log_level > 1)
	{
		puts("+-+-+-+-+-+-+-+");
		printf("run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	}

	if (!strncmp(qn->expr, "glob", 4))
	{
		size_t expr_size = strlen(qn->expr);
		for (uint64_t i = 5; i < expr_size;)
		{
			string *pattern_query = string_new();
			string_cat(pattern_query, "KEYS", 4);

			i += strspn(qn->expr + i, " \t");
			uint64_t endfield = strcspn(qn->expr + i, " \t");
			string_cat(pattern_query, " ", 1);
			string_cat(pattern_query, qn->expr + i, endfield);

			i += endfield;
			i += strspn(qn->expr + i, " \t");

			string_cat(pattern_query, "\r\n", 2);

			char *key = malloc(512);
			snprintf(key, 511, "redis_keysdump(tcp://%s:%u)/%s", carg->host, htons(carg->dest.sin_port), qn->expr);
			key[strlen(key) - 1] = 0;

			try_again(carg, pattern_query->s, pattern_query->l, redis_keysdump, "redis_keysdump", NULL, key, qn);
			free(pattern_query);
		}
	}
	else
	{
		size_t expr_size = strlen(qn->expr);
		size_t num_elements = selector_count_field(qn->expr, " \t", expr_size);
		char **redis_keys = calloc(1, (num_elements + 1) * sizeof(void*));
		uint64_t j = 0;
		for (uint64_t i = 5; i < expr_size; j++)
		{
			i += strspn(qn->expr + i, " \t");
			uint64_t endfield = strcspn(qn->expr + i, " \t");
			redis_keys[j] = strndup(qn->expr + i, endfield);

			i += endfield;
			i += strspn(qn->expr + i, " \t");
		}
		redis_keys[j] = 0;

		uint64_t writelen = strlen(qn->expr) + 2; // "MGET KEY1 KEY2 KEY3\r\n"
		char* write_comm = malloc(writelen + 1);
		strlcpy(write_comm, qn->expr, writelen - 1);
		strcat(write_comm, "\r\n");

		char *key = malloc(255);
		snprintf(key, 255, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest.sin_port), qn->expr);
		key[strlen(key) - 1] = 0;

		try_again(carg, write_comm, writelen, redis_query, "redis_query", NULL, key, redis_keys);
	}

}

void redis_queries(char *metrics, size_t size, context_arg *carg)
{
	if (carg->name)
	{
		query_ds *qds = query_get(carg->name);
		if (carg->log_level > 1)
			printf("found queries for datasource: %s: %p\n", carg->name, qds);
		if (qds)
		{
			alligator_ht_foreach_arg(qds->hash, redis_queries_foreach, carg);
		}
	}

	if (metrics && strstr(metrics, "PONG"))
	{
		carg->parser_status = 1;
	}
}

void redis_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp = strstr(metrics, "# Server");
	if (!tmp)
		return;

	tmp += 10;

	uint64_t i, is, ys;
	char mval[REDIS_NAME_SIZE];
	int8_t type;

	int64_t nval = 0;
	int64_t val = 1;


	for (i=0; i<size; i++)
	{
		if (!strncmp(tmp+i, " Memory", 7))
		{
			i += 9;
			for (; i<size && tmp[i]!='#'; i++)
			{
				if (!strncmp(tmp+i, "used_memory:", 12))
				{
					i += 12;
					uint64_t vl = atoll(tmp+i);
					metric_add_labels("redis_used_memory", &vl, DATATYPE_UINT, carg, "type", "total");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_memory_rss:", 16))
				{
					i += 16;
					uint64_t vl = atoll(tmp+i);
					metric_add_labels("redis_used_memory", &vl, DATATYPE_UINT, carg, "type", "rss");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_memory_peak:", 17))
				{
					i += 17;
					uint64_t vl = atoll(tmp+i);
					metric_add_labels("redis_used_memory", &vl, DATATYPE_UINT, carg, "type", "peak");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_memory_lua:", 16))
				{
					i += 16;
					uint64_t vl = atoll(tmp+i);
					metric_add_labels("redis_used_memory", &vl, DATATYPE_UINT, carg, "type", "lua");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "total_system_memory:", 20))
				{
					i += 20;
					uint64_t vl = atoll(tmp+i);
					metric_add_auto("redis_total_system_memory", &vl, DATATYPE_UINT, carg);
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "maxmemory:", 10))
				{
					i += 10;
					uint64_t vl = atoll(tmp+i);
					metric_add_auto("redis_maxmemory", &vl, DATATYPE_UINT, carg);
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "mem_fragmentation_ratio:", 24))
				{
					i += 24;
					double dl = atof(tmp+i);
					metric_add_auto("redis_mem_fragmentation_ratio", &dl, DATATYPE_DOUBLE, carg);
					i += strcspn(tmp+i, "\n");
				}
			}
		}
		else if (!strncmp(tmp+i, " CPU", 4))
		{
			i += 4;
			for (; i<size && tmp[i]!='#'; i++)
			{
				if (!strncmp(tmp+i, "used_cpu_sys:", 13))
				{
					i += 13;
					double dl = atof(tmp+i);
					metric_add_labels("redis_used_cpu", &dl, DATATYPE_DOUBLE, carg, "type", "sys");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_cpu_user:", 14))
				{
					i += 14;
					double dl = atof(tmp+i);
					metric_add_labels("redis_used_cpu", &dl, DATATYPE_DOUBLE, carg, "type", "user");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_cpu_sys_children:", 22))
				{
					i += 22;
					double dl = atof(tmp+i);
					metric_add_labels("redis_used_cpu", &dl, DATATYPE_DOUBLE, carg, "type", "sys_children");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_cpu_user_children:", 23))
				{
					i += 23;
					double dl = atof(tmp+i);
					metric_add_labels("redis_used_cpu", &dl, DATATYPE_DOUBLE, carg, "type", "user_children");
					i += strcspn(tmp+i, "\n");
				}
			}
		}
		else if (!strncmp(tmp+i, " Errorstats", 4))
		{
//# Errorstats
//errorstat_CLUSTERDOWN:count=1653679
			i += 4;
			for (; i<size && tmp[i]!='#'; i++)
			{
				if (!strncmp(tmp+i, "errorstat_", 10))
				{
					i += 10;
					uint64_t i_pre = i;
					char label[REDIS_NAME_SIZE];

					uint64_t size_counter = strcspn(tmp + i_pre, "=");
					i += size_counter + 1;
					strlcpy(label, tmp + i_pre, size_counter + 1);

					double dl = atof(tmp+i);
					metric_add_labels("redis_error_stat", &dl, DATATYPE_DOUBLE, carg, "stat", label);
					i += strcspn(tmp+i, "\n");
				}
			}
		}
		else if (!strncmp(tmp+i, " Commandstats", 13))
		{
			char cmdname[REDIS_NAME_SIZE];
			char *tmp2 = tmp+i+13;
			char *tmp3;
			while (tmp2 && *tmp2 != '#')
			{
				tmp2 = strstr(tmp2, "cmdstat_");
				if (!tmp2)
					break;

				tmp2 += 8;
				tmp3 = strstr(tmp2, ":");
				strlcpy(cmdname, tmp2, tmp3-tmp2+1);
				tmp2 = strstr(tmp2, "calls=");
				if(!tmp2)
					break;
				tmp2 = tmp2+6;
				uint64_t calls = atoll(tmp2);
				tmp2 = strstr(tmp2, "usec=");
				if (!tmp2)
					break;

				tmp2 += 5;
				uint64_t usec = atoll(tmp2);

				tmp2 = strstr(tmp2, "usec_per_call=");
				if (!tmp2)
					break;

				tmp2 += 14;
				double usec_per_call = atof(tmp2);

				tmp2 = strstr(tmp2, "\n");
				if (!tmp2)
					break;

				for (; tmp2 && ((*tmp2 == '\n')||(*tmp2 == '\r')); tmp2++);
				if (!tmp2)
					break;

				metric_add_labels("redis_cmdstat_calls", &calls, DATATYPE_UINT, carg, "cmd", cmdname);
				metric_add_labels("redis_cmdstat_usec", &usec, DATATYPE_UINT, carg, "cmd", cmdname);
				metric_add_labels("redis_cmdstat_usec_per_call", &usec_per_call, DATATYPE_DOUBLE, carg, "cmd", cmdname);
			}
			i = tmp2 - tmp;
		}
		else if (!strncmp(tmp+i, " Keyspace", 9))
		{
			char dbname[REDIS_NAME_SIZE];
			char *tmp2 = tmp+i+9;
			char *tmp3;
			while (tmp2)
			{
				tmp2 = strstr(tmp2, "db");
				if (!tmp2)
					break;

				tmp3 = strstr(tmp2, ":");
				strlcpy(dbname, tmp2, tmp3-tmp2+1);

				tmp2 = strstr(tmp2, "keys=");
				if(!tmp2)
					break;
				tmp2 = tmp2+5;
				uint64_t keys = atoll(tmp2);

				tmp2 = strstr(tmp2, "expires=");
				if (!tmp2)
					break;
				tmp2 += 8;
				uint64_t expires = atoll(tmp2);

				tmp2 = strstr(tmp2, "avg_ttl=");
				if (!tmp2)
					break;
				tmp2 += 8;
				uint64_t avg_ttl = atoll(tmp2);

				metric_add_labels("redis_keys", &keys, DATATYPE_UINT, carg, "db", dbname);
				metric_add_labels("redis_expires", &expires, DATATYPE_UINT, carg, "db", dbname);
				metric_add_labels("redis_avg_ttl", &avg_ttl, DATATYPE_UINT, carg, "db", dbname);

				for (; tmp2 && ((*tmp2 == '\n')||(*tmp2 == '\r')); tmp2++);
				if (!tmp2)
					break;
			}
			i = tmp2 - tmp;
		}
		else
		{
			char mname[REDIS_NAME_SIZE];
			strlcpy(mname, "redis_", 7);
			for (; i<size && tmp[i]!='#'; i++)
			{
				is = strcspn(tmp+i, ":");
				if (!is)
					return;

				strlcpy(mname+6, tmp+i, is+1);
				if (!metric_name_validator(mname, is))
				{
					is = strcspn(tmp+i, "\n");
					i += is;
					continue;
				}
				i += is + 1;

				is = strcspn(tmp+i, "\r\n");
				strlcpy(mval, tmp+i, is+1);
				type = DATATYPE_UINT;
				
				for (ys=0; ys<is; ys++)
				{
					if (mval[ys] == '.' && type == DATATYPE_UINT)
						type = DATATYPE_DOUBLE;
					else if (mval[ys] == '.' && type != DATATYPE_UINT)
					{
						type = -1;
						break;
					}
					else if (!isdigit(mval[ys]))
					{
						type = -1;

						// character metric, check for master status
						if(!strncmp(mname, "redis_role", 10) && !strncmp(mval, "master", 6))
						{
							metric_add_labels("redis_role", &val, DATATYPE_INT, carg, "role", "master");
							metric_add_labels("redis_role", &nval, DATATYPE_INT, carg, "role", "slave");
						}
						else if (!strncmp(mname, "redis_role", 10) && !strncmp(mval, "slave", 5))
						{
							metric_add_labels("redis_role", &nval, DATATYPE_INT, carg, "role", "master");
							metric_add_labels("redis_role", &val, DATATYPE_INT, carg, "role", "slave");
						}

						// check for link status
						if(!strncmp(mname, "redis_master_link_status", 24) && !strncmp(mval, "up", 2))
						{
							metric_add_labels("redis_master_link_status", &val, DATATYPE_INT, carg, "status", "up");
							metric_add_labels("redis_master_link_status", &nval, DATATYPE_INT, carg, "status", "down");
						}
						else if (!strncmp(mname, "redis_master_link_status", 24) && strncmp(mval, "up", 2))
						{
							metric_add_labels("redis_master_link_status", &nval, DATATYPE_INT, carg, "status", "up");
							metric_add_labels("redis_master_link_status", &val, DATATYPE_INT, carg, "status", "down");
						}

						// check for signed
						if (mval[ys] == '-')
						{
							int64_t ivl = atoll(mval);
							metric_add_auto(mname, &ivl, DATATYPE_INT, carg);
						}

						break;
					}
				}

				i += is;
				is = strspn(tmp+i, "\r\n \t")-1;
				i += is;

				if (type == -1)
					continue;

				if (type == DATATYPE_UINT)
				{
					uint64_t vl = atoll(mval);
					metric_add_auto(mname, &vl, DATATYPE_UINT, carg);
				}
				else if (type == DATATYPE_DOUBLE)
				{
					double dl = atoll(mval);
					metric_add_auto(mname, &dl, DATATYPE_DOUBLE, carg);
				}
			}
		}
	}

	redis_queries(NULL, 0, carg);

	carg->parser_status = 1;
}

int8_t redis_validator(context_arg *carg, char *data, size_t size)
{
	char *ret = strstr(data, "Keyspace");
	if (ret)
		return 1;
	else
		return 0;
}

void redis_cluster_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp;
	tmp = strstr(metrics, "cluster_state");
	if (!tmp)
		return;

	uint64_t ok = 1;
	uint64_t fail = 0;
	uint64_t msize;
	uint64_t fsize;
	int64_t mval;

	char mname[REDIS_NAME_SIZE];
	if (!strncmp(tmp, "cluster_state:ok", 16))
		metric_add_auto("redis_cluster_state", &ok, DATATYPE_UINT, carg);
	else
		metric_add_auto("redis_cluster_state", &fail, DATATYPE_UINT, carg);

	uint64_t i = 100;
	strlcpy(mname, "redis_", 7);
	while ((tmp - metrics < size-4) && (i--))
	{
		//printf("%zu <= %zu\n", tmp - metrics, size-4);
		tmp += strcspn(tmp, "\r\n");
		tmp += strspn(tmp, "\r\n");
		msize = strcspn(tmp, ":");
		fsize = msize+1 > REDIS_NAME_SIZE-6 ? REDIS_NAME_SIZE-6 : msize+1;
		strlcpy(mname+6, tmp, fsize);
		metric_name_normalizer(mname, 5+fsize);

		tmp += msize;
		tmp += strspn(tmp, ": ");
		mval = strtoll(tmp, &tmp, 10);

		metric_add_auto(mname, &mval, DATATYPE_INT, carg);
		//printf("%s(%u),%s: %d\n", mname, fsize, mname, mval);
	}

	carg->parser_status = 1;
}

int8_t redis_cluster_validator(context_arg *carg, char *data, size_t size)
{
	if (strncmp(data, "+OK", 3))
	{
		return 0;
	}
	carg->parser_status = 1;

	uint64_t body_size;
	char *tmp;
	uint64_t expect_size = strtoull(data+6, &tmp, 10);

	tmp += strspn(tmp, "\r\n");

	body_size = size - (tmp - data);
	//printf("expect: %u, body: %u\n", expect_size, body_size);
	
	return body_size >= expect_size;
}

void redis_memory_stat_handler(char *metrics, size_t size, context_arg *carg)
{
	char field[REDIS_NAME_SIZE];
	char metric_name[REDIS_NAME_SIZE];
	strlcpy(metric_name, "redis_memory_", REDIS_NAME_SIZE);
	int64_t metric_data;

	char *tmp = strstr(metrics, "\r\n");
	if (!tmp)
		return;

	uint64_t i = tmp - metrics + 2;
	size -= i;

	for (; i < size; i++)
	{
		*field = 0;
		uint64_t field_size = str_get_next(tmp, field, REDIS_NAME_SIZE, "\r\n", &i);
		if (field[0] == '$')
			continue;
		if (field[0] == 0)
			continue;
		if (field[0] == ':')
		{
			metric_data = strtoll(field + 1, NULL, 10);
			//printf("metric: '%s', data: %u\n", metric_name, metric_data);
			metric_add_auto(metric_name, &metric_data, DATATYPE_INT, carg);
		}
		else
		{
			metric_name_normalizer(field, field_size);
			strlcpy(metric_name + 13, field, REDIS_NAME_SIZE - 13);
		}
		//printf("field:\n'%s', %u/%u\n", field, i, size);
	}

	carg->parser_status = 1;
}

int8_t redis_memory_stat_validator(context_arg *carg, char *data, size_t size)
{
	if (data[0] == '*')
		return 1;

	return 0;
}

void redis_latency_stat_handler(char *metrics, size_t size, context_arg *carg)
{
	char field[REDIS_NAME_SIZE];
	char indicator[REDIS_NAME_SIZE];
	char metric_name[REDIS_NAME_SIZE];
	strlcpy(metric_name, "redis_latency_stat_ms", REDIS_NAME_SIZE);
	int64_t metric_data;
	uint64_t arg = 0;

	char *tmp = strstr(metrics, "\r\n");
	if (!tmp)
		return;

	uint64_t i = tmp - metrics + 2;
	size -= i;

	for (; i < size; i++)
	{
		*field = 0;
		str_get_next(tmp, field, REDIS_NAME_SIZE, "\r\n", &i);
		if (carg->log_level > 1)
			printf("redis latency field:\n'%s', %"u64"\n", field, i);
		if (field[0] == '$')
			continue;
		if (field[0] == 0)
			continue;
		if (field[0] == '*')
			continue;
		if (field[0] == ':')
		{
			if (arg == 1)
			{
				metric_data = strtoll(field + 1, NULL, 10);
				if (carg->log_level > 1)
					printf("metric: '%s', indicator: '%s', data: %"u64"\n", metric_name, indicator, metric_data);
				metric_add_labels(metric_name, &metric_data, DATATYPE_INT, carg, "operation", indicator);
			}
			++arg;
		}
		else
		{
			strlcpy(indicator, field, REDIS_NAME_SIZE);
			if (carg->log_level > 1)
				printf("indicator: %s\n", indicator);
			arg = 0;
		}
	}

	carg->parser_status = 1;
}

int8_t redis_latency_stat_validator(context_arg *carg, char *data, size_t size)
{
	if (data[0] == '*')
		return 1;

	return 0;
}

string* redis_parser_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	char* query = malloc(1000);
	if (hi->pass)
		snprintf(query, 1000, "AUTH %s\r\nINFO ALL\r\n", hi->pass);
	else
		snprintf(query, 1000, "INFO ALL\n");

	return string_init_add(query, 0, 0);
}

string* redis_parser_memory_stat_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	char* query = malloc(1000);
	if (hi->pass)
		snprintf(query, 1000, "AUTH %s\r\nMEMORY STATS\r\n", hi->pass);
	else
		snprintf(query, 1000, "MEMORY STATS\r\n");

	return string_init_add(query, 0, 0);
}

string* redis_parser_latency_stat_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	char* query = malloc(1000);
	if (hi->pass)
		snprintf(query, 1000, "AUTH %s\r\nLATENCY LATEST\r\n", hi->pass);
	else
		snprintf(query, 1000, "LATENCY LATEST\r\n");

	return string_init_add(query, 0, 0);
}

string* redis_parser_cluster_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	char* query = malloc(1000);
	if (hi->pass)
		snprintf(query, 1000, "AUTH %s\r\nCLUSTER INFO\r\n", hi->pass);
	else
		snprintf(query, 1000, "CLUSTER INFO\n");

	return string_init_add(query, 0, 0);
}

string* redis_parser_ping_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	char* query = malloc(1000);
	if (hi->pass)
		snprintf(query, 1000, "AUTH %s\r\nPING\r\n", hi->pass);
	else
		snprintf(query, 1000, "PING\r\n");

	return string_init_add(query, 0, 0);
}

void redis_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("redis");
	actx->handlers = 4;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);
	actx->handler[0].name = redis_handler;
	actx->handler[0].validator = redis_validator;
	actx->handler[0].mesg_func = redis_parser_mesg;
	strlcpy(actx->handler[0].key,"redis", 255);
	actx->handler[1].name = redis_cluster_handler;
	actx->handler[1].validator = redis_cluster_validator;
	actx->handler[1].mesg_func = redis_parser_cluster_mesg;
	strlcpy(actx->handler[1].key,"redis_cluster", 255);
	actx->handler[2].name = redis_memory_stat_handler;
	actx->handler[2].validator = redis_memory_stat_validator;
	actx->handler[2].mesg_func = redis_parser_memory_stat_mesg;
	strlcpy(actx->handler[2].key,"redis_memory_stat", 255);
	actx->handler[3].name = redis_latency_stat_handler;
	actx->handler[3].validator = redis_latency_stat_validator;
	actx->handler[3].mesg_func = redis_parser_latency_stat_mesg;
	strlcpy(actx->handler[3].key,"redis_latency_stat", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void redis_parser_ping_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("redis_ping");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);
	actx->handler[0].name = redis_queries;
	actx->handler[0].validator = redis_validator;
	actx->handler[0].mesg_func = redis_parser_ping_mesg;
	strlcpy(actx->handler[0].key,"redis", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
