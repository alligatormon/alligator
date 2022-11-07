#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "main.h"
#define HTTPD_LABEL_SIZE 100

void httpd_status_handler(char *metrics, size_t size, context_arg *carg)
{
	char metric_name[HTTPD_LABEL_SIZE];
	char sval[HTTPD_LABEL_SIZE];
	strlcpy(metric_name, "HTTPD_", 7);
	uint64_t sz;
	char *tmp = strstr(metrics, "ServerUptimeSeconds");
	if (!tmp)
		return;

	for (uint64_t i = 0; i < size;)
	{
		if (!strncmp(tmp, "Scoreboard", 10) || !strncmp(tmp, "ServerUptime", 12)) {}
		else
		{
			sz = strcspn(tmp, ":");
			strlcpy(metric_name+6, tmp, sz+1);
			metric_name_normalizer(metric_name, sz);

			if (carg->log_level > 0)
				printf("name '%s'\n", metric_name);

			tmp += sz;
			tmp += strcspn(tmp, " \t");
			tmp += strspn(tmp, " \t");
			sz = strcspn(tmp, "\n");
			strlcpy(sval, tmp, sz+1);

			if (strstr(sval, "."))
			{
				double dval = strtod(tmp, &tmp);
				metric_add_auto(metric_name, &dval, DATATYPE_DOUBLE, carg);

				if (carg->log_level > 0)
					printf("dval %lf\n", dval);
			}
			else
			{
				uint64_t val = strtoull(tmp, &tmp, 10);
				metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);

				if (carg->log_level > 0)
					printf("val %"u64"\n", val);
			}
		}

		tmp += strcspn(tmp, "\n");
		tmp += strspn(tmp, "\n");

		i = tmp - metrics;
	}
	carg->parser_status = 1;
}

string* httpd_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, "?auto", hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings, NULL), 0, 0);
}

void httpd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("httpd");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = httpd_status_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = httpd_status_mesg;
	strlcpy(actx->handler[0].key, "httpd_status", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
