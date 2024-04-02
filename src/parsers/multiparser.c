#include <stdio.h>
#include <string.h>
#include "main.h"
#include "parsers/http_proto.h"
#include "parsers/multiparser.h"
#include "common/selector.h"
#include "common/http.h"
#include "router/router.h"
#define READY_HANDLER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n"
#define HANDLER_202 "HTTP/1.1 202 Accepted\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n"
#define OPTIONS_ANSWER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\nAllow: OPTIONS, GET, PUT, POST\r\n"
#define METHOD_NOT_ALLOWED "HTTP/1.1 405 Method Not Allowed\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n"
#define COMMON_ANSWER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define HTTP_FORBIDDEN "HTTP/1.1 403 Access Forbidden\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"

void do_http_post(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	char *body = http_data->body;
	char *uri = http_data->uri;
	extern aconf *ac;

	if (!strncmp(http_data->uri, "/api", 4))
	{
		api_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/oplog", 6))
	{
		oplog_post_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/sharedlock", 11))
	{
		sharedlock_post_router(response, http_data, carg);
	}
	else
	{
		if (ac->log_level > 2)
			printf("Query: %s\n", uri);
		if (ac->log_level > 10)
			printf("get metrics from body:\n%s\n", body);
		multicollector(http_data, NULL, 0, carg);
		string_cat(response, HANDLER_202, strlen(HANDLER_202));

		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		string_cat(response, "\r\n", 2);
	}
}

void do_http_options(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	extern aconf *ac;

	string_cat(response, OPTIONS_ANSWER, strlen(OPTIONS_ANSWER));
	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
	string_cat(response, "\r\n", 2);
}

void do_http_not_allowed(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	extern aconf *ac;

	string_cat(response, METHOD_NOT_ALLOWED, strlen(METHOD_NOT_ALLOWED));
	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
	string_cat(response, "\r\n", 2);
}

void do_http_delete(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	extern aconf *ac;

	if (!strncmp(http_data->uri, "/api", 4))
		api_router(response, http_data, carg);
	else
		string_cat(response, "HTTP/1.1 404 Not Found\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"));
}

void do_http_get(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	if (!strncmp(http_data->uri, "/api", 4))
	{
		api_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/stats", 6))
	{
		stat_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/probe", 6))
	{
		probe_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/json", 5))
	{
		json_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/dsv", 4))
	{
		dsv_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/conf", 4))
	{
		conf_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/labels_cache", 13))
	{
		labels_cache_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/oplog", 6))
	{
		oplog_get_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/sharedlock", 11))
	{
		sharedlock_get_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/resolver", 9))
	{
		resolver_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/ready", 6))
	{
		string_cat(response, READY_HANDLER, strlen(READY_HANDLER));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		string_cat(response, "\r\n", 2);
	}
	else
	{
		//metric_str_build(0, response);
		string *body = string_init(response->m);
		metric_str_build(0, body);

		char *content_length = malloc(255);
		snprintf(content_length, 255, "Content-Length: %zu\r\n\r\n", body->l);

		string_cat(response, COMMON_ANSWER, strlen(COMMON_ANSWER));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		string_cat(response, content_length, strlen(content_length));
		string_cat(response, body->s, body->l);

		free(content_length);
		string_free(body);
	}

	if (carg->rreturn == ENTRYPOINT_RETURN_EMPTY)
	{
		string_null(response);
		string_cat(response, READY_HANDLER, strlen(READY_HANDLER));
	}
}

void do_http_put(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	do_http_post(buf, len, response, http_data, carg);
	//string_cat(response, "HTTP/1.1 400 Bad Query\n\n", strlen("HTTP/1.1 400 Bad Query\n\n")+1);
}

void do_http_response(char *buf, size_t len, string *response, context_arg *carg, http_reply_data* http_data)
{
	(void)response;
	char *tmp;
	if ( (tmp = strstr(buf, "\n\n")) )
		multicollector(http_data, tmp+2, len - (tmp-buf) - 2, carg);
	else if ( (tmp = strstr(buf, "\r\n\r\n")) )
		multicollector(http_data, tmp+4, len - (tmp-buf) - 4, carg);
}

int http_parser(char *buf, size_t len, string *response, context_arg *carg)
{
	if (!response)
		return 0;

	int ret = 1;

	http_reply_data* http_data = http_proto_get_request_data(buf, len, carg->auth_header);
	if(!http_data)
		return 0;

	if (!http_check_auth(carg, http_data) && http_data->method != HTTP_METHOD_OPTIONS)
	{
		string_cat(response, HTTP_FORBIDDEN, strlen(HTTP_FORBIDDEN));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		string_cat(response, "\r\n", 2);
		return 1;
	}

	if (http_data->method == HTTP_METHOD_POST)
	{
		if (http_data->body)
			do_http_post(buf, len, response, http_data, carg);
	}
	else if (http_data->method == HTTP_METHOD_RESPONSE)
	{
		do_http_response(buf, len, response, carg, http_data);
	}
	else if (http_data->method == HTTP_METHOD_GET)
	{
		do_http_get(buf, len, response, http_data, carg);
	}
	else if (http_data->method == HTTP_METHOD_PUT)
	{
		if (http_data->body)
	 		do_http_put(buf, len, response, http_data, carg);
	}
	else if (http_data->method == HTTP_METHOD_DELETE)
	{
		if (http_data->body)
	 		do_http_delete(buf, len, response, http_data, carg);
	}
	else if (http_data->method == HTTP_METHOD_OPTIONS)
	{
	    do_http_options(buf, len, response, http_data, carg);
	}
	else if ((http_data->method == HTTP_METHOD_HEAD) ||
		(http_data->method == HTTP_METHOD_TRACE) ||
		(http_data->method == HTTP_METHOD_PATCH))

	{
        do_http_not_allowed(buf, len, response, http_data, carg);
	}
	else	ret = 0;

	http_reply_data_free(http_data);
	return ret;
}

int plain_parser(char *buf, size_t len, context_arg *carg)
{
	multicollector(NULL, buf, len, carg);
	return 1;
}

void alligator_multiparser(char *buf, size_t slen, void (*handler)(char*, size_t, context_arg*), string *response, context_arg *carg)
{
	if (carg && carg->log_level > 99)
	{
		puts("========================================================================");
		printf("handler (%p) parsing (%zu):\n'%s'\n", handler, slen, buf);
		puts("========================================================================");
	}
	if (!buf)
		return;

	size_t len = slen;

	if (carg)
	{
		carg->response = response;
		carg->parsed = 1;
		carg->parser_status = 0;
		carg->exec_time = setrtime();
	}
	
	if ( handler )
	{
		char *proxybuf;
		uint64_t proxylen;
		if (carg && !carg->headers_pass && carg->is_http_query)
		{
			proxybuf = strstr(buf + carg->headers_size, "\r\n\r\n");
			if (!proxybuf)
				proxybuf = buf + carg->headers_size;

			proxybuf += strspn(proxybuf, "\r\n");
			proxylen = len - (proxybuf - buf);
		}
		else
		{
			proxybuf = buf;
			proxylen = len;
		}
		//printf(">>>>>>>>>>>>>>>\n%zu\n'%s'\n<<<<<<<<<<<<<<<<<<\n", proxylen, proxybuf);
		if (proxybuf)
			handler(proxybuf, proxylen, carg);

		if (carg)
			carg->exec_time_finish = setrtime();

		return;
	}
	int rc = 0;
	if ( (rc = http_parser(buf, len, response, carg)) ) {}
	else if ( (rc = plain_parser(buf, len, carg)) ) {}

	if (carg)
		carg->exec_time_finish = setrtime();
}

string* tcp_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if (hi->query)
		return string_init_add(strdup(hi->query), 0, 0);
	else
		return NULL;
}

void tcp_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("tcp");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = tcp_mesg;
	strlcpy(actx->handler[0].key,"tcp", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

string* blackbox_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add(gen_http_query(0, hi->query, "", hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings, NULL), 0, 0);
	else if (hi->query)
		return string_init_alloc(hi->query, 0);
	else
		return NULL;
}

void blackbox_null(char *metrics, size_t size, context_arg *carg)
{
	carg->parser_status = 1;
}

void blackbox_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("blackbox");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = blackbox_null;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = blackbox_mesg;
	actx->handler[0].headers_pass = 0;
	strlcpy(actx->handler[0].key,"blackbox", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void http_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("http");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = blackbox_null;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = blackbox_mesg;
	actx->handler[0].headers_pass = 0;
	strlcpy(actx->handler[0].key,"http", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void process_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("process");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "process", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
