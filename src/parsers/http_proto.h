#pragma once
#define HTTP_METHOD_RESPONSE 0
#define HTTP_METHOD_GET 1
#define HTTP_METHOD_POST 2
#define HTTP_METHOD_PUT 3

typedef struct http_reply_data
{
	int http_version;
	int http_code;
	char *mesg;
	char *headers;
	char *uri;
	size_t uri_size;
	int64_t content_length;
	char *body;
	size_t body_size;
	uint8_t method;
} http_reply_data;

http_reply_data* http_reply_parser(char *http, size_t n);
http_reply_data* http_proto_get_request_data(char *buf, size_t size);
void http_reply_data_free(http_reply_data* http);
