#include "events/proxy.h"
#include "events/context_arg.h"
#include "events/client.h"
#include "events/udp.h"
#include "events/uv_alloc.h"
#include "common/url.h"
#include "common/logs.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SOCKS5_VER 0x05
#define SOCKS5_CMD_CONNECT 0x01
#define SOCKS5_CMD_UDP 0x03
#define SOCKS5_ATYP_IPV4 0x01
#define SOCKS5_ATYP_DOMAIN 0x03
#define SOCKS5_ATYP_IPV6 0x04
#define SOCKS5_AUTH_NONE 0x00
#define SOCKS5_AUTH_PASS 0x02
#define SOCKS5_AUTH_NO_ACCEPT 0xFF

static int authority_has_port(const char *rest)
{
	const char *at;
	const char *h;
	const char *br;
	const char *colon;

	if (!rest)
		return 0;
	at = strrchr(rest, '@');
	h = at ? at + 1 : rest;
	if (*h == '[') {
		br = strchr(h, ']');
		return br && br[1] == ':';
	}
	colon = strrchr(h, ':');
	if (!colon || colon == h)
		return 0;
	return colon[1] >= '0' && colon[1] <= '9';
}

int proxy_host_is_ipv4(const char *host)
{
	struct in_addr addr;

	if (!host || !*host)
		return 0;
	return inet_pton(AF_INET, host, &addr) == 1;
}

proxy_settings *proxy_parse_url(const char *url)
{
	proxy_settings *p;
	host_aggregator_info *hi;
	char rewritten[2048];
	const char *rest;
	uint16_t default_port;
	int had_port;
	proxy_type type;
	uint8_t remote_dns = 0;

	if (!url || !*url)
		return NULL;

	if (!strncmp(url, "https://", 8))
		return NULL;

	if (!strncmp(url, "http://", 7)) {
		type = PROXY_TYPE_HTTP;
		rest = url + 7;
		default_port = 8080;
	} else if (!strncmp(url, "socks5h://", 10)) {
		type = PROXY_TYPE_SOCKS5;
		remote_dns = 1;
		rest = url + 10;
		default_port = 1080;
	} else if (!strncmp(url, "socks5://", 9)) {
		type = PROXY_TYPE_SOCKS5;
		remote_dns = 0;
		rest = url + 9;
		default_port = 1080;
	} else {
		return NULL;
	}

	if (!*rest)
		return NULL;

	had_port = authority_has_port(rest);
	snprintf(rewritten, sizeof(rewritten), "http://%s", rest);
	hi = parse_url(rewritten, strlen(rewritten));
	if (!hi || !hi->host || !*hi->host) {
		url_free(hi);
		return NULL;
	}

	p = calloc(1, sizeof(*p));
	if (!p) {
		url_free(hi);
		return NULL;
	}
	p->type = type;
	p->remote_dns = remote_dns;
	strlcpy(p->host, hi->host, PROXY_HOST_SIZE);
	if (had_port && hi->port[0])
		strlcpy(p->port, hi->port, PROXY_PORT_SIZE);
	else
		snprintf(p->port, PROXY_PORT_SIZE, "%u", default_port);
	p->numport = (uint16_t)strtoul(p->port, NULL, 10);
	if (hi->user)
		p->user = strdup(hi->user);
	if (hi->pass)
		p->password = strdup(hi->pass);
	if (hi->auth)
		p->auth_b64 = strdup(hi->auth);
	p->url = strdup(url);
	url_free(hi);
	return p;
}

proxy_settings *proxy_settings_copy(const proxy_settings *src)
{
	proxy_settings *p;

	if (!src)
		return NULL;
	p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	memcpy(p, src, sizeof(*p));
	p->user = src->user ? strdup(src->user) : NULL;
	p->password = src->password ? strdup(src->password) : NULL;
	p->auth_b64 = src->auth_b64 ? strdup(src->auth_b64) : NULL;
	p->url = src->url ? strdup(src->url) : NULL;
	return p;
}

void proxy_settings_free(proxy_settings *p)
{
	if (!p)
		return;
	free(p->user);
	free(p->password);
	free(p->auth_b64);
	free(p->url);
	free(p);
}

int proxy_ok_for_transport(const proxy_settings *p, uint8_t transport)
{
	if (!p)
		return 1;
	if (transport == APROTO_UDP && p->type != PROXY_TYPE_SOCKS5)
		return 0;
	return 1;
}

