#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "main.h"
char* gen_http_query(int http_type, char *method_query, char *append_query, char *host, char *useragent, char *auth, int clrf)
{
	//printf("%d, %s, %s, %s, %s, %s, %d\n", http_type, method_query, append_query, host, useragent, auth, clrf);
	size_t method_query_size = 0;
	size_t append_query_size = 0;
	if (method_query)
		method_query_size = strlen(method_query);
	if (append_query)
		append_query_size = strlen(append_query);

	char *query = malloc(method_query_size + append_query_size + 1);
	*query = 0;
	if (method_query && method_query_size > 1)
		strcat(query, method_query);
	if (append_query)
		strcat(query, append_query);

	size_t size_useragent, size_auth;

	size_t size_query = strlen(query);
	if ( !size_query )
	{
			size_query = 1;
			free(query);
			query = strdup("/");
	}

	if ( useragent )	size_useragent = strlen(useragent);
	else			size_useragent = 0;
	if ( auth )		size_auth = strlen(auth);
	else			size_auth = 0;

	size_t sz = size_query + size_useragent + size_auth + 1000; 

	char *buf = malloc(sz+1);
	char method[5];
	switch (http_type)
	{
		default: strlcpy(method, "GET", 4);
	}

	if (clrf)
	{
		if (auth)	snprintf(buf, sz, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAuthorization: Basic %s\r\n\r\n", method, query, host, useragent, auth);
		else		snprintf(buf, sz, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n", method, query, host, useragent);
	}
	else
	{
		if (auth)	snprintf(buf, sz, "%s %s HTTP/1.1\nHost: %s\nUser-Agent: %s\nAuthorization: Basic %s\n\n", method, query, host, useragent, auth);
		else		snprintf(buf, sz, "%s %s HTTP/1.1\nHost: %s\nUser-Agent: %s\n\n", method, query, host, useragent);
	}

	return buf;
}
