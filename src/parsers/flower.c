#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"
#define FLOWER_LABEL_SIZE 1000
void flower_handler(char *metrics, size_t size, client_info *cinfo)
{
	char *cur = metrics;
	size_t n;
	char label[FLOWER_LABEL_SIZE];
	puts(cur);

	cur = strstr(cur, "Active:")+8;
	int64_t total_active = atoll(cur);
	metric_add_auto("flower_tasks_total_active", &total_active, DATATYPE_INT, 0);

	cur = strstr(cur, "Processed:")+11;
	int64_t total_processed = atoll(cur);
	metric_add_auto("flower_tasks_total_processed", &total_processed, DATATYPE_INT, 0);

	cur = strstr(cur, "Failed:")+8;
	int64_t total_failed = atoll(cur);
	metric_add_auto("flower_tasks_total_failed", &total_failed, DATATYPE_INT, 0);

	cur = strstr(cur, "Succeeded:")+11;
	int64_t total_successed = atoll(cur);
	metric_add_auto("flower_tasks_total_successed", &total_successed, DATATYPE_INT, 0);

	cur = strstr(cur, "Retried:")+9;
	int64_t total_retried = atoll(cur);
	metric_add_auto("flower_tasks_total_retried", &total_retried, DATATYPE_INT, 0);

	while ((cur = strstr(cur, "<tr id=")))
	{
		cur += 8;
		n = strcspn(cur, "\">");
		int64_t label_size = FLOWER_LABEL_SIZE > n+1 ? n+1 : FLOWER_LABEL_SIZE;
		int64_t i, j;
		for (i=0, j=0; i<label_size && j<label_size; ++j, ++i)
		{
			if (!strncmp(cur+i, "%40", 3))
			{
				label[i] = '@';
				j += 2;
			}
			else
				label[i] = cur[j];
		}
		label[i-1] = 0;

		cur = strstr(cur, "<td>");
		if (!cur)
			return;
		cur += 4;

		cur = strstr(cur, "<td>");
		if (!cur)
			return;
		cur += 4;
		int status;
		if(!strncmp(cur, "True", 4))
			status = 1;
		else
			status = 0;
		metric_add_labels("flower_tasks_active", &status, DATATYPE_INT, 0, "worker",  label);

		cur = strstr(cur, "<td>");
		if (!cur)
			return;
		cur += 4;
		int64_t task_active = atoll(cur);
			metric_add_labels("flower_tasks_active", &task_active, DATATYPE_INT, 0, "worker",  label);

		cur = strstr(cur, "<td>");
		if (!cur)
			return;
		cur += 4;
		int64_t task_processed = atoll(cur);
			metric_add_labels("flower_tasks_processed", &task_processed, DATATYPE_INT, 0, "worker",  label);

		cur = strstr(cur, "<td>");
		if (!cur)
			return;
		cur += 4;
		int64_t task_failed = atoll(cur);
			metric_add_labels("flower_tasks_failed", &task_failed, DATATYPE_INT, 0, "worker",  label);

		cur = strstr(cur, "<td>");
		if (!cur)
			return;
		cur += 4;
		int64_t task_successed = atoll(cur);
			metric_add_labels("flower_tasks_successed", &task_successed, DATATYPE_INT, 0, "worker",  label);

		cur = strstr(cur, "<td>");
		if (!cur)
			return;
		cur += 4;
		int64_t task_retried = atoll(cur);
			metric_add_labels("flower_tasks_retried", &task_retried, DATATYPE_INT, 0, "worker",  label);
	}
}