int proxy_needs_tunnel(context_arg *carg)
{
	if (!carg || !carg->proxy)
		return 0;
	if (carg->proxy->type == PROXY_TYPE_SOCKS5)
		return 1;
	if (carg->proxy->type == PROXY_TYPE_HTTP && carg->proto != APROTO_HTTP)
		return 1;
	return 0;
}

char *proxy_http_connect_build(const char *host, uint16_t port, const char *auth_b64, size_t *out_len)
{
	string *s;
	char *ret;
	size_t n;

	if (!host || !*host)
		return NULL;
	s = string_init(256);
	string_sprintf(s, "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n", host, port, host, port);
	if (auth_b64 && *auth_b64)
		string_sprintf(s, "Proxy-Authorization: Basic %s\r\n", auth_b64);
	string_cat(s, "\r\n", 2);
	n = s->l;
	ret = s->s;
	free(s);
	if (out_len)
		*out_len = n;
	return ret;
}

int proxy_http_connect_parse(const char *buf, size_t len, int *http_code, size_t *consumed)
{
	size_t i;
	size_t hdr_end = 0;
	size_t pos;
	int code = 0;
	int digits = 0;

	if (!buf || !http_code || !consumed)
		return -1;
	for (i = 0; i + 3 < len; i++) {
		if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
			hdr_end = i + 4;
			break;
		}
	}
	if (!hdr_end)
		return 0;
	*consumed = hdr_end;
	if (len < 8 || strncasecmp(buf, "HTTP/", 5))
		return -1;
	pos = 5;
	while (pos < hdr_end && buf[pos] != ' ')
		pos++;
	while (pos < hdr_end && buf[pos] == ' ')
		pos++;
	while (pos < hdr_end && buf[pos] >= '0' && buf[pos] <= '9' && digits < 3) {
		code = code * 10 + (buf[pos] - '0');
		pos++;
		digits++;
	}
	if (!digits)
		return -1;
	*http_code = code;
	if (code >= 200 && code < 300)
		return 1;
	return -1;
}

size_t proxy_socks5_greet_build(unsigned char *dst, size_t cap, int with_pass)
{
	if (!dst)
		return 0;
	if (with_pass) {
		if (cap < 4)
			return 0;
		dst[0] = SOCKS5_VER;
		dst[1] = 2;
		dst[2] = SOCKS5_AUTH_NONE;
		dst[3] = SOCKS5_AUTH_PASS;
		return 4;
	}
	if (cap < 3)
		return 0;
	dst[0] = SOCKS5_VER;
	dst[1] = 1;
	dst[2] = SOCKS5_AUTH_NONE;
	return 3;
}

size_t proxy_socks5_auth_build(unsigned char *dst, size_t cap, const char *user, const char *pass)
{
	size_t ulen;
	size_t plen;

	if (!dst)
		return 0;
	user = user ? user : "";
	pass = pass ? pass : "";
	ulen = strlen(user);
	plen = strlen(pass);
	if (ulen > 255 || plen > 255)
		return 0;
	if (cap < 3 + ulen + plen)
		return 0;
	dst[0] = 0x01;
	dst[1] = (unsigned char)ulen;
	memcpy(dst + 2, user, ulen);
	dst[2 + ulen] = (unsigned char)plen;
	memcpy(dst + 3 + ulen, pass, plen);
	return 3 + ulen + plen;
}

size_t proxy_socks5_cmd_build(unsigned char *dst, size_t cap, uint8_t cmd, const char *host, uint16_t port)
{
	size_t hlen;
	uint16_t nport;
	struct in_addr addr;

	if (!dst || cap < 10)
		return 0;
	nport = htons(port);
	dst[0] = SOCKS5_VER;
	dst[1] = cmd;
	dst[2] = 0;
	if (cmd == SOCKS5_CMD_UDP && (!host || !*host)) {
		dst[3] = SOCKS5_ATYP_IPV4;
		memset(dst + 4, 0, 4);
		memcpy(dst + 8, &nport, 2);
		return 10;
	}
	if (!host || !*host)
		return 0;
	if (inet_pton(AF_INET, host, &addr) == 1) {
		dst[3] = SOCKS5_ATYP_IPV4;
		memcpy(dst + 4, &addr, 4);
		memcpy(dst + 8, &nport, 2);
		return 10;
	}
	hlen = strlen(host);
	if (hlen == 0 || hlen > 255)
		return 0;
	if (cap < 7 + hlen)
		return 0;
	dst[3] = SOCKS5_ATYP_DOMAIN;
	dst[4] = (unsigned char)hlen;
	memcpy(dst + 5, host, hlen);
	memcpy(dst + 5 + hlen, &nport, 2);
	return 7 + hlen;
}

