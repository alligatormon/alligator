#pragma once
#include <stdio.h>
#include <inttypes.h>
#define HTTP_METHOD_RESPONSE 0
#define HTTP_METHOD_GET 1
#define HTTP_METHOD_POST 2
#define HTTP_METHOD_PUT 3
#define HTTP_METHOD_DELETE 4

typedef struct http_reply_data
{
	int http_version;
	uint16_t http_code;
	char *mesg;
	char *headers;
	char *uri;
	size_t uri_size;
	int64_t content_length;
	int64_t chunked_size;
	int64_t expire;
	int8_t chunked_expect;
	char *body;
	size_t body_size;
	size_t headers_size;
	uint8_t method;

	char *auth_basic;
	char *auth_bearer;
} http_reply_data;

http_reply_data* http_reply_parser(char *http, size_t n);
http_reply_data* http_proto_get_request_data(char *buf, size_t size);
void http_reply_data_free(http_reply_data* http);
char* http_proto_proxer(char *metrics, size_t size, char *instance);
