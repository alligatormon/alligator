#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "parsers/http_proto.h"

http_reply_data* http_reply_parser(char *http, size_t n)
{
	int http_version;
	int http_code;
	int old_style_newline = 2;
	char *mesg;
	char *headers;
	char *body;
	size_t sz;

	if (!(strncasecmp(http, "HTTP/1.0", 8)))
		http_version = 10;
	else if (!(strncasecmp(http, "HTTP/1.1", 8)))
		http_version = 11;
	else if (!(strncasecmp(http, "HTTP/2.0", 8)))
		http_version = 20;
	else
		return NULL;

	char *cur = http +8;
	while ( cur && ((*cur == ' ') || (*cur == '\t')) )
		cur++;

	http_code = atoi(cur);
	if (!http_code || http_code > 504)
		return NULL;

	while (cur && (isdigit(*cur)))
		cur++;
	while ( cur && ((*cur == ' ') || (*cur == '\t')) )
		cur++;

	sz = strcspn(cur, "\r\n\0");
	mesg = strndup(cur, sz);

	cur += sz+1;
	if (cur && *cur == '\n')
		cur++;

	char *tmp;
	tmp = strstr(cur, "\n\n");
	if (!tmp)
	{
		tmp = strstr(cur, "\r\n\r\n");
		if (!tmp)
			return NULL;
		old_style_newline = 4;
	}
	headers = strndup(cur, tmp-cur);

	cur = tmp + old_style_newline;
	body = cur;

	http_reply_data *hrdata = malloc(sizeof(*hrdata));
	hrdata->http_version = http_version;
	hrdata->http_code = http_code;
	hrdata->mesg = mesg;
	hrdata->headers = headers;
	hrdata->body = body;

	return hrdata;
}

void http_proto_handler(char *metrics, size_t size, char *instance, int kind) {
	printf("HTTPPROTO: '%s'\n", metrics);
	http_reply_data *hrdata = http_reply_parser(metrics, size);
	if (!hrdata)
		return;

	printf("version=%d\ncode=%d\nmesg='%s'\nheaders='%s'\nbody='%s'\n", hrdata->http_version, hrdata->http_code, hrdata->mesg, hrdata->headers, hrdata->body);
}
