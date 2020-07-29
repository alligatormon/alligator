#include <uv.h>
#include "common/url.h"
#include "events/context_arg.h"
#include "events/client.h"
#include "main.h"
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
