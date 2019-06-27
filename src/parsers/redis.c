#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"

#define REDIS_NAME_SIZE 100

void redis_handler(char *metrics, size_t size, client_info *cinfo)
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
					metric_add_labels("redis_used_memory", &vl, DATATYPE_UINT, 0, "type", "total");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_memory_rss:", 16))
				{
					i += 16;
					uint64_t vl = atoll(tmp+i);
					metric_add_labels("redis_used_memory", &vl, DATATYPE_UINT, 0, "type", "rss");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_memory_peak:", 17))
				{
					i += 17;
					uint64_t vl = atoll(tmp+i);
					metric_add_labels("redis_used_memory", &vl, DATATYPE_UINT, 0, "type", "peak");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_memory_lua:", 16))
				{
					i += 16;
					uint64_t vl = atoll(tmp+i);
					metric_add_labels("redis_used_memory", &vl, DATATYPE_UINT, 0, "type", "lua");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "total_system_memory:", 20))
				{
					i += 20;
					uint64_t vl = atoll(tmp+i);
					metric_add_auto("redis_total_system_memory", &vl, DATATYPE_UINT, 0);
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "maxmemory:", 10))
				{
					i += 10;
					uint64_t vl = atoll(tmp+i);
					metric_add_auto("redis_maxmemory", &vl, DATATYPE_UINT, 0);
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "mem_fragmentation_ratio:", 24))
				{
					i += 24;
					double dl = atof(tmp+i);
					metric_add_auto("redis_mem_fragmentation_ratio", &dl, DATATYPE_DOUBLE, 0);
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
					metric_add_labels("redis_used_cpu", &dl, DATATYPE_DOUBLE, 0, "type", "sys");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_cpu_user:", 14))
				{
					i += 14;
					double dl = atof(tmp+i);
					metric_add_labels("redis_used_cpu", &dl, DATATYPE_DOUBLE, 0, "type", "user");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_cpu_sys_children:", 22))
				{
					i += 22;
					double dl = atof(tmp+i);
					metric_add_labels("redis_used_cpu", &dl, DATATYPE_DOUBLE, 0, "type", "sys_children");
					i += strcspn(tmp+i, "\n");
				}
				else if (!strncmp(tmp+i, "used_cpu_user_children:", 23))
				{
					i += 23;
					double dl = atof(tmp+i);
					metric_add_labels("redis_used_cpu", &dl, DATATYPE_DOUBLE, 0, "type", "user_children");
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

				metric_add_labels("redis_cmdstat_calls", &calls, DATATYPE_UINT, 0, "cmd", cmdname);
				metric_add_labels("redis_cmdstat_usec", &usec, DATATYPE_UINT, 0, "cmd", cmdname);
				metric_add_labels("redis_cmdstat_usec_per_call", &usec_per_call, DATATYPE_DOUBLE, 0, "cmd", cmdname);
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

				metric_add_labels("redis_keys", &keys, DATATYPE_UINT, 0, "db", dbname);
				metric_add_labels("redis_expires", &expires, DATATYPE_UINT, 0, "db", dbname);
				metric_add_labels("redis_avg_ttl", &avg_ttl, DATATYPE_UINT, 0, "db", dbname);

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
							metric_add_labels("redis_role", &val, DATATYPE_INT, 0, "role", "master");
							metric_add_labels("redis_role", &nval, DATATYPE_INT, 0, "role", "slave");
						}
						else if (!strncmp(mname, "redis_role", 10) && !strncmp(mval, "slave", 5))
						{
							metric_add_labels("redis_role", &nval, DATATYPE_INT, 0, "role", "master");
							metric_add_labels("redis_role", &val, DATATYPE_INT, 0, "role", "slave");
						}

						// check for link status
						if(!strncmp(mname, "redis_master_link_status", 24) && !strncmp(mval, "up", 2))
						{
							metric_add_labels("redis_master_link_status", &val, DATATYPE_INT, 0, "status", "up");
							metric_add_labels("redis_master_link_status", &nval, DATATYPE_INT, 0, "status", "down");
						}
						else if (!strncmp(mname, "redis_master_link_status", 24) && strncmp(mval, "up", 2))
						{
							metric_add_labels("redis_master_link_status", &nval, DATATYPE_INT, 0, "status", "up");
							metric_add_labels("redis_master_link_status", &val, DATATYPE_INT, 0, "status", "down");
						}

						// check for signed
						if (mval[ys] == '-')
						{
							int64_t ivl = atoll(mval);
							metric_add_auto(mname, &ivl, DATATYPE_INT, 0);
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
					metric_add_auto(mname, &vl, DATATYPE_UINT, 0);
				}
				else if (type == DATATYPE_DOUBLE)
				{
					double dl = atoll(mval);
					metric_add_auto(mname, &dl, DATATYPE_DOUBLE, 0);
				}
			}
		}
	}
}
