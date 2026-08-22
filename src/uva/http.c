#include "uva.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UVA_HTTP_MAX_REDIRECTS
#define UVA_HTTP_MAX_REDIRECTS 5
#endif

#ifndef UVA_HTTP_DEFAULT_MAX_BODY
#define UVA_HTTP_DEFAULT_MAX_BODY (1u << 20)
#endif

#ifndef UVA_HTTP_HDR_MAX
#define UVA_HTTP_HDR_MAX (16u * 1024)
#endif

void uva_http_res_free(uva_http_res_t *r)
{
	if (!r)
		return;
	free(r->body);
	free(r->content_type);
	r->body = NULL;
	r->content_type = NULL;
	r->body_len = 0;
	r->status = 0;
}

static int starts_with_ci(const char *s, const char *pfx)
{
	size_t n = strlen(pfx);
	return strncasecmp(s, pfx, n) == 0;
}

static int parse_http_url(const char *url, char *host, size_t host_sz,
			  int *port, char *path, size_t path_sz)
{
	const char *p;
	const char *host_end;
	const char *slash;
	size_t host_len;

	if (!url || !starts_with_ci(url, "http://"))
		return UV_EINVAL;

	p = url + 7;
	if (*p == '[') {
		const char *rb = strchr(p, ']');
		if (!rb || (size_t)(rb - p - 1) >= host_sz)
			return UV_EINVAL;
		memcpy(host, p + 1, (size_t)(rb - p - 1));
		host[rb - p - 1] = '\0';
		p = rb + 1;
		*port = 80;
		if (*p == ':') {
			p++;
			*port = atoi(p);
			while (*p && *p != '/')
				p++;
		}
		if (*p == '\0') {
			snprintf(path, path_sz, "/");
			return 0;
		}
		if (strlen(p) >= path_sz)
			return UV_EINVAL;
		snprintf(path, path_sz, "%s", p);
		return 0;
	}

	slash = strchr(p, '/');
	host_end = p;
	while (*host_end && *host_end != '/' && *host_end != ':')
		host_end++;

	host_len = (size_t)(host_end - p);
	if (host_len == 0 || host_len >= host_sz)
		return UV_EINVAL;
	memcpy(host, p, host_len);
	host[host_len] = '\0';

	*port = 80;
	if (*host_end == ':') {
		*port = atoi(host_end + 1);
		if (*port <= 0)
			return UV_EINVAL;
		while (*host_end && *host_end != '/')
			host_end++;
	}

	if (!slash || *slash == '\0') {
		snprintf(path, path_sz, "/");
		return 0;
	}
	if (strlen(slash) >= path_sz)
		return UV_EINVAL;
	snprintf(path, path_sz, "%s", slash);
	return 0;
}

