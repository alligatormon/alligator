#pragma once
typedef struct http_headers
{
	char *key;
	char *value;
} http_headers;
char* gen_http_query(int http_type, char *method_query, char *append_query, char *host, char *useragent, char *auth, int clrf, char *httpver, void *env_arg, void *proxy_settings);