int proxy_socks5_reply_needed(const unsigned char *buf, size_t len, size_t *need)
{
	uint8_t atyp;

	if (!need)
		return -1;
	if (len < 4) {
		*need = 4;
		return 0;
	}
	atyp = buf[3];
	if (atyp == SOCKS5_ATYP_IPV4)
		*need = 10;
	else if (atyp == SOCKS5_ATYP_DOMAIN) {
		if (len < 5) {
			*need = 5;
			return 0;
		}
		*need = (size_t)4 + 1 + buf[4] + 2;
	} else if (atyp == SOCKS5_ATYP_IPV6)
		*need = 22;
	else
		return -1;
	if (len < *need)
		return 0;
	return 1;
}

int proxy_socks5_reply_parse(const unsigned char *buf, size_t len, int *rep, struct sockaddr_in *bind_addr)
{
	size_t need = 0;
	int rc;
	uint16_t nport;
	uint8_t atyp;

	rc = proxy_socks5_reply_needed(buf, len, &need);
	if (rc <= 0)
		return rc;
	if (buf[0] != SOCKS5_VER)
		return -1;
	if (rep)
		*rep = buf[1];
	if (buf[1] != 0)
		return -1;
	if (!bind_addr)
		return 1;
	memset(bind_addr, 0, sizeof(*bind_addr));
	bind_addr->sin_family = AF_INET;
	atyp = buf[3];
	if (atyp == SOCKS5_ATYP_IPV4) {
		memcpy(&bind_addr->sin_addr, buf + 4, 4);
		memcpy(&nport, buf + 8, 2);
		bind_addr->sin_port = nport;
		return 1;
	}
	if (atyp == SOCKS5_ATYP_DOMAIN) {
		memcpy(&nport, buf + 5 + buf[4], 2);
		bind_addr->sin_port = nport;
		bind_addr->sin_addr.s_addr = 0;
		return 1;
	}
	if (atyp == SOCKS5_ATYP_IPV6) {
		memcpy(&nport, buf + 20, 2);
		bind_addr->sin_port = nport;
		bind_addr->sin_addr.s_addr = 0;
		return 1;
	}
	return -1;
}

size_t proxy_udp_wrap(unsigned char *dst, size_t cap, const char *host, uint16_t port, const unsigned char *payload, size_t payload_len)
{
	size_t hdr;
	unsigned char cmd[512];

	if (!dst)
		return 0;
	hdr = proxy_socks5_cmd_build(cmd, sizeof(cmd), SOCKS5_CMD_CONNECT, host, port);
	if (!hdr || hdr < 3)
		return 0;
	/* Convert CONNECT header to UDP: RSV(2)=0 FRAG=0 then ATYP... (drop VER CMD RSV). */
	if (cap < 3 + (hdr - 3) + payload_len)
		return 0;
	dst[0] = 0;
	dst[1] = 0;
	dst[2] = 0;
	memcpy(dst + 3, cmd + 3, hdr - 3);
	if (payload && payload_len)
		memcpy(dst + 3 + (hdr - 3), payload, payload_len);
	return 3 + (hdr - 3) + payload_len;
}

int proxy_udp_unwrap(const unsigned char *src, size_t src_len, const unsigned char **payload, size_t *payload_len)
{
	size_t hdr;
	uint8_t atyp;

	if (!src || src_len < 4 || !payload || !payload_len)
		return -1;
	if (src[2] != 0)
		return -1;
	atyp = src[3];
	if (atyp == SOCKS5_ATYP_IPV4)
		hdr = 10;
	else if (atyp == SOCKS5_ATYP_DOMAIN) {
		if (src_len < 5)
			return -1;
		hdr = (size_t)4 + 1 + src[4] + 2;
	} else if (atyp == SOCKS5_ATYP_IPV6)
		hdr = 22;
	else
		return -1;
	if (src_len < hdr)
		return -1;
	*payload = src + hdr;
	*payload_len = src_len - hdr;
	return 1;
}

