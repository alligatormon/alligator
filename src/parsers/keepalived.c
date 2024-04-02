#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "events/fs_read.h"
#include "main.h"
#define KEEPALIVEDLEN_BIG 2048
#define KEEPALIVEDLEN 1024

void keepalived_recurse(context_arg *carg, char *metrics, size_t size, uint64_t indent)
{
	char field[KEEPALIVEDLEN_BIG];
	uint64_t field_len;

	char context[KEEPALIVEDLEN];
	*context = 0;

	char instance[KEEPALIVEDLEN];
	*instance = 0;

	char name[KEEPALIVEDLEN];
	char metric_name[KEEPALIVEDLEN];
	strlcpy(metric_name, "Keepalived_", KEEPALIVEDLEN);
	uint64_t cur_indent = 0;
	for (uint64_t i = 0; i < size; i++)
	{
		*field = 0;
		field_len = str_get_next(metrics, field, KEEPALIVEDLEN_BIG, "\n", &i);

		if (carg->log_level > 1)
			printf("keepalived field: %s\n", field);

		cur_indent = strspn(field, " ");
		if (cur_indent < indent)
			*context = 0;

		if (cur_indent < 2)
			*instance = 0;

		indent = cur_indent;

		uint64_t j = 0;
		uint64_t name_size = str_get_next(field, name, KEEPALIVEDLEN, ":", &j);
		++j;
		if (j < field_len)
		{
			prometheus_metric_name_normalizer(name + cur_indent, name_size - cur_indent);
			strlcpy(metric_name + 11, name + cur_indent, KEEPALIVEDLEN - 11);

			str_get_next(field, name, KEEPALIVEDLEN, ":", &j);

			char *ptrtoval = name + strspn(name, " ");
			if (carg->log_level > 1)
				printf("\t\t'%s {context=\"%s\", instance=\"%s\"} '%s'\n", metric_name, context, instance, ptrtoval);

			if (isdigit(*ptrtoval))
			{
				int64_t val_data = strtoll(ptrtoval, NULL, 10);
				metric_add_labels2(metric_name, &val_data, DATATYPE_INT, ac->system_carg, "context", context, "instance", instance);
			}

			if (cur_indent < 2)
				strlcpy(instance, name + 1, KEEPALIVEDLEN);
		}
		else
		{
			strlcpy(context, name + cur_indent, KEEPALIVEDLEN);

		}
	}
	carg->parser_status = 1;
}

int keepalived_isdigit(char *val)
{
	size_t len = strlen(val);
	size_t to_len = len > 3 ? 3 : len;
	for (uint64_t i = 0; i < to_len; i++)
	{
		if (!isdigit(val[i]) && val[i] != ' ' && val[i] != 's')
			return 0;
	}

	return 1;
}

