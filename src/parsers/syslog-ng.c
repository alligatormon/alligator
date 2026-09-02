#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

#define SYSLOGNG_NAME_SIZE 1000

static int syslogng_copy_field(char **cur, char *metrics, size_t size, char *dst, size_t dstsz)
{
	if (*cur - metrics >= (ptrdiff_t)size)
		return 0;
	size_t msize = strcspn(*cur, ";");
	if ((*cur)[msize] != ';')
		return 0;
	size_t copy = msize < (dstsz - 1) ? msize : (dstsz - 1);
	strlcpy(dst, *cur, copy + 1);
	*cur += msize + 1;
	return 1;
}

void syslog_ng_handler(char *metrics, size_t size, context_arg *carg)
{
	char *cur = metrics;
	char source_name[SYSLOGNG_NAME_SIZE];
	char source_id[SYSLOGNG_NAME_SIZE];
	char source_instance[SYSLOGNG_NAME_SIZE];
	char state[SYSLOGNG_NAME_SIZE];
	char type[SYSLOGNG_NAME_SIZE];
	uint64_t value;
	while (cur-metrics < size)
	{
		cur += strcspn(cur, "\n");
		if (cur - metrics >= size)
			break;
		++cur;
		if (cur - metrics >= size)
			break;

		if (!syslogng_copy_field(&cur, metrics, size, source_name, sizeof(source_name)))
			break;
		if (strstr(source_name, ".\n"))
			break;
		if (!syslogng_copy_field(&cur, metrics, size, source_id, sizeof(source_id)))
			break;
		if (!syslogng_copy_field(&cur, metrics, size, source_instance, sizeof(source_instance)))
			break;
		if (!syslogng_copy_field(&cur, metrics, size, state, sizeof(state)))
			break;
		if (!syslogng_copy_field(&cur, metrics, size, type, sizeof(type)))
			break;

		value = strtoull(cur, &cur, 10);

		carglog(carg, L_DEBUG, "source_name: '%s', source_id: '%s', source_instance: '%s', state: '%s', type: '%s' ::: %"PRIu64"\n", source_name, source_id, source_instance, state, type, value);
		namespace_metric_family_set(NULL, carg, "syslogng_stats", METRIC_TYPE_GAUGE, "syslog-ng statistics value by source and state.");
		if (!*source_id)
			metric_add_labels4("syslogng_stats", &value, DATATYPE_UINT, carg, "source_name", source_name, "source_instance", source_instance, "state", state, "type", type);
		else if (!*source_instance)
			metric_add_labels4("syslogng_stats", &value, DATATYPE_UINT, carg, "source_name", source_name, "source_id", source_id, "state", state, "type", type);
		else
			metric_add_labels5("syslogng_stats", &value, DATATYPE_UINT, carg, "source_name", source_name, "source_id", source_id, "source_instance", source_instance, "state", state, "type", type);
	}
	carg->parser_status = 1;
}

string* syslog_ng_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("STATS CSV\n", 0);
}

void syslog_ng_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("syslog-ng");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = syslog_ng_handler;
	actx->handler[0].mesg_func = syslog_ng_mesg;
	strlcpy(actx->handler[0].key,"syslog-ng", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
