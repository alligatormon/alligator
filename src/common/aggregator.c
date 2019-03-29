#include <uv.h>
#include "common/url.h"
#include "events/client_info.h"
#include "events/client.h"
#include "main.h"
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
		do_tcp_client(hi->host, hi->port, handler, mesg);
	}
	else if (hi->proto == APROTO_UNIXGRAM)
	{
		do_unixgram_client(hi->host, handler, mesg);
	}
}

void smart_aggregator_selector_buffer(host_aggregator_info *hi, void *handler, uv_buf_t *buffer, size_t buflen)
{
	if (!hi)
		return;

	if (hi->proto == APROTO_UNIX)
	{
		do_unix_client_buffer(hi->host, handler, buffer, buflen);
	}
	else if (hi->proto == APROTO_TCP)
	{
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen);
	}
}

void smart_aggregator_selector_plain(int proto, char *hostname, char *port, void *handler, char *mesg)
{
	if (!hostname)
		return;


	if (proto == APROTO_UNIX)
	{
		do_unix_client(hostname, handler, mesg);
	}
	else if (proto == APROTO_TCP)
	{
		do_tcp_client(hostname, port, handler, mesg);
	}
	else if (proto == APROTO_UNIXGRAM)
	{
		do_unixgram_client(hostname, handler, mesg);
	}
}
