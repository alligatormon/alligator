#include <uv.h>
#include "common/url.h"
#include "events/context_arg.h"
#include "events/client.h"
#include "main.h"
void smart_aggregator(context_arg *carg)
{
	if (carg->proto == APROTO_UNIX)
		do_unix_client_carg(carg);
	else if (carg->proto == APROTO_TCP)
		do_tcp_client_carg(carg);
	else if (carg->proto == APROTO_HTTP)
		do_tcp_client_carg(carg);
	else if (carg->proto == APROTO_TLS)
		do_tcp_client_carg(carg);
	else if (carg->proto == APROTO_HTTPS)
		do_tcp_client_carg(carg);
	else if (carg->proto == APROTO_FCGI)
		do_tcp_client_carg(carg);
	else if (carg->proto == APROTO_UNIXFCGI)
		do_unix_client_carg(carg);
	//else if (carg->proto == APROTO_PROCESS)
	//	put_to_loop_cmd_carg(carg);
	//else if (carg->proto == APROTO_UNIXGRAM)
	//	do_unixgram_client_carg(carg);
}

void smart_aggregator_selector(host_aggregator_info *hi, void *handler, char *mesg, void *data)
{

	if (!hi)
		return;

	if (hi->transport == APROTO_UNIX)
		do_unix_client(hi->host, handler, mesg, hi->transport);
	else if (hi->transport == APROTO_TCP)
		do_tcp_client(hi->host, hi->port, handler, mesg, hi->transport, data, 0);
	else if (hi->transport == APROTO_HTTP)
		do_tcp_client(hi->host, hi->port, handler, mesg, hi->transport, data, 0);
	else if (hi->transport == APROTO_HTTPS)
		do_tcp_client(hi->host, hi->port, handler, mesg, hi->transport, NULL, 1);
	else if (hi->transport == APROTO_FCGI)
		do_tcp_client(hi->host, hi->port, handler, mesg, hi->transport, data, 0);
	else if (hi->transport == APROTO_UNIXFCGI)
		do_unix_client(hi->host, handler, mesg, hi->transport, data);
	else if (hi->transport == APROTO_UNIXGRAM)
		do_unixgram_client(hi->host, handler, mesg, data);
	else if (hi->transport == APROTO_PROCESS)
		put_to_loop_cmd(hi->host, varnish_handler);
}

void smart_aggregator_selector_buffer(host_aggregator_info *hi, void *handler, uv_buf_t *buffer, size_t buflen, void *data)
{
	if (!hi)
		return;

	if (hi->proto == APROTO_UNIX)
		do_unix_client_buffer(hi->host, handler, buffer, buflen, hi->proto, data);
	else if (hi->proto == APROTO_TCP)
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto, data, 0);
	else if (hi->proto == APROTO_HTTP)
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto, data, 0);
	else if (hi->proto == APROTO_FCGI)
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto, data, 0);
	else if (hi->proto == APROTO_HTTPS)
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto, data, 1);
	else if (hi->proto == APROTO_UNIXFCGI)
		do_unix_client_buffer(hi->host, handler, buffer, buflen, hi->proto, data);
}

void smart_aggregator_selector_plain(int proto, char *hostname, char *port, void *handler, char *mesg, void *data)
{
	if (!hostname)
		return;


	if (proto == APROTO_UNIX)
		do_unix_client(hostname, handler, mesg, data);
	else if (proto == APROTO_TCP)
		do_tcp_client(hostname, port, handler, mesg, proto, data, 0);
	else if (proto == APROTO_HTTP)
		do_tcp_client(hostname, port, handler, mesg, proto, data, 0);
	else if (proto == APROTO_HTTPS)
		do_tcp_client(hostname, port, handler, mesg, proto, data, 1);
	else if (proto == APROTO_UNIXGRAM)
		do_unixgram_client(hostname, handler, mesg);
}

void try_again(context_arg *carg, char *mesg)
{
	
}

