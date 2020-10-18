#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
void tftp_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t val = 0;
	if ((metrics[0] == '\0') && (metrics[1] == '\3'))
	{
		val = 1;
		if (carg->log_level > 0)
			printf("data package: %s: %"u64"\n", metrics+4, val);
	}
	if ((metrics[0] == '\0') && ((metrics[1] == '\5')))
	{
		if (carg->log_level > 0)
			printf("error package: %s\n", metrics+4);
	}
	if (carg->log_level > 2)
		printf("id: %d:%d %"u64"\n", metrics[2], metrics[3], val);
	metric_add_labels("tftp_file_exists", &val, DATATYPE_UINT, carg, "name", carg->mesg+2);

	//char *mesg = malloc(sizeof(mesg));
	//mesg[0] = 0;
	//mesg[1] = 4;
	//mesg[2] = metrics[2];
	//mesg[3] = metrics[3];
	//try_again(carg, mesg, 4, tftp_handler, "tftp");
}

string* tftp_mesg(host_aggregator_info *hi, void *arg)
{
	size_t query_len = strlen(hi->query);
	char *query = malloc(9+query_len);
	query[0] = 0;
	query[1] = 1;
	strlcpy(query+2, hi->query, query_len+1);
	query[3+query_len] = 0;
	strlcpy(query+3+query_len, "octet", 6);
	//return string_init_alloc("\0\1sendfile.txt\0octet\0", 21);
	return string_init_add(query, 9+query_len, 9+query_len);
}

void tftp_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("tftp");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = tftp_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = tftp_mesg;
	strlcpy(actx->handler[0].key,"tftp", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