int http_request_apply_proxy(context_arg *carg)
{
	char *req;
	size_t len;
	char *sp1;
	char *path;
	char *sp2;
	char origin[PROXY_HOST_SIZE + 16];
	size_t origin_len;
	size_t method_len;
	size_t path_len;
	size_t rest_len;
	char auth_hdr[512];
	size_t auth_len = 0;
	char *out;
	size_t new_len;
	char *eoh;
	size_t prefix;
	char *old;
	size_t rest_off;

	if (!carg || !carg->proxy || carg->proxy->type != PROXY_TYPE_HTTP)
		return 0;
	if (carg->proto != APROTO_HTTP)
		return 0;
	if (!carg->mesg || !carg->mesg_len)
		return 0;

	req = carg->mesg;
	len = carg->mesg_len;
	sp1 = memchr(req, ' ', len);
	if (!sp1)
		return -1;
	path = sp1 + 1;
	if ((size_t)(path - req) + 7 <= len && !strncmp(path, "http://", 7))
		return 0;
	sp2 = memchr(path, ' ', len - (size_t)(path - req));
	if (!sp2)
		return -1;

	if (carg->numport && carg->numport != 80)
		snprintf(origin, sizeof(origin), "http://%s:%u", carg->host, carg->numport);
	else
		snprintf(origin, sizeof(origin), "http://%s", carg->host);
	origin_len = strlen(origin);
	method_len = (size_t)(sp1 - req);
	path_len = (size_t)(sp2 - path);
	if (!path_len) {
		path = "/";
		path_len = 1;
	}
	rest_len = len - (size_t)(sp2 - req);
	if (carg->proxy->auth_b64 && *carg->proxy->auth_b64)
		auth_len = (size_t)snprintf(auth_hdr, sizeof(auth_hdr), "Proxy-Authorization: Basic %s\r\n", carg->proxy->auth_b64);

	new_len = method_len + 1 + origin_len + path_len + rest_len + auth_len;
	out = malloc(new_len + 1);
	if (!out)
		return -1;
	memcpy(out, req, method_len);
	out[method_len] = ' ';
	memcpy(out + method_len + 1, origin, origin_len);
	memcpy(out + method_len + 1 + origin_len, path, path_len);
	rest_off = method_len + 1 + origin_len + path_len;
	memcpy(out + rest_off, sp2, rest_len);
	out[new_len - auth_len] = 0;

	if (auth_len) {
		eoh = strstr(out, "\r\n\r\n");
		if (!eoh) {
			free(out);
			return -1;
		}
		prefix = (size_t)(eoh - out) + 2;
		memmove(out + prefix + auth_len, out + prefix, new_len - auth_len - prefix);
		memcpy(out + prefix, auth_hdr, auth_len);
	}
	out[new_len] = 0;

	old = carg->mesg;
	if (carg->buffer) {
		free(carg->buffer);
		carg->buffer = NULL;
	}
	aconf_mesg_set(carg, out, new_len);
	free(old);
	return 1;
}

static void proxy_fail(context_arg *carg, const char *why)
{
	if (carg)
		carglog(carg, L_ERROR, "proxy handshake failed key %s host %s: %s\n",
			carg->key ? carg->key : "?", carg->host, why ? why : "error");
	if (carg && carg->proxy_udp_associate)
		udp_client_socks_fail(carg);
	else
		tcp_client_fail_session(carg);
}

static void proxy_write_bytes(context_arg *carg, unsigned char *buf, size_t len)
{
	if (carg->write_buffer.base) {
		free(carg->write_buffer.base);
		carg->write_buffer.base = NULL;
		carg->write_buffer.len = 0;
	}
	carg->write_buffer = uv_buf_init((char *)buf, len);
	memset(&carg->write_req, 0, sizeof(carg->write_req));
	carg->write_req.data = carg;
	if (uv_write(&carg->write_req, (uv_stream_t *)&carg->client, &carg->write_buffer, 1, NULL)) {
		free(buf);
		carg->write_buffer.base = NULL;
		carg->write_buffer.len = 0;
		proxy_fail(carg, "uv_write");
	}
}

