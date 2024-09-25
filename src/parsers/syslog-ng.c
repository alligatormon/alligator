#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

#define SYSLOGNG_NAME_SIZE 1000

void syslog_ng_handler(char *metrics, size_t size, context_arg *carg)
{
	char *cur = metrics;
	uint64_t msize;
	char source_name[SYSLOGNG_NAME_SIZE];
	char source_id[SYSLOGNG_NAME_SIZE];
	char source_instance[SYSLOGNG_NAME_SIZE];
	char state[SYSLOGNG_NAME_SIZE];
	char type[SYSLOGNG_NAME_SIZE];
	uint64_t value;
	while (cur-metrics < size)
	{
		cur += strcspn(cur, "\n");
		++cur;

		msize = strcspn(cur, ";");
		strlcpy(source_name, cur, msize+1);
		if (strstr(source_name, ".\n"))
			break;
		cur += msize;
		++cur;

		msize = strcspn(cur, ";");
		strlcpy(source_id, cur, msize+1);
		cur += msize;
		++cur;

		msize = strcspn(cur, ";");
		strlcpy(source_instance, cur, msize+1);
		cur += msize;
		++cur;

		msize = strcspn(cur, ";");
		strlcpy(state, cur, msize+1);
		cur += msize;
		++cur;

		msize = strcspn(cur, ";");
		strlcpy(type, cur, msize+1);
		cur += msize;
		++cur;

		value = strtoull(cur, &cur, 10);

		carglog(carg, L_INFO, "source_name: '%s', source_id: '%s', source_instance: '%s', state: '%s', type: '%s' ::: %"PRIu64"\n", source_name, source_id, source_instance, state, type, value);
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
	return string_init_alloc("STATS\n", 0);
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
