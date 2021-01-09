#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "main.h"

#define NAMED_NAME_SIZE 10000
#define NAMED_SLIM_SIZE 1000

void named_counter_metrics(context_arg *carg, char *ctx_start, char *ctx_end, char *metrics, char **ptr, char *ctx, char *proto, char *ip_version, char *view, char *zone)
{
	uint64_t start_type = 0;
	char type[NAMED_SLIM_SIZE];
	char name[NAMED_SLIM_SIZE];
	char mname[NAMED_SLIM_SIZE];
	uint64_t value;
	char *context_end = NULL;

	char *tmp = strstr(metrics, ctx_start);
	if (tmp)
	{
		context_end = strstr(tmp, ctx_end);
		while (tmp++ < context_end)
		{
			tmp = strstr(tmp, "<counters");
			if (!tmp)
				break;

			if (context_end <= tmp)
				break;

			tmp = strstr(tmp+9, "type=");
			if (!tmp)
				break;

			start_type = strspn(tmp, "\"");
			start_type += strcspn(tmp + start_type, "\"");
			start_type += strspn(tmp + start_type, "\"");
			tmp += start_type;

			start_type = strcspn(tmp, "\"");
			strlcpy(type, tmp, start_type+1);
			metric_name_normalizer(type, start_type);
			tmp += start_type;

			if (carg->log_level > 1)
				printf("type: '%s'\n", type);

			char *tmp2 = strstr(tmp, "<counters ");
			while (tmp < tmp2)
			{
				char *check_counters = strstr(tmp, "<counters ");
				char *check_counter = strstr(tmp, "<counter ");
				//printf("check_counter %p\n", check_counter);
				if (!check_counter)
					break;
				//printf("check_counter OK\n");

				//printf("%p < %p: %d\n", check_counters, check_counter, (check_counters < check_counter));
				if (check_counters < check_counter)
					break;
				//printf("%d: OK\n", check_counters < check_counter);

				tmp = check_counter;

				tmp = strstr(tmp+8, "name=");
				if (!tmp)
					break;

				start_type = strspn(tmp, "\"");
				start_type += strcspn(tmp + start_type, "\"");
				start_type += strspn(tmp + start_type, "\"");
				tmp += start_type;
				start_type = strcspn(tmp, "\"");
				strlcpy(name, tmp, start_type+1);
				tmp += start_type;

				tmp += strcspn(tmp, "\">");
				tmp += strspn(tmp, "\">");
				start_type = strcspn(tmp, "\"");
				value = strtoull(tmp, &tmp, 10);

				snprintf(mname, 255, "named_%s_%s_counter", ctx, type);
				if (carg->log_level > 1)
					printf("\ttype: %s, ctx: %s, proto: %s, ip_version: %s, view: %s, zone: %s, name: '%s' : %"u64"\n", type, ctx, proto ? proto : "", ip_version ? ip_version : "", view ? view : "", zone ? zone : "", name, value);

				tommy_hashdyn *lbl = malloc(sizeof(*lbl));
				tommy_hashdyn_init(lbl);
				if (proto)
					labels_hash_insert_nocache(lbl, "proto", proto);
				if (ip_version)
					labels_hash_insert_nocache(lbl, "ip_version", ip_version);
				if (view)
					labels_hash_insert_nocache(lbl, "view", view);
				if (zone)
					labels_hash_insert_nocache(lbl, "zone", zone);
				labels_hash_insert_nocache(lbl, "name", name);

				metric_add(mname, lbl, &value, DATATYPE_UINT, carg);
			}
		}
	}
	if (ptr)
		*ptr = context_end;
}

char* named_get_object(char *tmp, char **ptr)
{
	if (!tmp)
		return NULL;

	char *ipv = strstr(tmp, "<");
	if (!ipv)
		return NULL;
	++ipv;

	uint64_t ipv_size = strcspn(ipv, ">");
	//char *ipv_name = malloc(ipv_size);
	char *ipv_name = strndup(ipv, ipv_size);
	//strlcpy(ipv_name, ipv, ipv_size);
	*ptr = ipv;
	*ptr += ipv_size;

	return ipv_name;
}

void named_get_taskmgr_value(context_arg *carg, char *metrics, char *node_name, char *name)
{
	char *str = strstr(metrics, node_name);
	if (str)
	{
		str += strcspn(str, ">");
		str += strspn(str, ">");
		uint64_t value = strtoull(str, NULL, 10);

		if (carg->log_level > 1)
			printf("taskmgr thread-mode %s: %"u64"\n", name, value);

		char mname[255];
		snprintf(mname, 254, "named_taskmgr_thread_model_%s", name);
		metric_add_auto(mname, &value, DATATYPE_UINT, carg);
	}
}

void named_get_value(context_arg *carg, char *metrics, char *resource, char *node_name, char *name, char *id, char *ctx_name, char *ctx_end)
{
	char *str = strstr(metrics, node_name);
	if (str)
	{
		str += strcspn(str, ">");
		str += strspn(str, ">");
		uint64_t value = strtoull(str, NULL, 10);

		char mname[255];
		snprintf(mname, 254, "%s_%s", resource, name);
		if (carg->log_level > 1)
			printf("%s: %"u64"\n", mname, value);

		if (ctx_name && id)
			metric_add_labels2(mname, &value, DATATYPE_UINT, carg, "name", ctx_name, "id", id);
		else
			metric_add_auto(mname, &value, DATATYPE_UINT, carg);
	}
}

