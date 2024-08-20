#define _GNU_SOURCE
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include "parsers/http_proto.h"
#include "common/http.h"
#include "events/context_arg.h"
#include "metric/labels.h"
#include "main.h"
#include "probe/probe.h"
#include "parsers/multiparser.h"
#include "common/selector.h"
#include "common/json_parser.h"
#include "common/units.h"
#include "api/api.h"

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
	if (http->auth_other)
		free(http->auth_other);
	if (http->clear_http)
		string_free(http->clear_http);
	if (http->auth_basic)
		free(http->auth_basic);
	if (http->location)
		free(http->location);

	free(http);
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
	if (!http_code || http_code > 599)
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

	// check a negative number
	if (cur > tmp)
		return NULL;
	size_t headers_len = tmp - cur;
	headers = strndup(cur, headers_len);

	http_reply_data *hrdata = calloc(1, sizeof(*hrdata));
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
		else if((http_code == 301 || http_code == 302 || http_code == 307 || http_code == 308) && !strncasecmp(headers+i, "Location:", 9))
		{
			char *loc_start = headers+i+9 + strcspn(headers+i+9, " ");
			loc_start += strspn(loc_start, " ");
			uint64_t location_size = strcspn(loc_start, "\r\n");
			hrdata->location = strndup(loc_start, location_size);
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
	hrdata->body = body;
	hrdata->body_offset = body - http;

	hrdata->body_size = n - hrdata->headers_size - offset;
	//size_t hrdata_body_size = strlen(hrdata->body);
	size_t hrdata_body_size = n - (hrdata->body - http);

	//printf("2string_cat clear htt %"d64"\n", hrdata->chunked_size);
	string_cat(hrdata->clear_http, hrdata->body, hrdata_body_size);
	//printf("cated2 %zu, body_size %zu\n", hrdata->clear_http->l, hrdata_body_size);
	//puts("+++++++++++++++++++++++++++++++++++++++++++++++++++");
	//printf("SAVED (chunked expect %d) body is\n'%s'\n", hrdata->chunked_expect, hrdata->body);
	//puts("===================================================");
	//puts("");
	//printf("\t2BODY is=====\n%s\n\t======\n", body);
	return hrdata;
}

void http_null_metrics(context_arg *carg)
{
	uint64_t count = 1;
	uint64_t status = 0;
	uint64_t http_code = 0;
	char code[2];
	strlcpy(code, "0", 2);
	metric_update_labels7("aggregator_http_request", &count, DATATYPE_UINT, carg, "code", code, "host", carg->host, "port", carg->port, "type", "aggregator", "proto", "tcp", "parser", carg->parser_name, "key", carg->key);
	metric_add_labels6("aggregator_http_code", &http_code, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "port", carg->port, "key", carg->key, "parser", carg->parser_name);
	metric_add_labels6("aggregator_http_headers_size", &status, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name, "port", carg->port);
	metric_add_labels6("aggregator_http_body_size", &status, DATATYPE_UINT, carg, "host", carg->host, "port", carg->port, "parser", carg->parser_name, "key", carg->key, "proto", "tcp", "type", "aggregator");
}

