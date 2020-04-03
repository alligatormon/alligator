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
	int8_t proto;
	int8_t transport;
} host_aggregator_info;

host_aggregator_info *parse_url (char *str, size_t len);
