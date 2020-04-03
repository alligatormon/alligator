#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "url.h"
#include "base64.h"
#include "main.h"
void url_set_proto(host_aggregator_info *hi, char **tmp, char *match, size_t size, int8_t proto, int8_t transport)
{
	if (!strncmp(*tmp, match, size))
	{
		hi->proto = proto;
		hi->transport = transport;
		*tmp += size;
	}
}

void url_get_unix_path(host_aggregator_info *hi, char **tmp)
{
	char *end = strstr(*tmp, ":");
	hi->host = strndup(*tmp, end-*tmp);

	*tmp = end+1;
}

void url_get_auth_data(host_aggregator_info *hi, char **tmp)
{
	char *end = strstr(*tmp, "@");
	if (!end)
		return;

	char *delim = strstr(*tmp, ":");
	if (delim > end)
		return;

	size_t sz;
	hi->user = strndup(*tmp, delim-*tmp);
	hi->pass = strndup(delim+1, end-delim);
	hi->auth = base64_encode(*tmp, end-*tmp, &sz);

	*tmp = end+1;
}

// return symbol indicator after *tmp
// 0 == end of url
// 1 == : (port indication)
// 2 == / (path indication)
// 3 == ? (args indication)
int8_t url_get_hostname(host_aggregator_info *hi, char **tmp)
{
	char *slash = strstr(*tmp, "/");
	char *colon = strstr(*tmp, ":");
	char *params = strstr(*tmp, "?");
	int8_t rc = 0;

	if ((hi->proto == APROTO_FILE) || (hi->proto == APROTO_PROCESS))
	{
		hi->host_header = strdup(*tmp);
		*tmp = NULL;
	}
	else if (!slash && !colon && !params)
	{
		hi->host_header = strdup(*tmp);
		*tmp = NULL;
	}
	else if (!slash && !colon && params)
	{
		hi->host_header = strndup(*tmp, params-*tmp);
		*tmp = params;
		rc = 3;
	}
	else if (!colon)
	{
		hi->host_header = strndup(*tmp, slash-*tmp);
		*tmp = slash;
		rc = 2;
	}
	else if (!slash)
	{
		hi->host_header = strndup(*tmp, colon-*tmp);
		*tmp = colon+1;
		rc = 1;
	}
	else if (colon > slash)
	{
		hi->host_header = strndup(*tmp, slash-*tmp);
		*tmp = slash;
		rc = 2;
	}
	else if (colon < slash)
	{
		hi->host_header = strndup(*tmp, colon-*tmp);
		*tmp = colon+1;
		rc = 1;
	}

	if (!hi->host)
		hi->host = strdup(hi->host_header);

	return rc;
}

void url_set_default_port(host_aggregator_info *hi)
{
	if (hi->proto == APROTO_HTTP)
		strlcpy(hi->port, "80", 3);
	if (hi->proto == APROTO_HTTPS)
		strlcpy(hi->port, "443", 4);
}

void url_get_port(host_aggregator_info *hi, char **tmp)
{
	int8_t portlen = strspn(*tmp, "1234567890");
	strlcpy(hi->port, *tmp, portlen+1);
	*tmp += portlen;
}

void url_get_query(host_aggregator_info *hi, char *tmp)
{
	printf("url_get_query: tmp %s, hi->proto %d\n", tmp, hi->proto);
	if (!tmp || !*tmp)
	{
		if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
			hi->query = strdup("/");
		else
			hi->query = calloc(1, 1);
	}
	else
	{
		if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
			hi->query = strdup(tmp);
		else
			hi->query = strdup(tmp+1);
	}
}