void named_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp = strstr(metrics, "<statistics");
	if (!tmp)
		return;

	//named_counter_metrics(carg, "<server>", "</server>", tmp, NULL, "server", NULL, NULL, NULL, NULL);

	tmp = strstr(metrics, "<traffic>");
	if (tmp)
	{
		tmp += 9;
		char *tmp_end = strstr(tmp, "</traffic>");

		while (tmp < tmp_end)
		{
			char *ipv = named_get_object(tmp, &tmp);
			if (!ipv)
				break;

			if (!tmp)
				break;

			named_counter_metrics(carg, "<udp>", "</udp>", tmp, NULL, "traffic", "udp", ipv, NULL, NULL);
			named_counter_metrics(carg, "<tcp>", "</tcp>", tmp, &tmp, "traffic", "tcp", ipv, NULL, NULL);

			named_get_object(tmp, &tmp);
			named_get_object(tmp, &tmp);
		}
	}

	tmp = strstr(metrics, "<views>");
	if (tmp)
	{
		uint64_t serial;
		tmp += 7;
		char *tmp_end = strstr(tmp, "</views>");
		char view_name[255];
		char zone_name[255];
		char type[255];

		while (tmp < tmp_end)
		{
			char *view = tmp = strstr(tmp, "<view ");
			if (!tmp)
				break;

			if (tmp > tmp_end)
				break;

			tmp = strstr(tmp, "name=\"");
			if (!tmp)
				break;

			tmp += 6;
			uint64_t view_name_size = strcspn(tmp, "\"");
			strlcpy(view_name, tmp, view_name_size+1);

			tmp = strstr(tmp, "<zones>");
			if (!tmp)
				break;
			char* zones_end = strstr(tmp, "</zones>");

			char *tmp2 = tmp;
			while (tmp2 < zones_end)
			{
				char *zone_start = tmp2 = strstr(tmp2, "<zone ");
				if (!tmp2)
					break;

				char *zone_end = strstr(tmp2, "</zone>");

				tmp2 = strstr(tmp2, "name=\"");
				if (!tmp2)
					break;

				tmp2 += 6;
				uint64_t zone_name_size = strcspn(tmp2, "\"");
				strlcpy(zone_name, tmp2, zone_name_size+1);

				named_counter_metrics(carg, "<zone", "</zone>", zone_start, NULL, "zone", NULL, NULL, NULL, zone_name);

				tmp2 = strstr(tmp2, "<type>");
				if (!tmp2)
				{
					break;
				}
				tmp2 += 6;

				if (tmp2 > zones_end)
					break;

				if (tmp2 > zone_end)
					continue;

				uint64_t type_size = strcspn(tmp2, "<");
				strlcpy(type, tmp2, type_size+1);

				tmp2 = strstr(tmp2, "<serial>");
				if (!tmp2)
					break;

				if (tmp2 > zones_end)
					break;

				if (tmp2 > zone_end)
					continue;

				serial = strtoull(tmp2, &tmp2, 10);
				if (carg->log_level > 1)
					printf("view name: %s, zone name: %s, type: %s: %"u64"\n", view_name, zone_name, type, serial);
				metric_add_labels3("named_view_zone_serial", &serial, DATATYPE_UINT, carg, "view", view_name, "zone", zone_name, "type", type);
			}

			char *tmp3 = strstr(view, "<cache");
			char* cache_end = strstr(tmp3, "</cache>");
			if (tmp3 && cache_end)
			{
				char cache_name[255];
				char rrset_name_str[255];

				tmp3 = strstr(tmp3, "name=\"");
				if (!tmp3)
					break;

				tmp3 += 6;
				if (tmp3 > cache_end)
					break;

				uint64_t cache_size = strcspn(tmp3, "\"");
				strlcpy(cache_name, tmp3, cache_size+1);

				while (tmp3 < cache_end)
				{
					char *rrset = tmp3 = strstr(tmp3, "<rrset>");
					if (!tmp3)
						break;

					tmp3 += 7;
					char *rrset_end = strstr(tmp3, "</rrset>");
					char *rrset_name = strstr(tmp3, "<name>");
					if (!rrset_name)
						continue;

					tmp3 = rrset_name + 6;
					if (tmp3 > rrset_end)
					{
						continue;
					}

					uint64_t rrset_size = strcspn(tmp3, "<");
					strlcpy(rrset_name_str, tmp3, rrset_size+1);

					char *rrset_counter = strstr(rrset, "<counter>");
					if (!rrset_counter)
						continue;

					tmp3 += 9;
					uint64_t value = strtoull(tmp3, &tmp3, 10);

					if (carg->log_level > 1)
						printf("view: %s, cache: %s, rrset name: %s: %"u64"\n", view_name, cache_name, rrset_name_str, value);

					metric_add_labels3("named_cache_rrset", &value, DATATYPE_UINT, carg, "view", view_name, "cache", cache_name, "name", rrset_name_str);
				}
			}

			named_counter_metrics(carg, "<view", "</view>", view, NULL, "view", NULL, NULL, view_name, NULL);
		}

	}

	tmp = strstr(metrics, "<socketmgr>");
	if (tmp)
	{
		char *ctx_end = strstr(tmp, "</socketmgr>");
		uint64_t udp = 0;
		uint64_t tcp = 0;
		uint64_t other = 0;
		while (tmp < ctx_end)
		{
			tmp = strstr(tmp, "<type>");
			if (!tmp)
				break;
			if (tmp > ctx_end)
				break;

			tmp += strcspn(tmp, ">");
			tmp += strspn(tmp, ">");
			if (!strncmp(tmp, "udp", 3))
				++udp;
			else if (!strncmp(tmp, "tcp", 3))
				++tcp;
			else
				++other;
		}
		metric_add_labels("named_sockets_count", &tcp, DATATYPE_UINT, carg, "proto", "tcp");
		metric_add_labels("named_sockets_count", &udp, DATATYPE_UINT, carg, "proto", "udp");
		metric_add_labels("named_sockets_count", &other, DATATYPE_UINT, carg, "proto", "other");
	}

	tmp = strstr(metrics, "<taskmgr>");
	if (tmp)
	{
		char *task = strstr(tmp, "<thread-model>");
		if (task)
		{
			task += 14;
			char *str = strstr(task, "<type>");
			if (str)
			{
				char type[255];
				str += strcspn(str, ">");
				str += strspn(str, ">");
				//uint64_t value = 1;
				uint64_t size = strcspn(str, "<");

				strlcpy(type, str, size+1);
				if (carg->log_level > 1)
					printf("taskmgr thread-mode type: %s\n", type);
			}
			named_get_taskmgr_value(carg, task, "<worker-threads>", "worker_threads");
			named_get_taskmgr_value(carg, task, "default-quantum", "default_quantum");
			named_get_taskmgr_value(carg, task, "tasks-running", "tasks_running");
			named_get_taskmgr_value(carg, task, "tasks-ready", "tasks_ready");
		}
	}

	tmp = strstr(metrics, "<memory>");
	if (tmp)
	{
		char *ctxs = strstr(tmp, "<contexts>");
		char *ctxs_end = strstr(tmp, "</contexts>");
		if (ctxs)
		{
			ctxs += 10;
			tmp = ctxs;
			char name[255];
			char id[255];
			uint64_t size;
			while (tmp && tmp < ctxs_end)
			{
				tmp = strstr(tmp, "<context>");
				if (!tmp)
					break;

				char *ctx_end = strstr(tmp, "</context>");
				char *name_ptr = strstr(tmp, "<name>");
				if (!name_ptr)
					continue;
				name_ptr += strcspn(name_ptr, ">");
				name_ptr += strspn(name_ptr, ">");
				size = strcspn(name_ptr, "<");
				strlcpy(name, name_ptr, size+1);

				char *id_ptr = strstr(tmp, "<id>");
				if (!id_ptr)
					continue;
				id_ptr += strcspn(id_ptr, ">");
				id_ptr += strspn(id_ptr, ">");
				size = strcspn(id_ptr, "<");
				strlcpy(id, id_ptr, size+1);

				named_get_value(carg, tmp, "named_memory_context", "<total>", "total", id, name, ctx_end);
				named_get_value(carg, tmp, "named_memory_context", "<inuse>", "inuse", id, name, ctx_end);
				named_get_value(carg, tmp, "named_memory_context", "<pools>", "pools", id, name, ctx_end);
				named_get_value(carg, tmp, "named_memory_context", "<hiwater>", "hiwater", id, name, ctx_end);
				named_get_value(carg, tmp, "named_memory_context", "<lowater>", "lowater", id, name, ctx_end);
				named_get_value(carg, tmp, "named_memory_context", "<blocksize>", "blocksize", id, name, ctx_end);
				tmp = ctx_end;
			}
		}

		tmp = ctxs_end;
		tmp = strstr(tmp, "<summary>");
		char *ctx_end = strstr(tmp, "</summary>");
		if (tmp)
		{
			named_get_value(carg, tmp, "named_memory_summary", "<TotalUse>", "total_use", NULL, NULL, ctx_end);
			named_get_value(carg, tmp, "named_memory_summary", "<InUse>", "in_use", NULL, NULL, ctx_end);
			named_get_value(carg, tmp, "named_memory_summary", "<BlockSize>", "block_size", NULL, NULL, ctx_end);
			named_get_value(carg, tmp, "named_memory_summary", "<ContextSize>", "context_size", NULL, NULL, ctx_end);
			named_get_value(carg, tmp, "named_memory_summary", "<Lost>", "lost", NULL, NULL, ctx_end);
		}
	}
}

string* named_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings), 0, 0);
}

void named_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("named");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = named_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = named_mesg;
	strlcpy(actx->handler[0].key,"named", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
