#pragma once
#define APROTO_FILE 0
#define APROTO_HTTP 1
#define APROTO_HTTP_AUTH 2
#define APROTO_UNIX 3
#define APROTO_UNIXGRAM 4
#define APROTO_TCP 5
#define APROTO_UDP 6
typedef struct host_aggregator_info
{
	char port[6];
	char *host;
	char *query;
	char *auth;
	int proto;
} host_aggregator_info;

host_aggregator_info *parse_url (char *str, size_t len);
