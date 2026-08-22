#include "events/proxy.h"
#include "events/context_arg.h"
#include "common/url.h"
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>

static void test_proxy_parse_urls(void)
{
	proxy_settings *p = proxy_parse_url("http://user:secret@proxy.example:3128");
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, PROXY_TYPE_HTTP, p->type);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "proxy.example", p->host);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3128, p->numport);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "user", p->user);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "secret", p->password);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p->auth_b64);
	proxy_settings_free(p);

	p = proxy_parse_url("socks5://127.0.0.1:1080");
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, PROXY_TYPE_SOCKS5, p->type);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, p->remote_dns);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1080, p->numport);
	proxy_settings_free(p);

	p = proxy_parse_url("socks5h://127.0.0.1");
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, p->remote_dns);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1080, p->numport);
	proxy_settings_free(p);

	p = proxy_parse_url("http://onlyhost");
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 8080, p->numport);
	proxy_settings_free(p);

	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, proxy_parse_url("https://proxy:3128"));
	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, proxy_parse_url("ftp://proxy:3128"));
	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, proxy_parse_url(""));
	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, proxy_parse_url(NULL));
}

static void test_proxy_http_connect_bytes(void)
{
	size_t len = 0;
	char *req = proxy_http_connect_build("origin.example", 443, "dXNlcjpwYXNz", &len);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, req);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "CONNECT origin.example:443 HTTP/1.1") != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "Host: origin.example:443") != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "Proxy-Authorization: Basic dXNlcjpwYXNz") != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)strlen(req), (int)len);
	free(req);

	int code = 0;
	size_t consumed = 0;
	const char *ok = "HTTP/1.1 200 Connection established\r\n\r\n";
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, proxy_http_connect_parse(ok, strlen(ok), &code, &consumed));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 200, code);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)strlen(ok), (int)consumed);

	const char *partial = "HTTP/1.1 200 Connection established\r\n";
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, proxy_http_connect_parse(partial, strlen(partial), &code, &consumed));

	const char *rej = "HTTP/1.1 407 Proxy Authentication Required\r\n\r\n";
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, proxy_http_connect_parse(rej, strlen(rej), &code, &consumed));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 407, code);
}

static void test_proxy_socks5_packets(void)
{
	unsigned char buf[64];
	size_t n;
	size_t need = 0;
	int rep = -1;
	struct sockaddr_in bind_addr;

	n = proxy_socks5_greet_build(buf, sizeof(buf), 0);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, (int)n);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 5, buf[0]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, buf[1]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, buf[2]);

	n = proxy_socks5_greet_build(buf, sizeof(buf), 1);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, (int)n);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, buf[1]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, buf[3]);

	n = proxy_socks5_auth_build(buf, sizeof(buf), "user", "pass");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1 + 1 + 4 + 1 + 4, (int)n);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, buf[0]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, buf[1]);

	n = proxy_socks5_cmd_build(buf, sizeof(buf), 0x01, "8.8.8.8", 53);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10, (int)n);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, buf[3]);

	n = proxy_socks5_cmd_build(buf, sizeof(buf), 0x01, "example.com", 443);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 7 + (int)strlen("example.com"), (int)n);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, buf[3]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)strlen("example.com"), buf[4]);

	n = proxy_socks5_cmd_build(buf, sizeof(buf), 0x03, NULL, 0);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10, (int)n);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, buf[1]);

	unsigned char reply_ok[10] = { 5, 0, 0, 1, 127, 0, 0, 1, 0x04, 0x38 };
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, proxy_socks5_reply_needed(reply_ok, 10, &need));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10, (int)need);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, proxy_socks5_reply_parse(reply_ok, 10, &rep, &bind_addr));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, rep);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1080, ntohs(bind_addr.sin_port));

	unsigned char reply_fail[10] = { 5, 1, 0, 1, 0, 0, 0, 0, 0, 0 };
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, proxy_socks5_reply_parse(reply_fail, 10, &rep, &bind_addr));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, rep);
}

