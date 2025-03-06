#pragma once
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "events/context_arg.h"
#define HTTP_METHOD_RESPONSE 0
#define HTTP_METHOD_GET 1
#define HTTP_METHOD_POST 2
#define HTTP_METHOD_PUT 3
#define HTTP_METHOD_DELETE 4
#define HTTP_METHOD_OPTIONS 5
#define HTTP_METHOD_HEAD 6
#define HTTP_METHOD_CONNECT 7
#define HTTP_METHOD_TRACE 8
#define HTTP_METHOD_PATCH 9

typedef struct http_reply_data
{
	int http_version;
	uint16_t http_code;
	char *mesg;
	char *headers;
	char *uri;
	char *location;
	size_t uri_size;
	int64_t content_length;
	int64_t chunked_size;
	int64_t expire;
	int8_t chunked_expect;
	char *body;
	size_t body_size;
	size_t body_offset;
	size_t headers_size;
	uint8_t method;
	string *clear_http;

	char *auth_basic;
	char *auth_bearer;
	char *auth_other;
} http_reply_data;

http_reply_data* http_reply_parser(char *http, ssize_t n);
http_reply_data* http_proto_get_request_data(char *buf, size_t size, char *auth_header);
void http_reply_data_free(http_reply_data* http);
char* http_proto_proxer(char *metrics, size_t size, char *instance);
void env_serialize_http_answer(void *funcarg, void* arg);
void http_hrdata_metrics(context_arg *carg, http_reply_data *hrdata);
void http_null_metrics(context_arg *carg);
void http_follow_redirect(context_arg *carg, http_reply_data *hrdata);
http_reply_data* http_reply_data_clone(http_reply_data* http);
