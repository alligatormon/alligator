#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "common/url.h"
void put_aggregate_config(mtlen *mt)
{
	int64_t i;
	for (i=0;i<mt->m;i++)
	{
		printf("%"d64": '%s'(%zu)\n", i, mt->st[i].s, mt->st[i].l);
	}
	host_aggregator_info *hi = parse_url(mt->st[i].s, mt->st[i].l);
	do_tcp_client(hi->host, hi->port, NULL, "GET /metrics HTTP/1.1\nHost: test\n\n", APROTO_HTTP);
}