void http_hrdata_metrics(context_arg *carg, http_reply_data *hrdata)
{
	if (!hrdata)
		return;

	uint64_t count = 1;
	//printf("version=%d\ncode=%d\nmesg='%s'\nheaders='%p'\nbody='%p', content-length: %d, chunked: %d, headers size: %zu, body size: %zu\n", hrdata->http_version, hrdata->http_code, hrdata->mesg, hrdata->headers, hrdata->body, hrdata->content_length, hrdata->chunked_expect, hrdata->headers_size, hrdata->body_size);

	char code[7];
	snprintf(code, 6, "%"PRId16, hrdata->http_code);
	uint64_t http_code = hrdata->http_code;

	metric_update_labels7("aggregator_http_request", &count, DATATYPE_UINT, carg, "code", code, "host", carg->host, "port", carg->port, "type", "aggregator", "proto", "tcp", "parser", carg->parser_name, "key", carg->key);
	metric_add_labels6("aggregator_http_code", &http_code, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name, "port", carg->port);
	metric_add_labels6("aggregator_http_headers_size", &hrdata->headers_size, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name, "port", carg->port);
	metric_add_labels6("aggregator_http_body_size", &hrdata->body_size, DATATYPE_UINT, carg, "host", carg->host, "port", carg->port, "parser", carg->parser_name, "key", carg->key, "proto", "tcp", "type", "aggregator");

	if (carg->data && carg->parser_handler == blackbox_null)
	{
		probe_node *pn = carg->data;
		if (pn->valid_status_codes)
		{
			uint64_t val = 0;
			uint64_t valid_status_codes_size = pn->valid_status_codes_size;
			for (uint64_t i = 0; i < valid_status_codes_size; i++)
			{
				if (!strncmp(code, pn->valid_status_codes[i], strspn(pn->valid_status_codes[i], "0123456789")))
					val = 1;
			}
			metric_add_labels6("probe_success", &val, DATATYPE_UINT, carg, "proto", "http", "type", "blackbox", "host", carg->host, "key", carg->key, "prober", pn->prober_str, "module", pn->name);
		}
	}
}

void http_get_auth_data(http_reply_data *hr_data, char *auth_header)
{
	hr_data->auth_bearer = 0;
	hr_data->auth_basic = 0;
	hr_data->auth_other = 0;
	char *ptr = strcasestr(hr_data->headers, auth_header);
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
		}
		else if (!strncasecmp(ptr, "Bearer", 6))
		{
			ptr += strcspn(ptr, " \t");
			ptr += strspn(ptr, " \t");
			uint8_t size = strcspn(ptr, "\r\n");
			hr_data->auth_bearer = strndup(ptr, size);
		}
		else
		{
			uint8_t size = strcspn(ptr, "\r\n");
			hr_data->auth_other = strndup(ptr, size);
		}
	}
}

void http_follow_redirect(context_arg *carg, http_reply_data *hrdata)
{
	if (ac->log_level > 2)
		printf("http_follow_redirect: %s\n", hrdata->location);
	if (!hrdata)
		return;

	if (!carg->follow_redirects)
		return;

	if (hrdata->http_code == 301 || hrdata->http_code == 302 || hrdata->http_code == 307 || hrdata->http_code == 308)
	{
		if (!hrdata->location)
			return;

		char *location = NULL;
		if (*hrdata->location == '/')
		{
			char *tmp = strstr(carg->url, "://");
			tmp += strspn(tmp, "/");
			tmp += strcspn(tmp, "/");
			size_t url_len = tmp - carg->url;
			size_t location_len = url_len + strlen(hrdata->location + 1);
			location = malloc(location_len);
			strlcpy(location, carg->host, url_len + 1);
			strlcpy(location + url_len, hrdata->location, location_len - url_len);
		}
		else
		{
			location = strdup(hrdata->location);
		}
		if (ac->log_level > 2)
			printf("location is %s\n", location);

		json_t *aggregate_root = json_object();
		json_t *aggregate_arr = json_array();
		json_t *aggregate_obj = json_object();
		json_t *aggregate_handler = json_string(carg->parser_name);
		json_t *aggregate_url = json_string(location);
		json_t *aggregate_follow_redirects = json_integer(carg->follow_redirects-1);
		json_t *aggregate_timeout = json_integer(carg->timeout);
		json_t *aggregate_log_level = json_integer(carg->log_level);
		json_t *aggregate_add_label = labels_to_json(carg->labels);
		json_array_object_insert(aggregate_root, "aggregate", aggregate_arr);
		json_array_object_insert(aggregate_arr, "", aggregate_obj);
		json_array_object_insert(aggregate_obj, "handler", aggregate_handler);

		json_array_object_insert(aggregate_obj, "url", aggregate_url);
		json_array_object_insert(aggregate_obj, "follow_redirects", aggregate_follow_redirects);
		json_array_object_insert(aggregate_obj, "timeout", aggregate_timeout);
		json_array_object_insert(aggregate_obj, "log_level", aggregate_log_level);
		json_array_object_insert(aggregate_obj, "add_label", aggregate_add_label);

		char *dvalue = json_dumps(aggregate_root, JSON_INDENT(2));
		if (carg->log_level > 1)
			puts(dvalue);
		http_api_v1(NULL, NULL, dvalue);
		free(dvalue);
		json_decref(aggregate_root);
	}
}

