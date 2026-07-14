#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/logs.h"
#include "main.h"

void resolver_getaddrinfo(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
	/* req->data holds a private strdup of the host, NOT the originating
	 * context_arg. This request is fire-and-forget and may outlive the
	 * carg that issued it (e.g. one-shot aggregators freed after their
	 * TTL expires), so dereferencing carg here would be a use-after-free. */
	char* host = (char*)req->data;
	glog(L_INFO, "getaddrinfo resolve done for hostname %s, status: %d\n", host ? host : "(null)", status);

	char addr[17] = {'\0'};
	if (status < 0)
	{
		uv_freeaddrinfo(res);
		free(host);
		free(req);
		return;
	}

	uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);

	if (host)
		dns_record_rule_push(host, DNS_TYPE_A, NULL, 0, addr, 17, 300);

	uv_freeaddrinfo(res);
	free(host);
	free(req);
}

void resolver_getaddrinfo_get(context_arg* carg)
{
	carglog(carg, L_INFO, "resolve host call tcp client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);
	struct addrinfo hints;
	uv_getaddrinfo_t* addr_info = 0;
	addr_info = malloc(sizeof(uv_getaddrinfo_t));
	/* Detach the request from carg's lifetime: keep only a private copy of
	 * the host. The carg may be freed before this async resolve completes. */
	addr_info->data = *carg->host ? strdup(carg->host) : NULL;

	hints.ai_family = PF_INET;
	if (carg->transport == APROTO_UDP)
	{
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
	}
	else
	{
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
	}
	hints.ai_flags = 0;

	if (uv_getaddrinfo(carg->loop, addr_info, resolver_getaddrinfo, carg->host, carg->port, &hints))
	{
		free(addr_info->data);
		free(addr_info);
	}
}
