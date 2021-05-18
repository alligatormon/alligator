#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "main.h"
#define HAPROXY_NAME_SIZE 1000

void haproxy_info_handler(char *metrics, size_t size, context_arg *carg)
{
	plain_parse(metrics, size, ": ", "\n", "Haproxy_", 8, carg);
}

void haproxy_pools_handler(char *metrics, size_t size, context_arg *carg)
{
	char name[HAPROXY_NAME_SIZE];
	size_t name_size;
	int64_t i;
	int64_t allocated;
	int64_t bytes;
	int64_t used;
	int64_t users;
	int to;
	size_t sz;
	int64_t cursor;

	to = strcspn(metrics, "\n");
	char *tmp = metrics + to;

	char* total = strstr(metrics, "\nTotal");
	if (!total)
		return;

	for(i=0; i<size && tmp < total; ++i)
	{
		cursor = 0;
		tmp+=10;
		to = strcspn(tmp, "(");
		name_size = HAPROXY_NAME_SIZE > to ? to : HAPROXY_NAME_SIZE;
			strlcpy(name, tmp, name_size);
		name[name_size] = 0;

		to = strcspn(tmp, ":");
		tmp += to;
		int64_t sz_tmp = tmp - metrics;
		if (sz_tmp > size)
			break;
		sz = size - sz_tmp;

		allocated = int_get_next(tmp, sz, ' ', &cursor);
		metric_add_labels("haproxy_pool_allocated", &allocated, DATATYPE_INT, carg, "pool", name);

		bytes = int_get_next(tmp, sz, ' ', &cursor);
		metric_add_labels("haproxy_pool_bytes", &bytes, DATATYPE_INT, carg, "pool", name);

		used = int_get_next(tmp, sz, ' ', &cursor);
		metric_add_labels("haproxy_pool_used", &used, DATATYPE_INT, carg, "pool", name);

		users = int_get_next(tmp, sz, ' ', &cursor);
		metric_add_labels("haproxy_pool_users", &users, DATATYPE_INT, carg, "pool", name);

		tmp += cursor;
		to = strcspn(tmp, "\n");
		tmp += to;
	}
	
	cursor = 0;
	sz = size - (total-tmp);
	
	allocated = int_get_next(total, sz, ' ', &cursor);
	metric_add_auto("haproxy_pool_allocated_total", &allocated, DATATYPE_INT, carg);

	bytes = int_get_next(total, sz, ' ', &cursor);
	metric_add_auto("haproxy_pool_bytes_total", &bytes, DATATYPE_INT, carg);

	used = int_get_next(total, sz, ' ', &cursor);
	metric_add_auto("haproxy_pool_used_total", &used, DATATYPE_INT, carg);
}

void haproxy_stat_handler(char *metrics, size_t size, context_arg *carg)
{
	char name[HAPROXY_NAME_SIZE];
	size_t name_size;

	char svname[HAPROXY_NAME_SIZE];
	size_t svname_size;

	int64_t i;
	int64_t j;
	int64_t to;
	int64_t tmpval;
	char *metrics_labels[] = { "qcur","qmax","scur","smax","slim","stot","bin","bout","dreq","dresp","ereq","econ","eresp","wretr","wredis","status","weight","act","bck","chkfail","chkdown","lastchg","downtime","qlimit","pid","iid","sid","throttle","lbtot","tracked","type","rate","rate_lim","rate_max","check_status","check_code","check_duration","hrsp_1xx","hrsp_2xx","hrsp_3xx","hrsp_4xx","hrsp_5xx","hrsp_other","hanafail","req_rate","req_rate_max","req_tot","cli_abrt","srv_abrt","comp_in","comp_out","comp_byp","comp_rsp","lastsess","last_chk","last_agt","qtime","ctime","rtime","ttime" };
	size_t pointer_size = sizeof(metrics_labels) / sizeof(void*);

	for (i=strcspn(metrics, "\n")+1; i<size; ++i)
	{
		to = strcspn(metrics+i, ",") +1;
		name_size = HAPROXY_NAME_SIZE > to ? to : HAPROXY_NAME_SIZE;
			strlcpy(name, metrics+i, name_size);
		name[name_size] = 0;

		i += to;
		to = strcspn(metrics+i, ",") +1;
		svname_size = HAPROXY_NAME_SIZE > to ? to : HAPROXY_NAME_SIZE;
			strlcpy(svname, metrics+i, svname_size);
		svname[svname_size] = 0;

		for (j=0; j<pointer_size; ++j)
		{
			i += to;
			//printf("j=%"d64", metric: %"d64", size %zu, name %s\n", j, tmpval, pointer_size, metrics_labels[j]);
			to = strcspn(metrics+i, ",");
			if(to)
			{
				if (!strncmp(metrics_labels[j], "status", 6))
				{
					char status[100];
					strlcpy(status, metrics+i, 100 > (to + 1) ? (to + 1) : 100);
					tmpval = 1;
					metric_add_labels3("haproxy_stat", &tmpval, DATATYPE_INT, carg, "name", name, "svname", svname, "status", status);
				}
				else if (!strncmp(metrics_labels[j], "check_status", 12))
				{
					char status[100];
					strlcpy(status, metrics+i, 100 > (to + 1) ? (to + 1) : 100);
					tmpval = 1;
					metric_add_labels3("haproxy_stat", &tmpval, DATATYPE_INT, carg, "name", name, "svname", svname, "check_status", status);
				}
				else
				{
					tmpval = atoll(metrics+i);
					metric_add_labels3("haproxy_stat", &tmpval, DATATYPE_INT, carg, "name", name, "svname", svname, "type", metrics_labels[j]);
				}
			}
			++i;
		}
		to = strcspn(metrics+i, "\n");
		i += to;
	}
}