static void test_proxy_udp_wrap_unwrap(void)
{
	unsigned char pkt[128];
	const unsigned char *payload = NULL;
	size_t payload_len = 0;
	const unsigned char data[] = { 0x12, 0x34, 0x56 };

	size_t n = proxy_udp_wrap(pkt, sizeof(pkt), "1.2.3.4", 53, data, sizeof(data));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, n > sizeof(data));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, pkt[0]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, pkt[1]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, pkt[2]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, pkt[3]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, proxy_udp_unwrap(pkt, n, &payload, &payload_len));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)sizeof(data), (int)payload_len);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, memcmp(payload, data, sizeof(data)));

	n = proxy_udp_wrap(pkt, sizeof(pkt), "dns.google", 53, data, sizeof(data));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, n > 0);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, pkt[3]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, proxy_udp_unwrap(pkt, n, &payload, &payload_len));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)sizeof(data), (int)payload_len);
}

static void test_proxy_transport_and_json(void)
{
	proxy_settings *http = proxy_parse_url("http://127.0.0.1:3128");
	proxy_settings *socks = proxy_parse_url("socks5://127.0.0.1:1080");
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, http);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, socks);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, proxy_ok_for_transport(http, APROTO_UDP));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, proxy_ok_for_transport(socks, APROTO_UDP));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, proxy_ok_for_transport(http, APROTO_TCP));
	proxy_settings_free(http);
	proxy_settings_free(socks);

	host_aggregator_info *hi = parse_url("http://example.com/metrics", strlen("http://example.com/metrics"));
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
	json_error_t error;
	json_t *root = json_loads("{\"proxy\":\"http://user:pw@10.0.0.1:3128\",\"key\":\"proxy-ut\"}", 0, &error);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
	char *msg = strdup("GET /metrics HTTP/1.1\r\nHost: example.com\r\n\r\n");
	context_arg *carg = context_arg_json_fill(root, hi, NULL, "http", msg, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg->proxy);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, PROXY_TYPE_HTTP, carg->proxy->type);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3128, carg->proxy->numport);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(carg->mesg, "GET http://example.com/metrics HTTP/1.1") != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(carg->mesg, "Proxy-Authorization: Basic") != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(carg->mesg, "Host: example.com") != NULL);

	host_aggregator_info *hi_udp = parse_url("udp://8.8.8.8:53", strlen("udp://8.8.8.8:53"));
	json_t *root_udp = json_loads("{\"proxy\":\"http://127.0.0.1:3128\"}", 0, &error);
	char *udp_msg = strdup("dns");
	context_arg *ucarg = context_arg_json_fill(root_udp, hi_udp, NULL, "dns", udp_msg, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ucarg);
	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, ucarg->proxy);

	carg_free(ucarg);
	json_decref(root_udp);
	url_free(hi_udp);
	carg_free(carg);
	json_decref(root);
	url_free(hi);
}

static void test_proxy_absolute_uri_rewrite(void)
{
	context_arg carg;
	memset(&carg, 0, sizeof(carg));
	carg.proto = APROTO_HTTP;
	carg.numport = 80;
	strlcpy(carg.host, "example.com", sizeof(carg.host));
	carg.proxy = proxy_parse_url("http://u:p@proxy:8080");
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg.proxy);
	char *msg = strdup("GET /metrics HTTP/1.1\r\nUser-Agent: t\r\nHost: example.com\r\n\r\n");
	aconf_mesg_set(&carg, msg, strlen(msg));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_request_apply_proxy(&carg));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(carg.mesg, "GET http://example.com/metrics HTTP/1.1") != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(carg.mesg, "Proxy-Authorization: Basic") != NULL);
	free(carg.buffer);
	free(carg.mesg);
	proxy_settings_free(carg.proxy);
}

static void test_proxy_suite(void)
{
	test_proxy_parse_urls();
	test_proxy_http_connect_bytes();
	test_proxy_socks5_packets();
	test_proxy_udp_wrap_unwrap();
	test_proxy_transport_and_json();
	test_proxy_absolute_uri_rewrite();
}
