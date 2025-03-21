#pragma once
#define APROTO_FILE 0
#define APROTO_HTTP 1
#define APROTO_UNIX 3
#define APROTO_UNIXGRAM 4
#define APROTO_TCP 5
#define APROTO_UDP 6
#define APROTO_PROCESS 7
#define APROTO_FCGI 8
#define APROTO_UNIXFCGI 10
#define APROTO_HTTPS 11
#define APROTO_TLS 13
#define APROTO_DTLS 14
#define APROTO_ICMP 15
#define APROTO_PG 16
#define APROTO_MY 17
#define APROTO_MONGODB 18
#define APROTO_ZKCONF 19
#define APROTO_RESOLVER 20
#define APROTO_CASSANDR 21
#include <stdio.h>
typedef struct host_aggregator_info
{
	char port[6];
	char *host;
	char *host_header;
	char *query;
	char *auth;
	char *user;
	char *pass;
	char *url;
	char *transport_string;
	int8_t proto;
	int8_t transport;
	int8_t tls;
} host_aggregator_info;

host_aggregator_info *parse_url (char *str, size_t len);
void url_free(host_aggregator_info *hi);
