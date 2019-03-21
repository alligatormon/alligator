#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "dstructures/metric.h"
#include "events/client_info.h"
#define HAPROXY_NAME_SIZE 1000

void haproxy_info_handler(char *metrics, size_t size, client_info *cinfo)
{
	selector_split_metric(metrics, size, "\n", 1, ": ", 2, "Haproxy_", 8, NULL, 0);
}

void haproxy_pools_handler(char *metrics, size_t size, client_info *cinfo)
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
		metric_labels_add_lbl("haproxy_pool_allocated", &allocated, ALLIGATOR_DATATYPE_INT, 0, "pool", name);

		bytes = int_get_next(tmp, sz, ' ', &cursor);
		metric_labels_add_lbl("haproxy_pool_bytes", &bytes, ALLIGATOR_DATATYPE_INT, 0, "pool", name);

		used = int_get_next(tmp, sz, ' ', &cursor);
		metric_labels_add_lbl("haproxy_pool_used", &used, ALLIGATOR_DATATYPE_INT, 0, "pool", name);

		users = int_get_next(tmp, sz, ' ', &cursor);
		metric_labels_add_lbl("haproxy_pool_users", &users, ALLIGATOR_DATATYPE_INT, 0, "pool", name);

		tmp += cursor;
		to = strcspn(tmp, "\n");
		tmp += to;
	}
	
	cursor = 0;
	sz = size - (total-tmp);
	
	allocated = int_get_next(total, sz, ' ', &cursor);
	metric_labels_add_auto("haproxy_pool_allocated_total", &allocated, ALLIGATOR_DATATYPE_INT, 0);

	bytes = int_get_next(total, sz, ' ', &cursor);
	metric_labels_add_auto("haproxy_pool_bytes_total", &bytes, ALLIGATOR_DATATYPE_INT, 0);

	used = int_get_next(total, sz, ' ', &cursor);
	metric_labels_add_auto("haproxy_pool_used_total", &used, ALLIGATOR_DATATYPE_INT, 0);
}

void haproxy_stat_handler(char *metrics, size_t size, client_info *cinfo)
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
					metric_labels_add_lbl3("haproxy_stat", &tmpval, ALLIGATOR_DATATYPE_INT, 0, "name", name, "svname", svname, "status", status);
				}
				else if (!strncmp(metrics_labels[j], "check_status", 12))
				{
					char status[100];
					strlcpy(status, metrics+i, 100 > (to + 1) ? (to + 1) : 100);
					tmpval = 1;
					metric_labels_add_lbl3("haproxy_stat", &tmpval, ALLIGATOR_DATATYPE_INT, 0, "name", name, "svname", svname, "check_status", status);
				}
				else
				{
					tmpval = atoll(metrics+i);
					metric_labels_add_lbl3("haproxy_stat", &tmpval, ALLIGATOR_DATATYPE_INT, 0, "name", name, "svname", svname, "type", metrics_labels[j]);
				}
			}
			++i;
		}
		to = strcspn(metrics+i, "\n");
		i += to;
	}
}

void haproxy_sess_handler(char *metrics, size_t size, client_info *cinfo)
{
	int64_t i;
	int64_t cnt;
	int64_t to;
	for(i=0, cnt=-1; i<size; ++i, ++cnt)
	{
		to = strcspn(metrics+i, "\n");
		i += to;
	}
	metric_labels_add_auto("haproxy_sess_count", &cnt, ALLIGATOR_DATATYPE_INT, 0);
}

void haproxy_table_select_handler(char *metrics, size_t size, client_info *cinfo)
{
}

void haproxy_table_handler(char *metrics, size_t size, client_info *cinfo)
{
	//smart_aggregator_selector_plain(cinfo->proto, cinfo->hostname, cinfo->port, haproxy_table_handler, "show table backend-slave\n");
}
