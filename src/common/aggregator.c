#include "common/url.h"
void smart_aggregator_selector(host_aggregator_info *hi, void *handler, char *mesg)
{
	if (!hi)
		return;

	if (hi->proto == APROTO_UNIX)
	{
		do_unix_client(hi->host, handler, mesg);
	}
	else if (hi->proto == APROTO_TCP)
	{
		do_unix_client(hi->host, handler, mesg);
		do_tcp_client(hi->host, hi->port, handler, mesg);
	}
}
