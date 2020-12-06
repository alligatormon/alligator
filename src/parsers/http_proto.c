#define _GNU_SOURCE
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include "parsers/http_proto.h"
#include "common/http.h"
#include "events/context_arg.h"
#include "main.h"

void http_reply_free(http_reply_data* hrdata)
{
	free(hrdata->mesg);
	free(hrdata->headers);
	free(hrdata);
}

http_reply_data* http_reply_parser(char *http, ssize_t n)
{
	//printf("============== parse body(%zd) ==============\n'%s'\n", n, http);
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
	{
		if (ac->log_level > 3)
			printf("1DO NOT HTTP RESPONSE: %s\n", http);
		return NULL;
	}

	char *cur = http +8;
	while ( cur && ((*cur == ' ') || (*cur == '\t')) )
		cur++;

	http_code = atoi(cur);
	if (!http_code || http_code > 504)
	{
		if (ac->log_level > 3)
			printf("2DO NOT HTTP RESPONSE: %s\n", http);
		return NULL;
	}

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
	tmp = strstr(cur, "\r\n\r\n");
	old_style_newline = 4;
	if (!tmp)
	{
		tmp = strstr(cur, "\n\n");
		old_style_newline = 2;
		if (!tmp)
		{
			//printf("3DO NOT HTTP RESPONSE: '%s'\nCUR\n'%s'\n", http, cur);
			//return NULL;
			tmp = http + n - 4;
		}
	}
	size_t headers_len = tmp - cur;
	headers = strndup(cur, headers_len);

	http_reply_data *hrdata = malloc(sizeof(*hrdata));
	hrdata->content_length = 0;
	hrdata->chunked_size = 0;
	hrdata->chunked_expect = 0;
	int64_t i;
	for (i=0; i<headers_len; ++i)
	{
		if(!strncasecmp(headers+i, "Content-Length:", 15))
		{
			hrdata->content_length = atoll(headers+i+15+1);
		}
		else if(!strncasecmp(headers+i, "Transfer-Encoding:", 18))
		{	
			if (strstr(headers+i+18, "chunked"))
				hrdata->chunked_expect = 1;
		}
	}

	cur = tmp + old_style_newline;
	body = cur;
	hrdata->headers_size = body - http;

	hrdata->http_version = http_version;
	hrdata->http_code = http_code;
	hrdata->mesg = mesg;
	hrdata->headers = headers;
	hrdata->auth_bearer = 0;
	hrdata->auth_basic = 0;

	hrdata->clear_http = string_init(n);
	//printf("init %zd, inited %zu\n", n, hrdata->clear_http->l);
	//printf("headers size: %zu\n", hrdata->headers_size);
	string_cat(hrdata->clear_http, http, hrdata->headers_size);
	//printf("cated %zu\n", hrdata->clear_http->l);
	//printf("clear headers:\n'%s'\n", hrdata->clear_http->s);
	//puts("===================================================");
	uint64_t offset = 0;
	if (hrdata->chunked_expect)
	{
		//puts("=====================chunked======================");
		char *tmp2;
		hrdata->chunked_size = strtoll(body, &tmp2, 16);
		tmp2 += strspn(tmp2, "\r\n");
		//printf("chunked %p -> %p:\n'%s'\n", body, tmp2, body);
		offset = tmp2 - body;
		body = tmp2;
		hrdata->body = body;
	}
	else
		hrdata->body = body;

	hrdata->body_size = n - hrdata->headers_size - offset;
	//size_t hrdata_body_size = strlen(hrdata->body);
	size_t hrdata_body_size = n - (hrdata->body - http);

	if (hrdata->chunked_expect)
	{
		// need for loop as in calc chunk
		//printf("1string_cat clear htt %"d64"\n", hrdata->chunked_size);
		string_cat(hrdata->clear_http, hrdata->body, hrdata->chunked_size);
		//printf("hrdata->chunked_size %"d64" -= %"d64"\n", hrdata->chunked_size, hrdata_body_size);
		hrdata->chunked_size -= hrdata_body_size;
	}
	else
	{
		//printf("2string_cat clear htt %"d64"\n", hrdata->chunked_size);
		string_cat(hrdata->clear_http, hrdata->body, hrdata_body_size);
	}
	//printf("cated2 %zu, body_size %zu\n", hrdata->clear_http->l, hrdata_body_size);
	//puts("+++++++++++++++++++++++++++++++++++++++++++++++++++");
	//printf("SAVED (chunked expect %d) body is\n'%s'\n", hrdata->chunked_expect, hrdata->body);
	//puts("===================================================");
	//puts("");
	return hrdata;
}

void http_proto_handler(char *metrics, size_t size, context_arg *carg)
{
	http_reply_data *hrdata = http_reply_parser(metrics, size);
	if (!hrdata)
		return;

	uint64_t count = 1;
	//printf("version=%d\ncode=%d\nmesg='%s'\nheaders='%p'\nbody='%p', content-length: %d, chunked: %d, headers size: %zu, body size: %zu\n", hrdata->http_version, hrdata->http_code, hrdata->mesg, hrdata->headers, hrdata->body, hrdata->content_length, hrdata->chunked_expect, hrdata->headers_size, hrdata->body_size);

	char code[5];
	snprintf(code, 5, "%"PRId16"", hrdata->http_code);

	metric_update_labels3("aggregator_http_request", &count, DATATYPE_UINT, carg, "code", code, "destination", carg->host, "port", carg->port);
	metric_add_labels2("aggregator_http_headers_size", &hrdata->headers_size, DATATYPE_UINT, carg, "destination", carg->host, "port", carg->port);
	metric_add_labels2("aggregator_http_body_size", &hrdata->body_size, DATATYPE_UINT, carg, "destination", carg->host, "port", carg->port);
	http_reply_free(hrdata);
}

