#include <stdio.h>
#include <string.h>
#include "common/https_tls_check.h"
#include "main.h"
#include "parsers/prom_format.h"

void do_http_post(char *buf, size_t len, char *response)
{
	char *body;
	if ( !( body = strstr(buf, "\n\n") ) )
	{
		if ( ( body = strstr(buf, "\r\n\r\n") ) )
			//printf("body is %s\n", body+4);
			if ( body && strlen(body) > 4 )
			{
				if ( !strncmp(body+4, "certificate_https_check", strlen("certificate_https_check")) )
				{
					//puts("add certificate checking:");
					//https_ssl_check_push(body +5 +strlen("certificate_https_check"));
				}
				else
				{
					//puts("add metric parser");
					//selector_get_plain_metrics(body+4, strlen(body+4), "\n", " ", "push_", 5 );
					prom_getmetrics_n(body+4, strlen(body+4));
				}
			}
	}
	else
	{
		//printf("body is %s\n", body+2);
		if ( !strncmp(body+2, "certificate_https_check", strlen("certificate_https_check")) )
		{
			//puts("add certificate checking");
			//https_ssl_check_push(body +3 +strlen("certificate_https_check"));
		}
		else
		{
			//puts("add metric parser");
			//selector_get_plain_metrics(body+2, strlen(body+2), "\n", " ", "push_", 5 );
			prom_getmetrics_n(body+2, strlen(body+2));
		}
	}
	strlcpy(response, "HTTP/1.1 202 Accepted\n\n", strlen("HTTP/1.1 400 Bad Query\n\n")+1);
}

void do_http_get(char *buf, size_t len, char *response)
{
	if (strstr(buf, "/api"))
	{
	}
	else
	{
		stlen str;
		str.l = 1;
		str.s = response;
		str.s[0] = 0;
		stlentext(&str, "HTTP/1.1 200 OK\n\n");

		metric_labels_print(&str);
	}
}

void do_http_put(char *buf, size_t len, char *response)
{
	strlcpy(response, "HTTP/1.1 400 Bad Query\n\n", strlen("HTTP/1.1 400 Bad Query\n\n")+1);
}

void do_http_response(char *buf, size_t len, char *response)
{
	(void)response;
	char *tmp;
	if ( (tmp = strstr(buf, "\n\n")) )
		prom_getmetrics_n(tmp+2, strlen(tmp)-2);
	else if ( (tmp = strstr(buf, "\r\n\r\n")) )
		prom_getmetrics_n(tmp+4, strlen(tmp)-4);
}

int http_parser(char *buf, size_t len, char *response)
{
	if ( !strncmp(buf, "POST", 4) )
		do_http_post(buf, len, response);
	else if ( !strncmp(buf, "HTTP/", 5) )
		do_http_response(buf, len, response);
	else if ( !strncmp(buf, "GET", 3) )
		do_http_get(buf, len, response);
	else if ( !strncmp(buf, "PUT", 3) )
	 	do_http_put(buf, len, response);
	else	return 0;
	return 1;
}

int plain_parser(char *buf, size_t len)
{
	//selector_get_plain_metrics(buf, len, "\n", " ", "", 0 );
	prom_getmetrics_n(buf, len);
	return 1;
}

void alligator_multiparser(char *buf, size_t slen, void (*handler)(char*, size_t, client_info*), char *response, client_info *cinfo)
{
	//printf("parsing '%s'(%zu)\n", buf, len);
	size_t len = strlen(buf);
	if ( handler )
	{
		char *body = http_proto_proxer(buf, len, NULL);
		if (buf != body)
			len = strlen(body);
		handler(body, len, cinfo);
		return;
	}
	int rc = 0;
	if ( (rc = http_parser(buf, len, response)) ) {}
	else if ( (rc = plain_parser(buf, len)) ) {}
}