void keepalived_recurse_data(context_arg *carg, char *metrics, size_t size)
{
	char *tmp = strstr(metrics, "------< VRRP");
	if (!tmp)
		return;

	size -= (tmp - metrics);
	metrics = tmp;

	char field[KEEPALIVEDLEN_BIG];

	char context[KEEPALIVEDLEN];
	*context = 0;

	char instance[KEEPALIVEDLEN];
	*instance = 0;

	char key[KEEPALIVEDLEN];
	*key = 0;

	char name[KEEPALIVEDLEN];
	char metric_name[KEEPALIVEDLEN];
	strlcpy(metric_name, "Keepalived_Data_", KEEPALIVEDLEN);
	uint64_t cur_indent = 0;
	for (uint64_t i = 0; i < size; i++)
	{
		*field = 0;
		str_get_next(metrics, field, KEEPALIVEDLEN_BIG, "\n", &i);

		if (carg->log_level > 1)
			printf("keepalived field: '%s'\n", field);

		cur_indent = strspn(field, " ");
		if (cur_indent < 1)
			*context = 0;

		if (cur_indent < 3)
			*instance = 0;

		uint64_t j = 0;
		uint64_t name_size = str_get_next(field, name, KEEPALIVEDLEN, "=", &j);
		name[--name_size] = 0;
		++j;
		if (*field != '-')
		{
			prometheus_metric_name_normalizer(name + cur_indent, name_size - cur_indent);
			uint64_t metric_name_size = strlcpy(metric_name + 16, name + cur_indent, KEEPALIVEDLEN - 16);
			metric_name_size += 16;

			if (!strcmp(name + cur_indent, "Password"))
				continue;

			if (!strcmp(name + cur_indent, "Command"))
				continue;

			if (!strcmp(name + cur_indent, "Authentication_type"))
				continue;

			if (!strcmp(name + cur_indent, "Tracked_scripts_"))
				continue;

			if (!strcmp(name + cur_indent, "Script_uid:gid"))
				continue;

			if (!strcmp(name + cur_indent, "uid:gid"))
				continue;

			if (strstr(name, "weight"))
				continue;

			if (isdigit(name[cur_indent]))
				continue;

			if (!strcmp(context, "VRRP Sockpool"))
				continue;

			str_get_next(field, name, KEEPALIVEDLEN, "=", &j);

			if (cur_indent < 2)
				strlcpy(instance, name + 1, KEEPALIVEDLEN);

			char *ptrtoval = name + strspn(name, " ");
			char *ptrtocnt = NULL;

			if (keepalived_isdigit(ptrtoval))
			{
				if (carg->log_level > 1)
					printf("\t\t'%s {context=\"%s\", instance=\"%s\"} '%s'\n", metric_name, context, instance, ptrtoval);

				int64_t val_data = strtoll(ptrtoval, NULL, 10);
				//if (!strstr(ptrtoval, "usecs"))
				//	val_data /= 1000000;
				metric_add_labels2(metric_name, &val_data, DATATYPE_INT, ac->system_carg, "context", context, "instance", instance);
			}
			else if ((ptrtocnt = strstr(field, "counter")))
			{
				char *ptrtoval = ptrtocnt + 8;
				metric_name[metric_name_size - 1] = 0;

				if (carg->log_level > 1)
					printf("\t\t'%s {context=\"%s\", instance=\"%s\"} '%s'\n", metric_name, context, instance, ptrtoval);

				int64_t val_data = strtoll(ptrtoval, NULL, 10);
				metric_add_labels2(metric_name, &val_data, DATATYPE_INT, ac->system_carg, "context", context, "instance", instance);
			}
			else if (!strstr(field, "="))
			{
				continue;
			}
			else
			{
				if (carg->log_level > 1)
					printf("\t\t'%s {context=\"%s\", instance=\"%s\", key=\"%s\"{from '%s'} 1\n", metric_name, context, instance, ptrtoval, name);

				int64_t val_data = 1;
				metric_add_labels3(metric_name, &val_data, DATATYPE_INT, ac->system_carg, "context", context, "instance", instance, "key", ptrtoval);
			}
		}
		else
		{
			uint16_t start_sym_context = strcspn(name, "<") + 2;
			uint16_t stop_sym_context = strcspn(name + start_sym_context, ">");
			uint16_t copy_size = stop_sym_context > KEEPALIVEDLEN ? KEEPALIVEDLEN : stop_sym_context;
			strlcpy(context, name + start_sym_context, copy_size);
		}
	}
	carg->parser_status = 1;
}

void keepalived_stats_parser(char *context, size_t size, void *data, char *filename)
{
	context_arg *carg = data;
	keepalived_recurse(carg, context, size, 0);
}

void keepalived_data_parser(char *context, size_t size, void *data, char *filename)
{
	context_arg *carg = data;
	keepalived_recurse_data(carg, context, size);
}

void keepalived_handler(char *metrics, size_t size, context_arg *carg)
{
	int64_t pid = strtoll(metrics, NULL, 10);

	if (carg->log_level > 0)
		printf("keepalived_handler: send SIGUSR1, SIGUSR2 to pid: %"d64"\n", pid);

	uv_kill(pid, SIGUSR1);
	uv_kill(pid, SIGUSR2);

	read_from_file("/tmp/keepalived.stats", 0, keepalived_stats_parser, carg);
	read_from_file("/tmp/keepalived.data", 0, keepalived_data_parser, carg);
}

void keepalived_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("keepalived");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = keepalived_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"keepalived_stats", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
