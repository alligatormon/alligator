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

char* gen_http_query(int http_type, char *method_query, char *append_query, char *host, char *useragent, char *auth, char *httpver, void *env_arg, void *proxy_settings, string *body);
alligator_ht* http_get_args(char *str, size_t size);
int http_arg_compare(const void* arg, const void* obj);
char *http_get_param(alligator_ht *args, char *key);
uint64_t urlencode(char* dest, char* src, size_t src_len);
void http_args_free(alligator_ht *arg);
#define HTTP_GET 0
#define HTTP_POST 1
