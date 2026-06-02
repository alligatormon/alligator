#include <stdlib.h>
#include <string.h>
#include "common/http_entrypoint.h"
#include "common/selector.h"

static const char conn_close[] = "Connection: close\r\n";
static const char conn_keepalive[] = "Connection: keep-alive\r\n";

static const context_arg *http_entrypoint_policy(const context_arg *carg)
{
	if (!carg)
		return NULL;
	if (carg->srv_carg)
		return carg->srv_carg;
	return carg;
}

void http_entrypoint_negotiate(context_arg *carg, const http_reply_data *hr)
{
	const context_arg *policy;
	uint8_t close = 1;

	if (!carg || !hr)
		return;

	policy = http_entrypoint_policy(carg);
	if (!policy || !policy->http_keepalive)
	{
		carg->http_close_after_response = 1;
		return;
	}

	if (hr->connection_close)
		close = 1;
	else if (hr->http_version >= 11)
		close = 0;
	else if (hr->http_version == 10 && hr->connection_keepalive)
		close = 0;
	else if (hr->http_version == 10)
		close = 1;
	else
		close = 1;

	carg->http_close_after_response = close;
}

size_t http_entrypoint_request_size(const char *buf, const http_reply_data *hr)
{
	size_t end;

	if (!buf || !hr || !hr->body || hr->body < buf)
		return 0;

	end = (size_t)(hr->body - buf);
	if (hr->content_length > 0)
		end += (size_t)hr->content_length;

	return end;
}

int http_entrypoint_request_complete(const char *buf, size_t buflen, const http_reply_data *hr, size_t *msg_size)
{
	size_t need;

	if (!buf || !hr || !hr->body)
		return 0;

	need = http_entrypoint_request_size(buf, hr);
	if (!need || buflen < need)
		return 0;

	if (msg_size)
		*msg_size = need;

	return 1;
}

char *http_entrypoint_prepare_response(context_arg *carg, char *body, size_t len, int *allocated)
{
	const char *from;
	const char *to;
	size_t from_len;
	size_t to_len;
	char *hit;
	char *out;
	size_t off;

	if (allocated)
		*allocated = 0;

	if (!body || len < 4 || strncmp(body, "HTTP", 4))
		return body;

	if (!carg || carg->http_close_after_response)
		return body;

	from = conn_close;
	to = conn_keepalive;
	from_len = sizeof(conn_close) - 1;
	to_len = sizeof(conn_keepalive) - 1;

	hit = strstr(body, from);
	if (!hit)
		return body;

	out = malloc(len + (to_len - from_len) + 1);
	if (!out)
		return body;

	off = (size_t)(hit - body);
	memcpy(out, body, off);
	memcpy(out + off, to, to_len);
	memcpy(out + off + to_len, hit + from_len, len - off - from_len);
	out[len + (to_len - from_len)] = '\0';

	if (allocated)
		*allocated = 1;

	return out;
}

int http_entrypoint_should_shutdown(context_arg *carg, int write_status)
{
	if (write_status != 0)
		return 1;
	if (!carg)
		return 1;
	return carg->http_close_after_response ? 1 : 0;
}

void http_entrypoint_reset_request_state(context_arg *carg)
{
	if (!carg)
		return;

	carg->body_read = 0;
	carg->expect_body_length = 0;
	carg->http_request_size = 0;
	carg->body = NULL;
	carg->headers_size = 0;
	carg->parsed = 0;
	carg->parser_status = 0;
}

void http_entrypoint_finish_empty_body(string *response)
{
	if (!response || !response->s || !response->l)
		return;

	if (!strstr(response->s, "Content-Length:"))
		string_cat(response, "Content-Length: 0\r\n", 19);

	string_cat(response, "\r\n", 2);
}

void http_entrypoint_consume_request(context_arg *carg)
{
	size_t n;

	if (!carg || !carg->http_request_size || !carg->full_body)
		return;

	n = carg->http_request_size;
	if (carg->full_body->l > n)
	{
		memmove(carg->full_body->s, carg->full_body->s + n, carg->full_body->l - n);
		carg->full_body->l -= n;
		carg->full_body->s[carg->full_body->l] = '\0';
	}
	else
		string_null(carg->full_body);

	carg->http_request_size = 0;
}
