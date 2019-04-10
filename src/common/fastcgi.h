#pragma once
typedef struct fcgi_reply_data
{
	int64_t content_length;
	char *body;
} fcgi_reply_data;

uv_buf_t* gen_fcgi_query(char *method_query, char *host, char *useragent, char *auth, size_t *num);
fcgi_reply_data* fcgi_reply_parser(char *fcgi, size_t n);
