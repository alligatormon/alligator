#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#define GEARMAND_NAME_SIZE 1000

void gearmand_handler(char *metrics, size_t size, context_arg *carg)
{
	char cdc[GEARMAND_NAME_SIZE];
	int64_t i;
	int64_t total = 0;
	int64_t running = 0;
	int64_t available_workers = 0;

	int64_t ctx_total = 0;
	int64_t ctx_running = 0;
	int64_t ctx_available_workers = 0;

	size_t name_size;

	for (i=0; i<size; )
	{
		uint64_t cursor = 0;
		int to = strcspn(metrics+i, "\t");
		name_size = GEARMAND_NAME_SIZE > to+1 ? to+1 : GEARMAND_NAME_SIZE;
		strlcpy(cdc, metrics+i, name_size);
		if ((to == 1) || (*cdc == '.' && to == 2))
			break;


		ctx_total = int_get_next(metrics+i, size, '\t', &cursor);
		metric_add_labels("gearmand_total", &ctx_total, DATATYPE_INT, carg, "function", cdc);

		ctx_running = int_get_next(metrics+i, size, '\t', &cursor);
		metric_add_labels("gearmand_running", &ctx_running, DATATYPE_INT, carg, "function", cdc);

		ctx_available_workers = int_get_next(metrics+i, size, '\t', &cursor);
		metric_add_labels("gearmand_available_workers", &ctx_available_workers, DATATYPE_INT, carg, "function", cdc);

		total += ctx_total;
		running += ctx_running;
		available_workers += ctx_available_workers;

		i += cursor;
	}

	metric_add_auto("gearmand_server_total", &total, DATATYPE_INT, carg);
	metric_add_auto("gearmand_server_running", &running, DATATYPE_INT, carg);
	metric_add_auto("gearmand_server_available_workers", &available_workers, DATATYPE_INT, carg);
	carg->parser_status = 1;
}

int8_t gearmand_validator(context_arg *carg, char *data, size_t size)
{
	char *ret = strstr(data, "\n.\n");
	if (ret)
		return 1;
	else
		return 0;
}

string* gearmand_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(strdup("status\r\n"));
}

void gearmand_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("gearmand");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = gearmand_handler;
	actx->handler[0].validator = gearmand_validator;
	actx->handler[0].mesg_func = gearmand_mesg;
	strlcpy(actx->handler[0].key,"gearmand", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
