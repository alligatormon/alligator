#pragma once
#include "common/selector.h"
#include "dstructures/tommy.h"

typedef struct http_headers
{
	char *key;
	char *value;
} http_headers;

typedef struct http_arg
{
	char *key;
	char *value;

	alligator_ht_node node;
} http_arg;

char* gen_http_query(int http_type, char *method_query, char *append_query, char *host, char *useragent, char *auth, int clrf, char *httpver, void *env_arg, void *proxy_settings, string *body);
alligator_ht* http_get_args(char *str, size_t size);
int http_arg_compare(const void* arg, const void* obj);
#define HTTP_GET 0
#define HTTP_POST 1