static int proxy_send_socks_cmd(context_arg *carg)
{
	unsigned char tmp[512];
	unsigned char *copy;
	size_t n;
	uint8_t cmd;
	const char *host;
	uint16_t port;

	cmd = carg->proxy_udp_associate ? SOCKS5_CMD_UDP : SOCKS5_CMD_CONNECT;
	if (cmd == SOCKS5_CMD_UDP) {
		host = NULL;
		port = 0;
	} else {
		host = carg->host;
		port = carg->numport;
	}
	n = proxy_socks5_cmd_build(tmp, sizeof(tmp), cmd, host, port);
	if (!n)
		return -1;
	copy = malloc(n);
	if (!copy)
		return -1;
	memcpy(copy, tmp, n);
	carg->proxy_phase = PROXY_PHASE_SOCKS5_CMD_READ;
	carg->proxy_hs_len = 0;
	proxy_write_bytes(carg, copy, n);
	return 0;
}

void proxy_handshake_reset(context_arg *carg)
{
	if (!carg)
		return;
	carg->proxy_phase = PROXY_PHASE_NONE;
	carg->proxy_hs_len = 0;
	memset(carg->proxy_hs_buf, 0, sizeof(carg->proxy_hs_buf));
	memset(&carg->proxy_udp_relay, 0, sizeof(carg->proxy_udp_relay));
}

void proxy_fill_udp_relay(context_arg *carg, const struct sockaddr_in *bind_addr)
{
	struct sockaddr_in peer;
	int peer_len;

	if (!carg || !bind_addr)
		return;
	carg->proxy_udp_relay = *bind_addr;
	if (carg->proxy_udp_relay.sin_addr.s_addr == 0) {
		peer_len = sizeof(peer);
		memset(&peer, 0, sizeof(peer));
		if (!uv_tcp_getpeername(&carg->client, (struct sockaddr *)&peer, &peer_len))
			carg->proxy_udp_relay.sin_addr = peer.sin_addr;
	}
	carg->proxy_udp_relay.sin_family = AF_INET;
}

static void proxy_tunnel_ready(context_arg *carg)
{
	carg->proxy_phase = PROXY_PHASE_DONE;
	uv_read_stop((uv_stream_t *)&carg->client);
	if (carg->write_buffer.base) {
		free(carg->write_buffer.base);
		carg->write_buffer.base = NULL;
		carg->write_buffer.len = 0;
	}
	if (carg->proxy_udp_associate)
		udp_client_socks_relay_start(carg);
	else
		tcp_client_start_app(carg);
}

void proxy_handshake_start(context_arg *carg)
{
	unsigned char tmp[512];
	unsigned char *copy;
	size_t n;
	char *connect_req;
	int with_pass;

	if (!carg || !carg->proxy)
		return;
	carg->proxy_hs_len = 0;
	uv_read_start((uv_stream_t *)&carg->client, tcp_alloc, proxy_client_read);

	if (carg->proxy->type == PROXY_TYPE_HTTP) {
		connect_req = proxy_http_connect_build(carg->host, carg->numport, carg->proxy->auth_b64, &n);
		if (!connect_req) {
			proxy_fail(carg, "CONNECT build");
			return;
		}
		carg->proxy_phase = PROXY_PHASE_HTTP_CONNECT_READ;
		proxy_write_bytes(carg, (unsigned char *)connect_req, n);
		return;
	}

	with_pass = (carg->proxy->user && *carg->proxy->user) ? 1 : 0;
	n = proxy_socks5_greet_build(tmp, sizeof(tmp), with_pass);
	copy = malloc(n);
	if (!copy) {
		proxy_fail(carg, "malloc");
		return;
	}
	memcpy(copy, tmp, n);
	carg->proxy_phase = PROXY_PHASE_SOCKS5_GREET_READ;
	proxy_write_bytes(carg, copy, n);
}

static int proxy_hs_append(context_arg *carg, const char *base, ssize_t nread)
{
	if (nread <= 0)
		return -1;
	if (carg->proxy_hs_len + (size_t)nread > PROXY_HS_SIZE)
		return -1;
	memcpy(carg->proxy_hs_buf + carg->proxy_hs_len, base, (size_t)nread);
	carg->proxy_hs_len += (size_t)nread;
	return 0;
}

