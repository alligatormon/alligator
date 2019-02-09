#pragma once

typedef struct http_reply_data
{
	int http_version;
	int http_code;
	char *mesg;
	char *headers;
	int64_t content_size;
	char *body;
} http_reply_data;

http_reply_data* http_reply_parser(char *http, size_t n);