char* http_proto_proxer(char *metrics, size_t size, char *instance)
{
	http_reply_data *hrdata = http_reply_parser(metrics, size);
	// not http answer
	if (!hrdata)
		return metrics;

	//printf("version=%d\ncode=%d\nmesg='%s'\nheaders='%s'\nbody='%s'\n", hrdata->http_version, hrdata->http_code, hrdata->mesg, hrdata->headers, hrdata->body);
	char *body = hrdata->body;
	http_reply_free(hrdata);
	return body;
}

void http_get_auth_data(http_reply_data *hr_data)
{
	hr_data->auth_bearer = 0;
	hr_data->auth_basic = 0;
	char *ptr = strcasestr(hr_data->headers, "Authorization");
	if (ptr)
	{
		ptr += strcspn(ptr, ": \t");
		ptr += strspn(ptr, ": \t");
		if (!strncasecmp(ptr, "Basic", 5))
		{
			ptr += strcspn(ptr, " \t");
			ptr += strspn(ptr, " \t");
			uint8_t size = strcspn(ptr, "\r\n");
			hr_data->auth_basic = strndup(ptr, size);
			//printf("http auth basic '%s'\n", hr_data->auth_basic);
		}
		else if (!strncasecmp(ptr, "Bearer", 6))
		{
			ptr += strcspn(ptr, " \t");
			ptr += strspn(ptr, " \t");
			uint8_t size = strcspn(ptr, "\r\n");
			hr_data->auth_bearer = strndup(ptr, size);
			//printf("http auth bearer '%s'\n", hr_data->auth_bearer);
		}
	}
}

http_reply_data* http_proto_get_request_data(char *buf, size_t size)
{
	uint64_t i;
	int8_t skip = 0;
	uint8_t method;

	if ( !strncmp(buf, "POST", 4) )
	{
		skip = 4;
		method = HTTP_METHOD_POST;
	}
	else if ( !strncmp(buf, "HTTP/", 5) )
	{
		http_reply_data* ret = calloc(1, sizeof(*ret));
		ret->method = HTTP_METHOD_RESPONSE;
		return ret;
	}
	else if ( !strncmp(buf, "GET", 3) )
	{
		skip = 3;
		method = HTTP_METHOD_GET;
	}
	else if ( !strncmp(buf, "PUT", 3) )
	{
		skip = 3;
		method = HTTP_METHOD_PUT;
	}
	else if ( !strncmp(buf, "DELETE", 6) )
	{
		skip = 6;
		method = HTTP_METHOD_DELETE;
	}
	else	return 0;


	http_reply_data* ret = malloc(sizeof(*ret));
	ret->method = method;
	ret->http_code = 0;
	ret->mesg = 0;
	ret->headers = 0;
	ret->content_length = 0;
	ret->expire = -1;

	for (i=skip; i<size && buf[i]==' '; i++); // skip spaces after GET

	size_t uri_size = ret->uri_size = strcspn(buf+i, " \t\r\n");
	ret->uri = strndup(buf+i, uri_size);

	for (i = i + uri_size + 1; i<size && buf[i]==' '; i++); // skip spaces after URI

	skip = 8;
	if (!(strncasecmp(buf+i, "HTTP/1.0", 8)))
		ret->http_version = 10;
	else if (!(strncasecmp(buf+i, "HTTP/1.1", 8)))
		ret->http_version = 11;
	else if (!(strncasecmp(buf+i, "HTTP/2.0", 8)))
		ret->http_version = 20;
	else
	{
		skip = 0;
		ret->http_version = 0;
	}

	i+= skip;
	ret->body = strstr(buf+i, "\r\n\r\n");
	ret->body_size = 0;
	ret->headers_size = ret->body - (buf + i);
	ret->headers = strndup(buf + i, ret->headers_size);

	if (ret->body)
	{
		ret->body += 4;
		ret->body_size = size - (ret->body - buf);
	}

	for (i=0; i<ret->headers_size; i++)
	{
		if(!strncasecmp(ret->headers+i, "X-Expire-Time", 13))
		{
			ret->expire = strtoll(ret->headers+i+13+1, NULL, 10);
		}
		i += strcspn(ret->headers+i, "\n");
	}

	http_get_auth_data(ret);

	return ret;
}

uint8_t http_check_auth(context_arg *carg, http_reply_data *http_data)
{
	extern aconf *ac;
	if (ac->log_level > 3)
		printf("http auth: basic:%d bearer %d\n", carg->auth_basic ? 1:0, carg->auth_bearer ? 1:0);

	if (!carg)
		return 1;

	if (!carg->auth_basic && !carg->auth_bearer)
		return 1;

	if (carg->auth_basic && http_data->auth_basic)
		if (!strcmp(carg->auth_basic, http_data->auth_basic))
			return 1;

	if (carg->auth_bearer && http_data->auth_bearer)
		if (!strcmp(carg->auth_bearer, http_data->auth_bearer))
			return 1;

	return 0;
}

void http_reply_data_free(http_reply_data* http)
{
	if (http->uri)
		free(http->uri);
	if (http->mesg)
		free(http->mesg);
	if (http->headers)
		free(http->headers);
	if (http->auth_bearer)
		free(http->auth_bearer);
	if (http->auth_basic)
		free(http->auth_basic);

	free(http);
}


string* http_mesg(host_aggregator_info *hi, void *arg)
{
	return string_init_add(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1, NULL), 0, 0);
}

void http_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("http");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = http_proto_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = http_mesg;
	actx->handler[0].headers_pass = 1;
	strlcpy(actx->handler[0].key,"http", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
