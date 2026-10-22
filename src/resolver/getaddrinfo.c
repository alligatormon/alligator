#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/logs.h"
#include "main.h"
#include "common/aggregator.h"
#include "events/metrics.h"
#include "metric/labels.h"
#include "resolver/getaddrinfo.h"

/* Everything the async callback needs from the originating carg: that carg
 * may be freed first, so copy the host, namespace and labels. */
typedef struct resolver_getaddrinfo_request {
	char *host;
	char *namespace;
	alligator_ht *labels;
} resolver_getaddrinfo_request;

static void resolver_getaddrinfo_emit(resolver_getaddrinfo_request *rq, const char *result, uint64_t ok)
{
	context_arg tmp;

	if (!rq || !rq->host)
		return;

	memset(&tmp, 0, sizeof(tmp));
	tmp.namespace = rq->namespace;
	tmp.labels = rq->labels;
	tmp.ttl = -1;
	tmp.curr_ttl = -1;

	alligator_dns_resolution_status_set(&tmp, rq->host, "A", "IN", result, ok);
}

void resolver_getaddrinfo(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
	/* req->data holds a detached request (host + labels), NOT the originating
	 * context_arg. This request is fire-and-forget and may outlive the carg
	 * that issued it (e.g. one-shot aggregators freed after their TTL
	 * expires), so dereferencing carg here would be a use-after-free. */
	resolver_getaddrinfo_request *rq = req ? (resolver_getaddrinfo_request *)req->data : NULL;
	char* host = rq ? rq->host : NULL;

	char addr[17] = {'\0'};
	if (status < 0)
	{
		glog(L_ERROR, "getaddrinfo resolve failed for hostname %s: %s\n",
			host ? host : "(null)", uv_strerror(status));
		resolver_getaddrinfo_emit(rq, "error", 0);
		/* Drop waiting oneshots so they stop holding buffers and stop
		 * re-resolving on the aggregator crawl until the next schedule. */
		if (host)
			aggregator_oneshot_abandon_host(host);
		uv_freeaddrinfo(res);
		if (rq) {
			if (rq->labels)
				labels_hash_free(rq->labels);
			free(rq->namespace);
			free(rq->host);
			free(rq);
		}
		free(req);
		return;
	}

	uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);
	glog(L_DEBUG, "getaddrinfo resolve done for hostname %s, addr %s\n", host ? host : "(null)", addr);

	if (host)
		dns_record_rule_push(host, DNS_TYPE_A, NULL, 0, addr, 17, 300);
	resolver_getaddrinfo_emit(rq, "success", 1);

	uv_freeaddrinfo(res);
	/* Connect waiting oneshots now; do not wait for the crawl timer. */
	if (host)
		aggregator_oneshot_retry_host(host);
	if (rq) {
		if (rq->labels)
			labels_hash_free(rq->labels);
		free(rq->namespace);
		free(rq->host);
		free(rq);
	}
	free(req);
}

void resolver_getaddrinfo_get(context_arg* carg)
{
	resolver_getaddrinfo_get_host(carg, carg->host);
}

/* Resolve an explicit hostname (e.g. a proxy host) instead of carg->host.
 * The result is cached under and retried for this hostname. */
void resolver_getaddrinfo_get_host(context_arg* carg, const char* host)
{
	resolver_getaddrinfo_request *rq;

	if (!carg || !host || !*host)
		return;

	carglog(carg, L_DEBUG, "resolve host call tcp client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg, &carg->connect, &carg->client, carg->key, host, carg->port, carg->tls, carg->timeout);
	struct addrinfo hints;
	uv_getaddrinfo_t* addr_info = 0;
	addr_info = malloc(sizeof(uv_getaddrinfo_t));
	if (!addr_info)
		return;
	/* Detach the request from carg's lifetime: keep only the data the
	 * callback needs. The carg may be freed before this resolve completes. */
	rq = calloc(1, sizeof(*rq));
	if (!rq) {
		free(addr_info);
		return;
	}
	rq->host = strdup(host);
	if (!rq->host) {
		free(rq);
		free(addr_info);
		return;
	}
	if (carg->labels)
		rq->labels = labels_dup(carg->labels);
	if (carg->namespace)
		rq->namespace = strdup(carg->namespace);
	addr_info->data = rq;

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

	if (uv_getaddrinfo(carg->loop, addr_info, resolver_getaddrinfo, host, carg->port, &hints))
	{
		carglog(carg, L_ERROR, "getaddrinfo start failed key %s host %s port %s\n",
			carg->key ? carg->key : "?", host, carg->port);
		if (rq->labels)
			labels_hash_free(rq->labels);
		free(rq->namespace);
		free(rq->host);
		free(rq);
		free(addr_info);
	}
}
