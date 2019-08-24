#include <uv.h>
#include "common/url.h"
#include "events/client_info.h"
#include "events/client.h"
#include "main.h"
void smart_aggregator_selector(host_aggregator_info *hi, void *handler, char *mesg, void *data)
{

	if (!hi)
		return;

	if (hi->proto == APROTO_UNIX)
		do_unix_client(hi->host, handler, mesg, hi->proto);
	else if (hi->proto == APROTO_TCP)
		do_tcp_client(hi->host, hi->port, handler, mesg, hi->proto, data);
	else if (hi->proto == APROTO_HTTP)
		do_tcp_client(hi->host, hi->port, handler, mesg, hi->proto, data);
	else if (hi->proto == APROTO_HTTP_AUTH)
		do_tcp_client(hi->host, hi->port, handler, mesg, hi->proto, data);
	else if (hi->proto == APROTO_HTTPS)
		do_tls_tcp_client(hi->host, hi->port, handler, mesg, hi->proto, NULL, NULL);
	else if (hi->proto == APROTO_HTTPS_AUTH)
		do_tls_tcp_client(hi->host, hi->port, handler, mesg, hi->proto, NULL, NULL);
	else if (hi->proto == APROTO_FCGI)
		do_tcp_client(hi->host, hi->port, handler, mesg, hi->proto, data);
	else if (hi->proto == APROTO_FCGI_AUTH)
		do_tcp_client(hi->host, hi->port, handler, mesg, hi->proto, data);
	else if (hi->proto == APROTO_UNIXFCGI)
		do_unix_client(hi->host, handler, mesg, hi->proto, data);
	else if (hi->proto == APROTO_UNIXGRAM)
		do_unixgram_client(hi->host, handler, mesg, data);
}

void smart_aggregator_selector_buffer(host_aggregator_info *hi, void *handler, uv_buf_t *buffer, size_t buflen, void *data)
{
	if (!hi)
		return;

	if (hi->proto == APROTO_UNIX)
		do_unix_client_buffer(hi->host, handler, buffer, buflen, hi->proto, data);
	else if (hi->proto == APROTO_TCP)
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto, data);
	else if (hi->proto == APROTO_HTTP)
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto, data);
	else if (hi->proto == APROTO_FCGI)
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto, data);
	else if (hi->proto == APROTO_HTTP_AUTH)
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto, data);
	//else if (hi->proto == APROTO_HTTPS)
	//	do_tls_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto);
	else if (hi->proto == APROTO_FCGI_AUTH)
		do_tcp_client_buffer(hi->host, hi->port, handler, buffer, buflen, hi->proto, data);
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
		do_tcp_client(hostname, port, handler, mesg, proto, data);
	else if (proto == APROTO_HTTP)
		do_tcp_client(hostname, port, handler, mesg, proto, data);
	else if (proto == APROTO_HTTP_AUTH)
		do_tcp_client(hostname, port, handler, mesg, proto, data);
	else if (proto == APROTO_HTTPS)
		{}
		//do_tls_tcp_client(hostname, port, handler, mesg, proto);
	else if (proto == APROTO_UNIXGRAM)
		do_unixgram_client(hostname, handler, mesg);
}

void try_again(client_info *cinfo, char *mesg)
{
	
}

