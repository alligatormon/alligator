#include <uv.h>
#include "common/entrypoint.h"
#include "resolver/resolver.h"
#include "main.h"

int is_ip_addr(char *str)
{
	struct sockaddr_in sa;
	int result = inet_pton(AF_INET, str, &(sa.sin_addr));
	return result != 0;
}


char *resolver_carg_get_addr(context_arg *carg)
{
	char *addr = NULL;
	// check if already host is ip address
	if (is_ip_addr(carg->host))
		addr = carg->host;
	else
	{
		// check if already resolved
		string *addr_name = aggregator_get_addr(carg, carg->host, DNS_TYPE_A, DNS_CLASS_IN);
		addr = addr_name ? addr_name->s : NULL;
	}

	return addr;
}

void resolver_init_client_timer(context_arg *carg, void* timer_callback)
{
	carg->tt_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
	carg->tt_timer->data = carg;
	uv_timer_init(carg->loop, carg->tt_timer);
	uv_timer_start(carg->tt_timer, timer_callback, carg->timeout, 0);
}
