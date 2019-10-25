#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "main.h"

void rsyslog_impstats_handler(char *metrics, size_t size, context_arg *carg)
{
	extern aconf *ac;
	if (ac->log_level > 3)
	{
		puts("===================");
		printf("'%s'\n", metrics);
	}
	char field1[255];
	char field2[255];
	char field3[255];
	char action[255];
	char module[255];
	char origin[255];
	*field1 = 0;
	*field2 = 0;
	*field3 = 0;
	*action = 0;
	*module = 0;
	*origin = 0;

	size_t headerend = 0;
	size_t field_size1 = strcspn(metrics, ":");
	strlcpy(field1, metrics, field_size1+1);

	char *tmp  = strstr(metrics+field_size1+1, ":");
	if (tmp)
	{
		size_t field_size2 = tmp - (metrics+field_size1+1);
		strlcpy(field2, metrics+field_size1+1, field_size2+1);
	}

	tmp = strstr(metrics, "origin=");
	if (tmp)
	{
		size_t n;
		n = strcspn(tmp+7, " ");
		strlcpy(field3, tmp+7, n+1);
		headerend = tmp - metrics + n + 8;
	}

	if (*field1 && *field2 && *field3)
	{
		strcpy(action, field1);
		strcpy(module, field2);
		strcpy(origin, field3);
	}
	else if (*field2)
	{
		strcpy(module, field2);
		strcpy(origin, field3);
	}
	else if (*field1)
	{
		strcpy(module, field1);
		strcpy(origin, field3);
	}
	else
	{
		if (ac->log_level > 3)
			puts("no parse rules");
		return;
	}

	if (ac->log_level > 3)
	{
		printf("field1 = '%s'\nfield2 = '%s'\nfield3 = '%s'\n", field1, field2, field3);
		printf("action = '%s'\nmodule = '%s'\norigin = '%s'\n", action, module, origin);
	}

	for (; headerend<size;)
	{
		size_t len;
		len = strcspn(metrics+headerend, "=");
		char key[255];
		strlcpy(key, metrics+headerend, len+1);
		headerend += len + 1;
		len = strcspn(metrics+headerend, " ");
		char name[255];
		strlcpy(name, metrics+headerend, len+1);
		headerend += len + 1;
		if (ac->log_level > 3)
			printf("key: %s, name: %s\n", key, name);
		int64_t vl = atoll(name);
		if (*action)
			metric_add_labels4("rsyslog_stats", &vl, DATATYPE_INT, carg, "module", module, "origin", origin, "action", action, "key", key);
		else
			metric_add_labels3("rsyslog_stats", &vl, DATATYPE_INT, carg, "module", module, "origin", origin, "key", key);
	}
}