void url_dump(host_aggregator_info *hi)
{
	extern aconf *ac;
	if (!hi)
		return;

	if (ac && ac->log_level > 1)
	{
		printf("===url dump===\n");
		if (hi->transport == APROTO_DTLS)
			puts("transport: DTLS");
		if (hi->transport == APROTO_TLS)
			puts("transport: TLS");
		if (hi->transport == APROTO_TCP)
			puts("transport: TCP");
		if (hi->transport == APROTO_UDP)
			puts("transport: UDP");
		if (hi->transport == APROTO_PROCESS)
			puts("transport: run process");
		if (hi->transport == APROTO_FILE)
			puts("transport: open file");
		if (hi->transport == APROTO_ICMP)
			puts("transport: icmp");

		if (hi->proto == APROTO_DTLS)
			puts("proto: DTLS");
		if (hi->proto == APROTO_TLS)
			puts("proto: TLS");
		if (hi->proto == APROTO_TCP)
			puts("proto: TCP");
		if (hi->proto == APROTO_UDP)
			puts("proto: UDP");
		if (hi->proto == APROTO_PROCESS)
			puts("proto: run process");
		if (hi->proto == APROTO_FILE)
			puts("proto: open file");
		if (hi->proto == APROTO_ICMP)
			puts("proto: icmp");
		if (hi->proto == APROTO_FCGI)
			puts("proto: fastcgi");
		if (hi->proto == APROTO_HTTP)
			puts("proto: http");
		if (hi->proto == APROTO_HTTPS)
			puts("proto: https");


		if (hi->port)
			printf("port: %s\n", hi->port);
		if (hi->host)
			printf("address: %s\n", hi->host);
		if (hi->host_header)
			printf("header 'Host': %s\n", hi->host_header);
		if (hi->query)
			printf("query: %s\n", hi->query);
		if (hi->auth)
			puts("auth enable");
	}
}

// http://example.com
// http://unix:/var/run/run.sock:example.com/
// https://user:password@example.com:443/testr?12=sdcsa:av
// tcp://:password@example.com
host_aggregator_info *parse_url(char *str, size_t len)
{
	host_aggregator_info *hi = calloc(1, sizeof(*hi));
	hi->auth = 0;
	char *tmp = str;
	url_set_proto(hi, &tmp, "http://unix:/", 12, APROTO_HTTP, APROTO_UNIX);
	url_set_proto(hi, &tmp, "https://unix:/", 13, APROTO_HTTPS, APROTO_UNIX);
	url_set_proto(hi, &tmp, "fastcgi://unix:/", 15, APROTO_FCGI, APROTO_UNIX);
	url_set_proto(hi, &tmp, "tcp://unix:/", 11, APROTO_TCP, APROTO_UNIX);
	url_set_proto(hi, &tmp, "tls://unix:/", 11, APROTO_TLS, APROTO_UNIX);
	url_set_proto(hi, &tmp, "udp://unix:/", 11, APROTO_UDP, APROTO_UNIX);
	url_set_proto(hi, &tmp, "dtls://unix:/", 12, APROTO_DTLS, APROTO_UNIX);
	url_set_proto(hi, &tmp, "unix://", 7, APROTO_TCP, APROTO_UNIX);
	url_set_proto(hi, &tmp, "unixgram://", 7, APROTO_UDP, APROTO_UNIX);
	url_set_proto(hi, &tmp, "http://", 7, APROTO_HTTP, APROTO_TCP);
	url_set_proto(hi, &tmp, "https://", 8, APROTO_HTTPS, APROTO_TLS);
	url_set_proto(hi, &tmp, "fastcgi://", 10, APROTO_FCGI, APROTO_TCP);
	url_set_proto(hi, &tmp, "tcp://", 6, APROTO_TCP, APROTO_TCP);
	url_set_proto(hi, &tmp, "tls://", 6, APROTO_TLS, APROTO_TCP);
	url_set_proto(hi, &tmp, "udp://", 6, APROTO_UDP, APROTO_UDP);
	url_set_proto(hi, &tmp, "dtls://", 7, APROTO_DTLS, APROTO_UDP);
	url_set_proto(hi, &tmp, "icmp://", 7, APROTO_ICMP, APROTO_ICMP);
	url_set_proto(hi, &tmp, "exec://", 7, APROTO_PROCESS, APROTO_PROCESS);
	url_set_proto(hi, &tmp, "file://", 7, APROTO_FILE, APROTO_FILE);

	url_set_default_port(hi);

	if (hi->transport == APROTO_UNIX)
		url_get_unix_path(hi, &tmp);

	if (tmp == (void*)1)
		return hi;

	url_get_auth_data(hi, &tmp);

	int8_t indicator = url_get_hostname(hi, &tmp);

	if (indicator == 1)
		url_get_port(hi, &tmp);

	url_get_query(hi, tmp);

	url_dump(hi);
	return hi;
}

void url_free(host_aggregator_info *hi)
{
	if (!hi)
		return;
	if (hi->host)
		free(hi->host);
	if (hi->host_header)
		free(hi->host_header);
	if (hi->query)
		free(hi->query);
	if (hi->auth)
		free(hi->auth);
	if (hi->user)
		free(hi->user);
	if (hi->pass)
		free(hi->pass);

	free(hi);
}