static char *header_value(const char *headers, const char *name)
{
	size_t nlen = strlen(name);
	const char *p = headers;

	while (*p) {
		const char *eol = strstr(p, "\r\n");
		size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
		if (linelen >= nlen + 1 && strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
			const char *v = p + nlen + 1;
			size_t vlen;
			char *out;
			while (*v == ' ' || *v == '\t')
				v++;
			vlen = linelen - (size_t)(v - p);
			while (vlen && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t'))
				vlen--;
			out = malloc(vlen + 1);
			if (!out)
				return NULL;
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			return out;
		}
		if (!eol)
			break;
		p = eol + 2;
	}
	return NULL;
}

static int parse_status_line(const char *headers)
{
	const char *p = headers;
	int status;

	if (strncmp(p, "HTTP/", 5) != 0)
		return -1;
	p = strchr(p, ' ');
	if (!p)
		return -1;
	status = atoi(p + 1);
	return status > 0 ? status : -1;
}

static int append_bytes(char **buf, size_t *len, size_t *cap, const void *src, size_t n, size_t max_body)
{
	if (*len + n > max_body)
		return UV_ENOBUFS;
	if (*len + n + 1 > *cap) {
		size_t ncap = *cap ? *cap : 4096;
		char *nb;
		while (ncap < *len + n + 1)
			ncap *= 2;
		nb = realloc(*buf, ncap);
		if (!nb)
			return UV_ENOMEM;
		*buf = nb;
		*cap = ncap;
	}
	memcpy(*buf + *len, src, n);
	*len += n;
	(*buf)[*len] = '\0';
	return 0;
}

static int hex_value(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int decode_chunked(const char *in, size_t in_len, char **out, size_t *out_len, size_t max_body)
{
	size_t i = 0;
	char *body = NULL;
	size_t blen = 0, bcap = 0;

	while (i < in_len) {
		size_t chunk = 0;
		int digit;

		while (i < in_len && (digit = hex_value((unsigned char)in[i])) >= 0) {
			chunk = (chunk << 4) + (size_t)digit;
			i++;
		}
		while (i < in_len && in[i] != '\n')
			i++;
		if (i < in_len && in[i] == '\n')
			i++;
		if (chunk == 0)
			break;
		if (i + chunk > in_len) {
			free(body);
			return UV_E2BIG;
		}
		if (append_bytes(&body, &blen, &bcap, in + i, chunk, max_body) < 0) {
			free(body);
			return UV_ENOBUFS;
		}
		i += chunk;
		if (i + 1 < in_len && in[i] == '\r' && in[i + 1] == '\n')
			i += 2;
		else if (i < in_len && in[i] == '\n')
			i++;
	}

	*out = body ? body : calloc(1, 1);
	*out_len = blen;
	return *out ? 0 : UV_ENOMEM;
}

static int join_url(const char *base_url, const char *location, char *out, size_t out_sz)
{
	if (!location || !*location)
		return UV_EINVAL;
	if (starts_with_ci(location, "http://") || starts_with_ci(location, "https://")) {
		if (strlen(location) >= out_sz)
			return UV_EINVAL;
		snprintf(out, out_sz, "%s", location);
		return 0;
	}
	if (location[0] == '/') {
		char host[256];
		char path[2048];
		int port;
		if (parse_http_url(base_url, host, sizeof(host), &port, path, sizeof(path)) < 0)
			return UV_EINVAL;
		if (port == 80)
			snprintf(out, out_sz, "http://%s%s", host, location);
		else
			snprintf(out, out_sz, "http://%s:%d%s", host, port, location);
		return 0;
	}
	if (strlen(location) >= out_sz)
		return UV_EINVAL;
	snprintf(out, out_sz, "%s", location);
	return 0;
}

static int http_exchange(uv_loop_t *loop, const char *method, const char *url,
			 const char *content_type, const void *body, size_t body_len,
			 uint64_t timeout_ms, size_t max_body, uva_http_res_t *out)
{
	char host[256];
	char path[2048];
	int port = 80;
	uva_tcp_t *tcp = NULL;
	char *req = NULL;
	size_t req_len;
	char *raw = NULL;
	size_t raw_len = 0, raw_cap = 0;
	char chunk[4096];
	ssize_t n;
	char *hdr_end;
	int status;
	char *cl_hdr = NULL;
	char *te_hdr = NULL;
	char *ct_hdr = NULL;
	char *loc_hdr = NULL;
	const char *payload;
	size_t payload_len;
	int r;

	memset(out, 0, sizeof(*out));

	r = parse_http_url(url, host, sizeof(host), &port, path, sizeof(path));
	if (r < 0)
		return r;

	tcp = uva_tcp_new(loop);
	if (!tcp)
		return UV_ENOMEM;

	if (timeout_ms)
		uva_tcp_set_deadline(tcp, uv_now(loop) + timeout_ms);

	r = uva_tcp_connect(tcp, host, port);
	if (r < 0)
		goto out_tcp;

	req_len = (size_t)snprintf(NULL, 0,
		"%s %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: uva-http/1.0\r\n"
		"Accept: */*\r\n"
		"Connection: close\r\n"
		"%s%s%s"
		"Content-Length: %zu\r\n"
		"\r\n",
		method, path, host,
		content_type ? "Content-Type: " : "",
		content_type ? content_type : "",
		content_type ? "\r\n" : "",
		body_len);
	req = malloc(req_len + body_len + 1);
	if (!req) {
		r = UV_ENOMEM;
		goto out_tcp;
	}
	snprintf(req, req_len + 1,
		"%s %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: uva-http/1.0\r\n"
		"Accept: */*\r\n"
		"Connection: close\r\n"
		"%s%s%s"
		"Content-Length: %zu\r\n"
		"\r\n",
		method, path, host,
		content_type ? "Content-Type: " : "",
		content_type ? content_type : "",
		content_type ? "\r\n" : "",
		body_len);
	if (body_len && body)
		memcpy(req + req_len, body, body_len);

	r = (int)uva_tcp_write(tcp, req, req_len + body_len);
	if (r < 0)
		goto out_tcp;

	for (;;) {
		n = uva_tcp_read(tcp, chunk, sizeof(chunk));
		if (n < 0) {
			r = (int)n;
			goto out_tcp;
		}
		if (n == 0)
			break;
		r = append_bytes(&raw, &raw_len, &raw_cap, chunk, (size_t)n, max_body + UVA_HTTP_HDR_MAX);
		if (r < 0)
			goto out_tcp;
		if (raw_len >= UVA_HTTP_HDR_MAX && !strstr(raw, "\r\n\r\n")) {
			r = UV_ENOBUFS;
			goto out_tcp;
		}
	}

	if (!raw || raw_len == 0) {
		r = UV_EOF;
		goto out_tcp;
	}

	hdr_end = strstr(raw, "\r\n\r\n");
	if (!hdr_end) {
		r = UV_EPROTO;
		goto out_tcp;
	}
	*hdr_end = '\0';
	payload = hdr_end + 4;
	payload_len = raw_len - (size_t)(payload - raw);

	status = parse_status_line(raw);
	if (status < 0) {
		r = UV_EPROTO;
		goto out_tcp;
	}
	out->status = status;

	ct_hdr = header_value(raw, "Content-Type");
	cl_hdr = header_value(raw, "Content-Length");
	te_hdr = header_value(raw, "Transfer-Encoding");
	loc_hdr = header_value(raw, "Location");

	if (ct_hdr)
		out->content_type = ct_hdr;
	ct_hdr = NULL;

	if (te_hdr && strstr(te_hdr, "chunked")) {
		r = decode_chunked(payload, payload_len, &out->body, &out->body_len, max_body);
		if (r < 0)
			goto out_tcp;
	} else if (cl_hdr) {
		size_t want = (size_t)strtoull(cl_hdr, NULL, 10);
		if (want > max_body) {
			r = UV_ENOBUFS;
			goto out_tcp;
		}
		if (payload_len < want) {
			r = UV_E2BIG;
			goto out_tcp;
		}
		out->body = malloc(want + 1);
		if (!out->body) {
			r = UV_ENOMEM;
			goto out_tcp;
		}
		memcpy(out->body, payload, want);
		out->body[want] = '\0';
		out->body_len = want;
	} else {
		if (payload_len > max_body) {
			r = UV_ENOBUFS;
			goto out_tcp;
		}
		out->body = malloc(payload_len + 1);
		if (!out->body) {
			r = UV_ENOMEM;
			goto out_tcp;
		}
		memcpy(out->body, payload, payload_len);
		out->body[payload_len] = '\0';
		out->body_len = payload_len;
	}

	if (loc_hdr && (status == 301 || status == 302 || status == 303 ||
			status == 307 || status == 308)) {
		/* Stash Location in content_type so the caller can follow. */
		free(out->content_type);
		out->content_type = loc_hdr;
		loc_hdr = NULL;
		r = 1;
		goto out_tcp;
	}

	r = 0;

out_tcp:
	free(req);
	free(raw);
	free(cl_hdr);
	free(te_hdr);
	free(loc_hdr);
	if (r < 0)
		uva_http_res_free(out);
	uva_tcp_free(tcp);
	return r;
}

int uva_http_request(uv_loop_t *loop, const uva_http_req_t *req, uva_http_res_t *out)
{
	const char *method;
	char urlbuf[2048];
	const char *url;
	size_t max_body;
	int hops = 0;
	int follow;
	int r;

	if (!loop || !req || !req->url || !out)
		return UV_EINVAL;
	if (starts_with_ci(req->url, "https://"))
		return UV_ENOTSUP;

	method = req->method && req->method[0] ? req->method : "GET";
	url = req->url;
	max_body = req->max_body ? req->max_body : UVA_HTTP_DEFAULT_MAX_BODY;
	follow = req->follow_redirects;

	for (;;) {
		int redirect_status;

		r = http_exchange(loop, method, url, req->content_type, req->body, req->body_len,
				  req->timeout_ms, max_body, out);
		if (r != 1)
			return r;

		redirect_status = out->status;
		if (!follow || hops >= UVA_HTTP_MAX_REDIRECTS)
			return 0;
		if (join_url(url, out->content_type, urlbuf, sizeof(urlbuf)) < 0) {
			uva_http_res_free(out);
			return UV_EINVAL;
		}
		if (starts_with_ci(urlbuf, "https://")) {
			uva_http_res_free(out);
			return UV_ENOTSUP;
		}
		uva_http_res_free(out);
		url = urlbuf;
		if (redirect_status == 303)
			method = "GET";
		hops++;
	}
}
