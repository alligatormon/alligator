#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "common/json_query.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/validator.h"
#include "common/logs.h"
#include "main.h"
#define LIGHTTPD_LABEL_SIZE 100
void lighttpd_status_handler(char *metrics, size_t size, context_arg *carg)
{
	carg->parser_status = json_query(metrics, NULL, "lighttpd", carg, carg->pquery, carg->pquery_size);
}

void lighttpd_statistics_handler(char *metrics, size_t size, context_arg *carg)
{
	char metric_name[LIGHTTPD_LABEL_SIZE];
	strlcpy(metric_name, "lighttpd_", 10);
	char module[LIGHTTPD_LABEL_SIZE];
	char type[LIGHTTPD_LABEL_SIZE];
	char target[LIGHTTPD_LABEL_SIZE];
	char backend[LIGHTTPD_LABEL_SIZE];
	char debug_string[LIGHTTPD_LABEL_SIZE];
	alligator_ht *lbl = NULL;

	char *tmp = metrics;
	uint64_t sz;
	uint64_t val;
	for (uint64_t i = 0; i < size; i++)
	{
		if (carg->log_level > 0)
		{
			puts("=========");
			size_t dbg_sz = strcspn(tmp, "\n");
			size_t dbg_copy = dbg_sz < (sizeof(debug_string) - 1) ? dbg_sz : (sizeof(debug_string) - 1);
			strlcpy(debug_string, tmp, dbg_copy + 1);
			printf("lighttpd metric '%s', %p\n", debug_string, tmp);
		}

		lbl = calloc(1, sizeof(*lbl));
		alligator_ht_init(lbl);
		module[0] = 0;
		type[0] = 0;
		target[0] = 0;
		backend[0] = 0;

		sz = strcspn(tmp, ".:");
		size_t module_copy = sz < (sizeof(module) - 1) ? sz : (sizeof(module) - 1);
		strlcpy(module, tmp, module_copy + 1);
		labels_hash_insert_nocache(lbl, "module", module);

		carglog(carg, L_INFO, "lighttpd module: '%s', %p, %"u64"\n", module, tmp, sz);

		tmp += sz;
		int is_requests = 0;
		int is_active_requests = 0;
		if (*tmp == '.')
		{
			tmp += strspn(tmp, ".:");
			sz = strcspn(tmp, ".:");
			size_t type_copy = sz < (sizeof(type) - 1) ? sz : (sizeof(type) - 1);
			strlcpy(type, tmp, type_copy + 1);

			carglog(carg, L_INFO, "lighttpd type: '%s', %p, %"u64"\n", type, tmp, sz);

			tmp += sz;
			if (!strcmp(type, "requests"))
			{
				type[0] = 0;
				target[0] = 0;
				backend[0] = 0;
				is_requests = 1;
			}
			else if (!strcmp(type, "active-requests"))
			{
				type[0] = 0;
				target[0] = 0;
				backend[0] = 0;
				is_active_requests = 1;
			}
			else if (*tmp == '.')
			{
				
				labels_hash_insert_nocache(lbl, "type", type);
				tmp += strspn(tmp, ".:");
				sz = strcspn(tmp, ".:");
				size_t target_copy = sz < (sizeof(target) - 1) ? sz : (sizeof(target) - 1);
				strlcpy(target, tmp, target_copy + 1);

				carglog(carg, L_INFO, "lighttpd target: '%s', %p, %"u64"\n", target, tmp, sz);

				labels_hash_insert_nocache(lbl, "target", target);
				tmp += sz;

				if (*tmp == '.')
				{
					tmp += strspn(tmp, ".:");
					sz = strcspn(tmp, ".:");
					size_t backend_copy = sz < (sizeof(backend) - 1) ? sz : (sizeof(backend) - 1);
					strlcpy(backend, tmp, backend_copy + 1);

					carglog(carg, L_INFO, "lighttpd backend: '%s', %p, %"u64"\n", backend, tmp, sz);

					labels_hash_insert_nocache(lbl, "type", type);
					if (strcmp(backend, "load"))
					{
						backend[0] = 0;
						tmp += sz;
					}
					else
						labels_hash_insert_nocache(lbl, "backend", backend);
				}
			}
		}

		tmp += strspn(tmp, ".:");
		sz = strcspn(tmp, ".:");

		size_t line_dbg_sz = strcspn(tmp, "\n");
		size_t line_dbg_copy = line_dbg_sz < (sizeof(debug_string) - 1) ? line_dbg_sz : (sizeof(debug_string) - 1);
		strlcpy(debug_string, tmp, line_dbg_copy + 1);

		if (is_requests) {
			val = 0;
			metric_add("lighttpd_requests", lbl, &val, DATATYPE_UINT, carg);
		}
		else if (is_active_requests) {
			val = strtoull(tmp, &tmp, 10);
			metric_add("lighttpd_active_requests", lbl, &val, DATATYPE_UINT, carg);
		}
		else {
			carglog(carg, L_DEBUG, "lighttpd get metricname from '%s', %p\n", debug_string, tmp);

			size_t metric_name_rem = sizeof(metric_name) - 9;
			size_t metric_copy = sz < (metric_name_rem - 1) ? sz : (metric_name_rem - 1);
			strlcpy(metric_name+9, tmp, metric_copy + 1);
			metric_name_normalizer(metric_name, sz);
			carglog(carg, L_DEBUG, "lighttpd metric_name: '%s', %p, %"u64"\n", metric_name, tmp, sz);

			tmp += strcspn(tmp, ":");
			tmp += strspn(tmp, ":");
			val = strtoull(tmp, &tmp, 10);

			metric_add(metric_name, lbl, &val, DATATYPE_UINT, carg);

		}
		tmp += strcspn(tmp, "\n");
		tmp += strspn(tmp, "\n");
		i = tmp - metrics;
	}
	carg->parser_status = 1;
}

string* lighttpd_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, "?json", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

string* lighttpd_statistics_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

void lighttpd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("lighttpd_status");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = lighttpd_status_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = lighttpd_status_mesg;
	strlcpy(actx->handler[0].key,"lighttpd_status", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));


	actx = calloc(1, sizeof(*actx));

	actx->key = strdup("lighttpd_statistics");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = lighttpd_statistics_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = lighttpd_statistics_mesg;
	strlcpy(actx->handler[0].key,"lighttpd_statistics", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
