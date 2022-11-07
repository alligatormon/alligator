#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#define MOOSEFS_METRIC_SIZE 256
#define MOOSEFS_FIELD_SIZE 1024

void moosefs_mfscli_handler(char *metrics, size_t size, context_arg *carg)
{
	char field[MOOSEFS_FIELD_SIZE];
	char token[MOOSEFS_METRIC_SIZE];
	char metric_name[MOOSEFS_METRIC_SIZE];
	for (uint64_t i = 0; i < size; i++)
	{
		uint64_t field_len = str_get_next(metrics, field, MOOSEFS_FIELD_SIZE, "\n", &i);
		if (!strncmp(field, "metadata servers:", 17))
		{
			if (carg->log_level > 1)
				puts("metadata servers:");
			uint64_t mname_size = strlcpy(metric_name, "moosefs_metadata_", MOOSEFS_METRIC_SIZE);
			char *objname[] = { "field", "ip", "version", "state", "local_time", "metadata_version", "metadata_delay", "ram_used", "cpu_used", "last_meta_save", "last_save_duration", "last_save_status", "exports_checksum" };

			char ip[MOOSEFS_METRIC_SIZE];
			*ip = 0;

			for (uint64_t j = 0, k = 0; j < field_len; j++, k++)
			{
				str_get_next(field, token, MOOSEFS_METRIC_SIZE, "#", &j);
				if (k == 1) // IP
					strlcpy(ip, token, MOOSEFS_METRIC_SIZE);
				else if ((k == 2) || (k == 3)) // version and state
				{
					if (carg->log_level > 1)
						printf("\t%s:%s\n", objname[k], token);

					strlcpy(metric_name + mname_size, objname[k], MOOSEFS_METRIC_SIZE - mname_size);
					uint64_t val = 1;
					metric_add_labels2(metric_name, &val, DATATYPE_UINT, carg, "ip",  ip, objname[k], token);
				}
				else if ((k >= 9) && (k <= 11)) //   last_meta_save:- last_save_duration:- last_save_status:-
				{
					if (carg->log_level > 1)
						printf("\t%s:%s\n", objname[k], token);

					strlcpy(metric_name + mname_size, objname[k], MOOSEFS_METRIC_SIZE - mname_size);
					double val = strtod(token, NULL);
					metric_add_labels(metric_name, &val, DATATYPE_DOUBLE, carg, "ip",  ip);
				}
			}
		}
		else if (!strncmp(field, "master info:", 12))
		{
			if (carg->log_level > 1)
				puts("master info:");

			char param[MOOSEFS_METRIC_SIZE];
			*param = 0;
			uint64_t mname_size = strlcpy(metric_name, "moosefs_master_info_", MOOSEFS_METRIC_SIZE);

			for (uint64_t j = 0, k = 0; j < field_len && k < 3; j++, k++)
			{
				str_get_next(field, token, MOOSEFS_METRIC_SIZE, "#", &j);
				if (k == 1)
				{
					uint64_t param_size = strlcpy(param, token, MOOSEFS_METRIC_SIZE);
					prometheus_metric_name_normalizer(param, param_size);
				}
				else if (k == 2)
				{
					if (carg->log_level > 1)
						printf("\t%s:%s\n", param, token);

					if (!strncmp(param, "CPU_used", 8))
						break;

					strlcpy(metric_name + mname_size, param, MOOSEFS_METRIC_SIZE - mname_size);
					int64_t val = strtoll(token, NULL, 10);
					metric_add_auto(metric_name, &val, DATATYPE_INT, carg);
				}
			}
		}
		else if (!strncmp(field, "memory usage detailed info:", 27))
		{
			if (carg->log_level > 1)
				puts("memory usage detailed info:");

			uint64_t mname_size = strlcpy(metric_name, "moosefs_memory_info_", MOOSEFS_METRIC_SIZE);
			char *objname[] = { "field", "object_name", "memory_used", "memory_allocated", "utilization_percent", "percent_of_total_allocated_memory" };

			char param[MOOSEFS_METRIC_SIZE];
			*param = 0;

			for (uint64_t j = 0, k = 0; j < field_len; j++, k++)
			{
				str_get_next(field, token, MOOSEFS_METRIC_SIZE, "#", &j);
				if (k == 1) // param
					strlcpy(param, token, MOOSEFS_METRIC_SIZE);
				else if (k > 1)
				{
					if (carg->log_level > 1)
						printf("\t%s:%s\n", objname[k], token);

					strlcpy(metric_name + mname_size, objname[k], MOOSEFS_METRIC_SIZE - mname_size);
					int64_t val = strtoll(token, NULL, 10);
					metric_add_labels(metric_name, &val, DATATYPE_INT, carg, "param",  param);
				}
			}
		}
		else if (!strncmp(field, "all chunks matrix:", 18))
		{
			if (carg->log_level > 1)
				puts("all chunks matrix:");

			char param[MOOSEFS_METRIC_SIZE];
			*param = 0;
			uint64_t mname_size = strlcpy(metric_name, "moosefs_chunk_matrix_", MOOSEFS_METRIC_SIZE);

			for (uint64_t j = 0, k = 0; j < field_len && k < 3; j++, k++)
			{
				str_get_next(field, token, MOOSEFS_METRIC_SIZE, "#", &j);
				if (k == 1)
				{
					uint64_t param_size = strlcpy(param, token, MOOSEFS_METRIC_SIZE);
					prometheus_metric_name_normalizer(param, param_size);
				}
				else if (k == 2)
				{
					if (carg->log_level > 1)
						printf("\t%s:%s\n", param, token);

					strlcpy(metric_name + mname_size, param, MOOSEFS_METRIC_SIZE - mname_size);
					int64_t val = strtoll(token, NULL, 10);
					metric_add_auto(metric_name, &val, DATATYPE_INT, carg);
				}
			}
		}
		else if (!strncmp(field, "chunk servers:", 14))
		{
			if (carg->log_level > 1)
				puts("chunk servers:");

			uint64_t mname_size = strlcpy(metric_name, "moosefs_chunk_server_", MOOSEFS_METRIC_SIZE);
			char *objname[] = { "field", "host", "port", "id", "labels", "version", "load", "maintenance", "regular_chunks", "regular_used", "regular_total", "regular_percent_used", "removable_status", "removable_chunks", "removable_used", "removable_total", "removable_percent_used" };

			char host[MOOSEFS_METRIC_SIZE];
			char port[MOOSEFS_METRIC_SIZE];
			char id[MOOSEFS_METRIC_SIZE];

			for (uint64_t j = 0, k = 0; j < field_len; j++, k++)
			{
				str_get_next(field, token, MOOSEFS_METRIC_SIZE, "#", &j);
				if (k == 1) // host
					strlcpy(host, token, MOOSEFS_METRIC_SIZE);
				if (k == 2) // port
					strlcpy(port, token, MOOSEFS_METRIC_SIZE);
				if (k == 3) // id
					strlcpy(id, token, MOOSEFS_METRIC_SIZE);
				else if ((k == 4) || (k == 5) || (k == 7)) // labels and version and maintenance
				{
					if (carg->log_level > 1)
						printf("\t:(%s:%s:%s)%s:%s\n", host, port, id, objname[k], token);

					strlcpy(metric_name + mname_size, objname[k], MOOSEFS_METRIC_SIZE - mname_size);
					int64_t val = strtoll(token, NULL, 10);
					metric_add_labels(metric_name, &val, DATATYPE_INT, carg, objname[k], token);
				}
				else if (k > 6)
				{
					if (carg->log_level > 1)
						printf("\t(%s:%s:%s)%s:%s\n", host, port, id, objname[k], token);

					strlcpy(metric_name + mname_size, objname[k], MOOSEFS_METRIC_SIZE - mname_size);
					double val = strtod(token, NULL);
					metric_add_labels3(metric_name, &val, DATATYPE_DOUBLE, carg, "host", host, "port", port, "id", id);
				}
			}
		}
	}
	carg->parser_status = 1;
}

string* moosefs_mfscli_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("-ns\":\" -SIM -SMU -SIG -SCS -SIC -SSC", 0);
}

void moosefs_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("moosefs");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = moosefs_mfscli_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = moosefs_mfscli_mesg;
	strlcpy(actx->handler[0].key,"moosefs_mfscli", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