http_reply_data* http_proto_get_request_data(char *buf, size_t size, char *auth_header)
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
	else if ( !strncmp(buf, "OPTIONS", 7) )
	{
		skip = 7;
		method = HTTP_METHOD_OPTIONS;
	}
	else if ( !strncmp(buf, "HEAD", 4) )
	{
		skip = 4;
		method = HTTP_METHOD_HEAD;
	}
	else if ( !strncmp(buf, "CONNECT", 7) )
	{
		skip = 7;
		method = HTTP_METHOD_CONNECT;
	}
	else if ( !strncmp(buf, "TRACE", 5) )
	{
		skip = 5;
		method = HTTP_METHOD_TRACE;
	}
	else if ( !strncmp(buf, "PATCH", 5) )
	{
		skip = 5;
		method = HTTP_METHOD_PATCH;
	}
	else	return 0;


	http_reply_data* ret = calloc(1, sizeof(*ret));
	ret->method = method;
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
	uint8_t nn_size = 4;
	ret->body = strstr(buf+i, "\r\n\r\n");
	if (!ret->body)
	{
		nn_size = 2;
		ret->body = strstr(buf+i, "\n\n");
	}

	if (!ret->body)
		return ret;

	ret->body_size = 0;
	ret->headers_size = ret->body - (buf + i);
	ret->headers = strndup(buf + i, ret->headers_size);

	if (ret->body)
	{
		ret->body += nn_size;
		ret->body_size = size - (ret->body - buf);
	}

	for (i=0; i<ret->headers_size; i++)
	{
		if(!strncasecmp(ret->headers+i, "X-Expire-Time", 13))
		{
			ret->expire = get_sec_from_human_range(ret->headers+i+13+1, strlen(ret->headers+i+13+1));
		}
		else if(!strncasecmp(ret->headers+i, "Content-Length:", 15))
		{
			ret->content_length = atoll(ret->headers+i+15+1);
		}

		i += strcspn(ret->headers+i, "\n");
	}

	http_get_auth_data(ret, auth_header);

	return ret;
}

int8_t http_check_auth(context_arg *carg, http_reply_data *http_data)
{
	extern aconf *ac;
	if (ac->log_level > 3)
		printf("http auth: basic:%d bearer:%d other:%d\n", carg->auth_basic ? 1:0, carg->auth_bearer ? 1:0, carg->auth_other ? 1:0);

	if (!carg)
		return 1;

	if (!carg->auth_basic && !carg->auth_bearer && !carg->auth_other)
		return 1;

	if (!http_data->auth_basic && !http_data->auth_bearer && !http_data->auth_other)
		return -1;

	if (http_data->auth_basic)
	{
		if (carg->auth_basic) {
			for(uint64_t i = 0; i < carg->auth_basic_size; ++i)
				if ((carg->auth_basic[i]) && (!strcmp(carg->auth_basic[i], http_data->auth_basic)))
					return 1;
		}
		else
			return -1;
	}

	if (http_data->auth_bearer)
	{
		if (carg->auth_bearer) {
			for(uint64_t i = 0; i < carg->auth_bearer_size; ++i)
				if ((carg->auth_bearer[i]) && (!strcmp(carg->auth_bearer[i], http_data->auth_bearer)))
					return 1;
		}
		else
			return -1;
	}

	if (http_data->auth_other)
	{
		if (carg->auth_other) {
			for(uint64_t i = 0; i < carg->auth_other_size; ++i)
				if ((carg->auth_other[i]) && (!strcmp(carg->auth_other[i], http_data->auth_other)))
					return 1;
		}
		else
			return -1;
	}

	return 0;
}

void env_serialize_http_answer(void *funcarg, void* arg)
{
	env_struct *es = arg;
	string *stemplate = funcarg;

	string_cat(stemplate, es->k, strlen(es->k));
	string_cat(stemplate, ": ", 2);
	string_cat(stemplate, es->v, strlen(es->v));
	string_cat(stemplate, "\r\n", 2);
}
