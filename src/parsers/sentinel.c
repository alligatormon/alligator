#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"

#define SENTINEL_SIZE 1000

void sentinel_handler(char *metrics, size_t size, context_arg *carg)
{
	char **maps = malloc(sizeof(char*)*1);
	maps[0] = strdup("master0");
	char *res = selector_split_metric(metrics, size, "\r\n", 2, ":", 1, "sentinel_", 9, maps, 1);
	free(maps[0]);
	free(maps);
	if(!res)
		return;
	uint64_t nval = 0;
	uint64_t val = 1;

	char name[SENTINEL_SIZE];
	char address[SENTINEL_SIZE];
	uint64_t sentinels;
	uint64_t slaves;

	char *tmp = res;
	char *tmp2;
	size_t end;

	tmp2 = strstr(tmp, "name=");
	if (!tmp2)
	{
		free(res);
		return;
	}
	tmp = tmp2;
	end = strcspn(tmp+5, ",");
	strlcpy(name, tmp+5, end+1);
	tmp += end;

	if(strstr(tmp, "status=ok"))
	{
		metric_add_labels("sentinel_status", &val, DATATYPE_UINT, 0, "status", "ok");
		metric_add_labels("sentinel_status", &nval, DATATYPE_UINT, 0, "status", "fail");
	}
	else
	{
		metric_add_labels("sentinel_status", &nval, DATATYPE_UINT, 0, "status", "ok");
		metric_add_labels("sentinel_status", &val, DATATYPE_UINT, 0, "status", "fail");
	}

	tmp2 = strstr(tmp, "address=");
	if (!tmp2)
	{
		free(res);
		return;
	}
	tmp = tmp2;
	end = strcspn(tmp+8, ",");
	strlcpy(address, tmp+8, end+1);
	tmp += end;

	tmp2 = strstr(tmp, "slaves=");
	if (!tmp2)
	{
		free(res);
		return;
	}
	tmp = tmp2;
	end = strcspn(tmp+7, ",");
	slaves = atoll(tmp+7);
	tmp += end;

	tmp2 = strstr(tmp, "sentinels=");
	if (!tmp2)
	{
		free(res);
		return;
	}
	tmp = tmp2;
	sentinels = atoll(tmp+10);

	metric_add_labels2("sentinel_slaves", &slaves, DATATYPE_UINT, 0, "name", name, "address", address);
	metric_add_labels2("sentinel_sentinels", &sentinels, DATATYPE_UINT, 0, "name", name, "address", address);

	free(res);
}
