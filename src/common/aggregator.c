#include <uv.h>
#include "common/url.h"
#include "events/context_arg.h"
#include "events/client.h"
#include "main.h"
#include "parsers/multiparser.h"
#include "parsers/elasticsearch.h"

void smart_aggregator(context_arg *carg)
{
	if (carg->transport == APROTO_UNIX)
		unix_tcp_client(carg);
	else if (carg->transport == APROTO_TCP)
		tcp_client(carg);
	else if (carg->transport == APROTO_TLS)
		tcp_client(carg);
	//else if (carg->transport == APROTO_UDP)
	//	tcp_client(carg);
	//else if (carg->transport == APROTO_ICMP)
	//	unix_tcp_client(carg);
	//else if (carg->transport == APROTO_PROCESS)
	//	put_to_loop_cmd_carg(carg);
	//else if (carg->proto == APROTO_UNIXGRAM)
	//	do_unixgram_client_carg(carg);
}

void try_again(context_arg *carg, char *mesg)
{
	
}

int actx_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((aggregate_context*)obj)->key;
        return strcmp(s1, s2);
}

void aggregate_ctx_init()
{
	ac->aggregate_ctx = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->aggregate_ctx);

	redis_parser_push();
	elasticsearch_parser_push();
	gearmand_parser_push();
	beanstalkd_parser_push();
	eventstore_parser_push();
	clickhouse_parser_push();
	flower_parser_push();
	gdnsd_parser_push();
	hadoop_parser_push();
	sd_etcd_parser_push();
}