static void proxy_handle_http_connect_read(context_arg *carg)
{
	int code = 0;
	size_t consumed = 0;
	int rc = proxy_http_connect_parse(carg->proxy_hs_buf, carg->proxy_hs_len, &code, &consumed);

	if (rc == 0)
		return;
	if (rc < 0) {
		carglog(carg, L_ERROR, "proxy CONNECT rejected code %d\n", code);
		proxy_fail(carg, "CONNECT rejected");
		return;
	}
	carglog(carg, L_INFO, "proxy CONNECT ok code %d key %s origin %s:%s\n",
		code, carg->key ? carg->key : "?", carg->host, carg->port);
	proxy_tunnel_ready(carg);
}

static void proxy_handle_socks_greet(context_arg *carg)
{
	unsigned char method;
	unsigned char tmp[512];
	unsigned char *copy;
	size_t n;

	if (carg->proxy_hs_len < 2)
		return;
	if ((unsigned char)carg->proxy_hs_buf[0] != SOCKS5_VER) {
		proxy_fail(carg, "SOCKS5 version");
		return;
	}
	method = (unsigned char)carg->proxy_hs_buf[1];
	if (method == SOCKS5_AUTH_NO_ACCEPT) {
		proxy_fail(carg, "SOCKS5 no acceptable auth");
		return;
	}
	if (method == SOCKS5_AUTH_PASS) {
		n = proxy_socks5_auth_build(tmp, sizeof(tmp), carg->proxy->user, carg->proxy->password);
		if (!n) {
			proxy_fail(carg, "SOCKS5 auth build");
			return;
		}
		copy = malloc(n);
		if (!copy) {
			proxy_fail(carg, "malloc");
			return;
		}
		memcpy(copy, tmp, n);
		carg->proxy_phase = PROXY_PHASE_SOCKS5_AUTH_READ;
		carg->proxy_hs_len = 0;
		proxy_write_bytes(carg, copy, n);
		return;
	}
	if (method != SOCKS5_AUTH_NONE) {
		proxy_fail(carg, "SOCKS5 auth method");
		return;
	}
	if (proxy_send_socks_cmd(carg) < 0)
		proxy_fail(carg, "SOCKS5 cmd build");
}

static void proxy_handle_socks_auth(context_arg *carg)
{
	if (carg->proxy_hs_len < 2)
		return;
	if ((unsigned char)carg->proxy_hs_buf[1] != 0) {
		proxy_fail(carg, "SOCKS5 username/password");
		return;
	}
	if (proxy_send_socks_cmd(carg) < 0)
		proxy_fail(carg, "SOCKS5 cmd build");
}

static void proxy_handle_socks_cmd(context_arg *carg)
{
	int rep = -1;
	struct sockaddr_in bind_addr;
	int rc;

	rc = proxy_socks5_reply_parse((const unsigned char *)carg->proxy_hs_buf, carg->proxy_hs_len, &rep, &bind_addr);
	if (rc == 0)
		return;
	if (rc < 0) {
		carglog(carg, L_ERROR, "SOCKS5 command rejected rep=%d\n", rep);
		proxy_fail(carg, "SOCKS5 command");
		return;
	}
	if (carg->proxy_udp_associate)
		proxy_fill_udp_relay(carg, &bind_addr);
	carglog(carg, L_INFO, "SOCKS5 %s ok key %s origin %s:%s\n",
		carg->proxy_udp_associate ? "UDP ASSOCIATE" : "CONNECT",
		carg->key ? carg->key : "?", carg->host, carg->port);
	proxy_tunnel_ready(carg);
}

void proxy_client_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
	context_arg *carg = stream->data;

	if (nread < 0) {
		proxy_fail(carg, uv_strerror((int)nread));
		return;
	}
	if (nread == 0)
		return;
	if (proxy_hs_append(carg, buf ? buf->base : NULL, nread) < 0) {
		proxy_fail(carg, "handshake overflow");
		return;
	}
	switch (carg->proxy_phase) {
		case PROXY_PHASE_HTTP_CONNECT_READ:
			proxy_handle_http_connect_read(carg);
			break;
		case PROXY_PHASE_SOCKS5_GREET_READ:
			proxy_handle_socks_greet(carg);
			break;
		case PROXY_PHASE_SOCKS5_AUTH_READ:
			proxy_handle_socks_auth(carg);
			break;
		case PROXY_PHASE_SOCKS5_CMD_READ:
			proxy_handle_socks_cmd(carg);
			break;
		default:
			break;
	}
}