void haproxy_sess_handler(char *metrics, size_t size, context_arg *carg)
{
	//puts(metrics);
	int64_t i;
	int64_t cnt;
	int64_t to;
	for(i=0, cnt=-1; i<size; ++i, ++cnt)
	{
		//0x55dce59cd3e0: proto=tcpv4 src=127.0.0.1:46776 fe=main be=app srv=app4 ts=08 age=36s calls=8 rq[f=c800000h,i=0,an=2000h,rx=23s,wx=,ax=] rp[f=000000h,i=0,an=00h,rx=,wx=,ax=] s0=[7,8h,fd=1,ex=] s1=[5,108h,fd=2,ex=3s] exp=3s
		to = strcspn(metrics+i, "\n");
		i += to;
	}
	metric_add_auto("haproxy_sess_count", &cnt, DATATYPE_INT, carg);
}

int8_t haproxy_validator(char *data, size_t size)
{
	char *ret = strstr(data, "\n\n");
	if (ret)
		return 1;
	else
		return 0;
}


string* haproxy_stat_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add(gen_http_query(0, hi->query, ";csv", hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings, NULL), 0, 0);
	else
		return string_init_add("show stat\n", 0, 0);
}

string* haproxy_info_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto != APROTO_HTTP) && (hi->proto != APROTO_HTTPS))
		return string_init_add("show info\n", 0, 0);
	else
		return (void*)-1;
}

string* haproxy_pools_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto != APROTO_HTTP) && (hi->proto != APROTO_HTTPS))
		return string_init_add("show pools\n", 0, 0);
	else
		return (void*)-1;
}

string* haproxy_sess_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto != APROTO_HTTP) && (hi->proto != APROTO_HTTPS))
		return string_init_add("show sess\n", 0, 0);
	else
		return (void*)-1;
}

void haproxy_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("haproxy");
	actx->handlers = 4;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = haproxy_stat_handler;
	actx->handler[0].validator = haproxy_validator;
	actx->handler[0].mesg_func = haproxy_stat_mesg;
	strlcpy(actx->handler[0].key,"haproxy_stat", 255);

	actx->handler[1].name = haproxy_info_handler;
	actx->handler[1].validator = haproxy_validator;
	actx->handler[1].mesg_func = haproxy_info_mesg;
	strlcpy(actx->handler[1].key,"haproxy_info", 255);

	actx->handler[2].name = haproxy_pools_handler;
	actx->handler[2].validator = haproxy_validator;
	actx->handler[2].mesg_func = haproxy_pools_mesg;
	strlcpy(actx->handler[2].key,"haproxy_pools", 255);

	actx->handler[3].name = haproxy_sess_handler;
	actx->handler[3].validator = haproxy_validator;
	actx->handler[3].mesg_func = haproxy_sess_mesg;
	strlcpy(actx->handler[3].key,"haproxy_sess", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
