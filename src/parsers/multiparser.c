#include <stdio.h>
#include <string.h>
#include "common/https_tls_check.h"
#include "main.h"
#include "parsers/http_proto.h"
#include "common/selector.h"

//void do_http_post(char *buf, size_t len, string *response, http_reply_data* http_data)
//{
//	char *body = http_data->body;
//	char *uri = http_data->uri
//	if ( !( body = strstr(buf, "\n\n") ) )
//	{
//		if ( ( body = strstr(buf, "\r\n\r\n") ) )
//			//printf("body is %s\n", body+4);
//			if ( body && strlen(body) > 4 )
//			{
//				if ( !strncmp(body+4, "certificate_https_check", strlen("certificate_https_check")) )
//				{
//					//puts("add certificate checking:");
//					//https_ssl_check_push(body +5 +strlen("certificate_https_check"));
//				}
//				else
//				{
//					//puts("add metric parser");
//					//selector_get_plain_metrics(body+4, strlen(body+4), "\n", " ", "push_", 5 );
//					printf("\n====0====\npotential get: %s\n", uri);
//					printf("\nget metrics from\n%s\n", body+4);
//					prom_getmetrics_n(body+4, strlen(body+4), uri);
//				}
//			}
//	}
//	else
//	{
//		//printf("body is %s\n", body+2);
//		if ( !strncmp(body+2, "certificate_https_check", strlen("certificate_https_check")) )
//		{
//			//puts("add certificate checking");
//			//https_ssl_check_push(body +3 +strlen("certificate_https_check"));
//		}
//		else
//		{
//			//puts("add metric parser");
//			//selector_get_plain_metrics(body+2, strlen(body+2), "\n", " ", "push_", 5 );
//			printf("\n====1====\npotential get: %s\n", parseget);
//			printf("\nget metrics from\n%s\n", body+2);
//			prom_getmetrics_n(body+2, strlen(body+2), parseget);
//		}
//	}
//	free(parseget);
//	string_cat(response, "HTTP/1.1 202 Accepted\n\n", strlen("HTTP/1.1 202 Accepted\n\n")+1);
//}

void do_http_post(char *buf, size_t len, string *response, http_reply_data* http_data, client_info *cinfo)
{
	char *body = http_data->body;
	char *uri = http_data->uri;
	extern aconf *ac;

	if ( !strncmp(body, "certificate_https_check", strlen("certificate_https_check")) )
	{
		//puts("add certificate checking:");
		//https_ssl_check_push(body +5 +strlen("certificate_https_check"));
	}
	else
	{
		//puts("add metric parser");
		//selector_get_plain_metrics(body+4, strlen(body+4), "\n", " ", "push_", 5 );
		if (ac->log_level > 2)
			printf("Query: %s\n", uri);
		if (ac->log_level > 10)
			printf("get metrics from body:\n%s\n", body);
		multicollector(http_data, cinfo);
	}

	string_cat(response, "HTTP/1.1 202 Accepted\n\n", strlen("HTTP/1.1 202 Accepted\n\n")+1);
}

void do_http_get(char *buf, size_t len, string *response, http_reply_data* http_data)
{
	//if (strstr(buf, "/api"))
	if (!strcmp(http_data->uri, "/api"))
	{
	}
	else
	{
		string_cat(response, "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n", strlen("HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"));
		metric_str_build(0, response);
	}
}

void do_http_put(char *buf, size_t len, string *response, http_reply_data* http_data, client_info *cinfo)
{
	do_http_post(buf, len, response, http_data, cinfo);
	//string_cat(response, "HTTP/1.1 400 Bad Query\n\n", strlen("HTTP/1.1 400 Bad Query\n\n")+1);
}

void do_http_response(char *buf, size_t len, string *response)
{
	(void)response;
	char *tmp;
	if ( (tmp = strstr(buf, "\n\n")) )
		multicollector(tmp+2, strlen(tmp)-2, NULL);
	else if ( (tmp = strstr(buf, "\r\n\r\n")) )
		multicollector(tmp+4, strlen(tmp)-4, NULL);
}

int http_parser(char *buf, size_t len, string *response, client_info *cinfo)
{
	int ret = 1;

	http_reply_data* http_data = http_proto_get_request_data(buf, len);
	if(!http_data)
		return 0;

	if (http_data->method == HTTP_METHOD_POST)
	{
		if (http_data->body)
			do_http_post(buf, len, response, http_data, cinfo);
	}
	else if (http_data->method == HTTP_METHOD_RESPONSE)
	{
		do_http_response(buf, len, response);
	}
	else if (http_data->method == HTTP_METHOD_GET)
	{
		do_http_get(buf, len, response, http_data);
	}
	else if (http_data->method == HTTP_METHOD_PUT)
	{
		if (http_data->body)
	 		do_http_put(buf, len, response, http_data, cinfo);
	}
	else	ret = 0;

	http_reply_data_free(http_data);
	return ret;
}

int plain_parser(char *buf, size_t len)
{
	//selector_get_plain_metrics(buf, len, "\n", " ", "", 0 );
	multicollector(buf, len, NULL);
	return 1;
}

void alligator_multiparser(char *buf, size_t slen, void (*handler)(char*, size_t, client_info*), string *response, client_info *cinfo)
{
	//printf("handler (%p) parsing '%s'(%zu)\n", handler, buf, slen);
	if (!buf)
		return;
	//size_t len = strlen(buf);
	size_t len = slen;
	//printf("len = %zu, slen = %zu\n", len, slen);
	
	if ( handler )
	{
		//char *body = http_proto_proxer(buf, len, NULL); // TODO: remove?
		//if (buf != body)
		//	len = strlen(body);
		handler(buf, len, cinfo);
		return;
	}
	int rc = 0;
	if ( (rc = http_parser(buf, len, response, cinfo)) ) {}
	else if ( (rc = plain_parser(buf, len)) ) {}
}
