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
	size_t f1_copy = field_size1 < (sizeof(field1) - 1) ? field_size1 : (sizeof(field1) - 1);
	strlcpy(field1, metrics, f1_copy + 1);

	char *tmp  = strstr(metrics+field_size1+1, ":");
	if (tmp)
	{
		size_t field_size2 = tmp - (metrics+field_size1+1);
		size_t f2_copy = field_size2 < (sizeof(field2) - 1) ? field_size2 : (sizeof(field2) - 1);
		strlcpy(field2, metrics+field_size1+1, f2_copy + 1);
	}

	tmp = strstr(metrics, "origin=");
	if (tmp)
	{
		size_t n;
		n = strcspn(tmp+7, " ");
		size_t f3_copy = n < (sizeof(field3) - 1) ? n : (sizeof(field3) - 1);
		strlcpy(field3, tmp+7, f3_copy + 1);
		headerend = tmp - metrics + n + 8;
	}

	if (*field1 && *field2 && *field3)
	{
		strlcpy(action, field1, sizeof(action));
		strlcpy(module, field2, sizeof(module));
		strlcpy(origin, field3, sizeof(origin));
	}
	else if (*field2)
	{
		strlcpy(module, field2, sizeof(module));
		strlcpy(origin, field3, sizeof(origin));
	}
	else if (*field1)
	{
		strlcpy(module, field1, sizeof(module));
		strlcpy(origin, field3, sizeof(origin));
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
		size_t key_copy = len < (sizeof(key) - 1) ? len : (sizeof(key) - 1);
		strlcpy(key, metrics+headerend, key_copy + 1);
		headerend += len + 1;
		len = strcspn(metrics+headerend, " ");
		char name[255];
		size_t name_copy = len < (sizeof(name) - 1) ? len : (sizeof(name) - 1);
		strlcpy(name, metrics+headerend, name_copy + 1);
		headerend += len + 1;
		if (ac->log_level > 3)
			printf("key: %s, name: %s\n", key, name);
		int64_t vl = atoll(name);
		if (*action)
			metric_add_labels4("rsyslog_stats", &vl, DATATYPE_INT, carg, "module", module, "origin", origin, "action", action, "key", key);
		else
			metric_add_labels3("rsyslog_stats", &vl, DATATYPE_INT, carg, "module", module, "origin", origin, "key", key);
	}
	carg->parser_status = 1;
}
