#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "main.h"
char* gen_http_query(int http_type, char *method_query, char *host, char *useragent, char *auth, int clrf)
{
	size_t size_useragent, size_auth, size_method_query;
	if ( method_query )
	{
		size_method_query = strlen(method_query);
		if ( !size_method_query )
		{
				size_method_query = 1;
				free(method_query);
				method_query = strdup("/");
		}
	}
	else
	{
				size_method_query = 1;
				method_query = strdup("/");
	}
	if ( useragent )	size_useragent = strlen(useragent);
	else			size_useragent = 0;
	if ( auth )		size_auth = strlen(auth);
	else			size_auth = 0;

	size_t sz = size_method_query + size_useragent + size_auth + 1000; 

	char *buf = malloc(sz);
	char method[5];
	switch (http_type)
	{
		default: strlcpy(method, "GET", 4);
	}

	if (clrf)
	{
		if (auth)	snprintf(buf, sz, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAuthorization: Basic %s\r\n\r\n", method, method_query, host, useragent, auth);
		else		snprintf(buf, sz, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n", method, method_query, host, useragent);
	}
	else
	{
		if (auth)	snprintf(buf, sz, "%s %s HTTP/1.1\nHost: %s\nUser-Agent: %s\nAuthorization: Basic %s\n\n", method, method_query, host, useragent, auth);
		else		snprintf(buf, sz, "%s %s HTTP/1.1\nHost: %s\nUser-Agent: %s\n\n", method, method_query, host, useragent);
	}

	return buf;
}
