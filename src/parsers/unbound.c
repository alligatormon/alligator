#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "main.h"
#define UNBOUND_NAME_SIZE 1024

void unbound_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t i = 0;
	char tmp[UNBOUND_NAME_SIZE];
	char tmp2[UNBOUND_NAME_SIZE];
	char tmp3[UNBOUND_NAME_SIZE];
	char tmp4[UNBOUND_NAME_SIZE];
	uint64_t copysize;
	uint64_t copysize2;
	//uint64_t copysize3;
	char *argindex;
	uint64_t val;
	double dval;

	for (; i < size; i++)
	{
		copysize = strcspn(metrics+i, " =");
		strlcpy(tmp, metrics+i, (copysize > UNBOUND_NAME_SIZE ? UNBOUND_NAME_SIZE : copysize)+1);
		i += copysize;
		i += strspn(metrics+i, "= ");

		if (!strncmp(tmp, "num.query", 9))
		{
			val = strtoull(metrics+i, NULL, 10);
			//printf("'%s' = '%llu'\n", tmp, val);
			argindex = strstr(tmp+10, ".");
			if (argindex)
			{
				strlcpy(tmp2, tmp+10, argindex-tmp-10+1);
				//printf("%s{%s=\"%s\"} %llu\n", "unbound_num_query", tmp2, argindex+1, val);
				metric_add_labels("unbound_num_query", &val, DATATYPE_UINT, carg, tmp2, argindex+1);
			}
			else
				metric_add_labels("unbound_num_query", &val, DATATYPE_UINT, carg, "query", tmp+10);
				//printf("%s{query=\"%s\"} %llu\n", "unbound_num_query", tmp+10, val);

		}
		else if (!strncmp(tmp, "num.answer", 10))
		{
			val = strtoull(metrics+i, NULL, 10);
			//printf("'%s' = '%llu'\n", tmp, val);
			argindex = strstr(tmp+11, ".");
			if (argindex)
			{
				strlcpy(tmp2, tmp+11, argindex-tmp-11+1);
				//printf("%s{%s=\"%s\"} %llu\n", "unbound_num_answer", tmp2, argindex+1, val);
				metric_add_labels("unbound_num_answer", &val, DATATYPE_UINT, carg, tmp2, argindex+1);
			}
			else
				metric_add_labels("unbound_num_answer", &val, DATATYPE_UINT, carg, "answer", tmp+11);
				//printf("%s{answer=\"%s\"} %llu\n", "unbound_num_answer", tmp+11, val);

		}
		else if (!strncmp(tmp, "thread", 6))
		{
			//printf("'%s' = '%llu'\n", tmp, val);

			copysize = strcspn(tmp, ".")+1;
			strlcpy(tmp2, tmp, copysize);

			copysize2 = strlen(tmp+copysize)+1;
			strlcpy(tmp3, tmp+copysize, copysize2);

			//printf("DEBUG: %s\n", tmp+copysize);
			if (!strncmp(tmp+copysize, "recursion.time.avg", 18))
			{
				dval = strtof(metrics+i, NULL);
				//printf("%s{thread=\"%s\"} %lf\n", "recursion_time_avg", tmp2, dval);
				metric_add_labels("unbound_recursion_time_avg", &dval, DATATYPE_DOUBLE, carg, "thread", tmp2);
			}
			else
			{
				val = strtoull(metrics+i, NULL, 10);
				strlcpy(tmp4, "unbound_thread_", UNBOUND_NAME_SIZE);
				strcat(tmp4, tmp3);
				metric_name_normalizer(tmp4, strlen(tmp4));
				//printf("%s {thread=\"%s\"} %llu\n", tmp4, tmp2, val);
				metric_add_labels(tmp4, &val, DATATYPE_UINT, carg, "thread", tmp2);
			}
		}
		else if (!strncmp(tmp, "total.recursion.time.avg", 24))
		{
			dval = strtof(metrics+i, NULL);

			//printf("%s %llu\n", "total_recursion_time_avg", dval);
			metric_add_auto("unbound_total_recursion_time_avg", &dval, DATATYPE_DOUBLE, carg);
		}
		else if (!strncmp(tmp, "total.num", 9))
		{
			val = strtoull(metrics+i, NULL, 10);
			strlcpy(tmp2, tmp+10, strlen(tmp+10)+1);

			//printf("%s{%s=\"%s\"} %llu\n", "unbound_num_total", "num", tmp2, val);
			metric_add_labels("unbound_num_total", &val, DATATYPE_UINT, carg, "num", tmp2);
		}
		else if (!strncmp(tmp, "total.requestlist", 17))
		{
			val = strtoull(metrics+i, NULL, 10);
			strlcpy(tmp2, tmp+18, strlen(tmp+18)+1);

			//printf("%s{%s=\"%s\"} %llu\n", "unbound_total_requestlist", "list", tmp2, val);
			metric_add_labels("unbound_total_requestlist", &val, DATATYPE_UINT, carg, "list", tmp2);
		}
		else if (!strncmp(tmp, "mem", 3))
		{
			val = strtoull(metrics+i, NULL, 10);
			//printf("'%s' = '%llu'\n", tmp, val);

			copysize = strcspn(tmp+4, ".")+1;
			strlcpy(tmp2, tmp+4, copysize);

			copysize2 = strcspn(tmp+4+copysize, ".")+1;
			strlcpy(tmp3, tmp+4+copysize, copysize2);

			//printf("%s{%s=\"%s\"} %llu\n", "unbound_mem", tmp2, tmp3, val);
			metric_add_labels("unbound_mem", &val, DATATYPE_UINT, carg, tmp2, tmp3);
		}
		else if (!strncmp(tmp, "histogram", 9))
		{
			val = strtoull(metrics+i, NULL, 10);
			//printf("'%s' = '%llu'\n", tmp, val);

			argindex = strstr(tmp+10, ".to.");
			argindex += 4;

			double dtmp = strtof(argindex, NULL);
			uint64_t utmp = dtmp * 1000000;
			snprintf(tmp2, UNBOUND_NAME_SIZE, "%"u64, utmp);
			

			//printf("%s{le=%s} %llu\n", "unbound_duration_microseconds", tmp2, val);
			metric_add_labels("unbound_duration_microseconds", &val, DATATYPE_UINT, carg, "bucket", tmp2);
		}
		else if (!strncmp(tmp, "time.up", 7))
		{
			val = strtoull(metrics+i, NULL, 10);
			//printf("%s %llu\n", "unbound_uptime", val);
			metric_add_auto("unbound_uptime", &val, DATATYPE_UINT, carg);
		}
		else if (strncmp(tmp, "time", 4))
		{
			val = strtoull(metrics+i, NULL, 10);
			strlcpy(tmp2, "unbound_", UNBOUND_NAME_SIZE);
			strcat(tmp2, tmp);
			metric_name_normalizer(tmp2, strlen(tmp2));
			//printf("%s %llu\n", tmp2, val);
			metric_add_auto(tmp2, &val, DATATYPE_UINT, carg);
		}
		//else
		//{
		//	val = strtoull(metrics+i, NULL, 10);
		//	printf("==> '%s' = '%llu'\n", tmp, val);
		//}

		i += strcspn(metrics+i, "\n");
	}
}

int8_t unbound_validator(char *data)
{
	char *ret = strstr(data, "key.cache.count");
	if (ret)
		return 1;
	else
		return 0;
}
