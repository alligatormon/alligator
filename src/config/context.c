#include <stdio.h>
#include "main.h"
#include "common/url.h"
#include "common/http.h"

void context_aggregate_parser(mtlen *mt, int64_t *i)
{
	if ( *i == 0 )
		*i += 1;

	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		//printf("%"d64": %s\n", *i, mt->st[*i].s);
		if (!strcmp(mt->st[*i-1].s, "prometheus"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_tcp_client(hi->host, hi->port, NULL, "GET /metrics HTTP/1.1\nHost: test\n\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "plain"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_tcp_client(hi->host, hi->port, NULL, "GET /metrics HTTP/1.1\nHost: test\n\n");
		}
#ifndef _WIN64
#ifndef __APPLE__
		else if (!strcmp(mt->st[*i-1].s, "icmp"))
		{
			do_icmp_client(mt->st[*i].s);
		}
#endif
		else if (!strcmp(mt->st[*i-1].s, "http"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, hi->host, "alligator", hi->auth);
			printf("query %s, host %s, port %s\n", query, hi->host, hi->port);
			do_tcp_client(hi->host, hi->port, http_proto_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "exec"))
		{
			put_to_loop_cmd(mt->st[*i].s, NULL);
		}
		else if (!strcmp(mt->st[*i-1].s, "unix"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_unix_client(hi->host, NULL, "");
		}
		else if (!strcmp(mt->st[*i-1].s, "unixgram"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_unixgram_client(hi->host, NULL, "");
		}
		else if (!strcmp(mt->st[*i-1].s, "clickhouse"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, "?query=select%20*\%20from\%20system.metrics", hi->host, "alligator", hi->auth);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "redis"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_tcp_client(hi->host, hi->port, redis_handler, "INFO\n");
		}
#endif
	}
}

void context_entrypoint_parser(mtlen *mt, int64_t *i)
{
	if ( *i == 0 )
		*i += 1;
	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		printf("entrypoint %"d64": %s\n", *i, mt->st[*i].s);
		if (!strncmp(mt->st[*i-1].s, "tcp", 3))
			tcp_server_handler("0.0.0.0", atoi(mt->st[*i].s), NULL);
#ifndef _WIN64
		else if (!strncmp(mt->st[*i-1].s, "udp", 3))
			udp_server_handler("0.0.0.0", atoi(mt->st[*i].s), NULL);
		else if (!strncmp(mt->st[*i-1].s, "unixgram", 8))
			unixgram_server_handler(mt->st[*i].s, NULL);
		else if (!strncmp(mt->st[*i-1].s, "unix", 4))
			unix_server_handler(mt->st[*i].s, NULL);
#endif
	}
}
