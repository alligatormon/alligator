#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include "common/aggregator.h"
#include "common/http.h"
#include "events/context_arg.h"
#include "dstructures/ht.h"
#include "main.h"

extern aconf *ac;

typedef struct await_mock_http {
	uv_tcp_t server;
	uv_tcp_t client;
	uv_write_t write_req;
	char *response;
	size_t response_len;
	int hang;
	int accepted;
	int client_used;
	int port;
	int writes;
} await_mock_http;

static void await_runtime_init(void)
{
	if (!ac->aggregators)
		ac->aggregators = alligator_ht_init(NULL);
	if (!ac->aggregator)
		ac->aggregator = alligator_ht_init(NULL);
	if (!ac->uggregator)
		ac->uggregator = alligator_ht_init(NULL);
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);
}

static void await_mock_on_write(uv_write_t *req, int status)
{
	await_mock_http *m = req->data;
	(void)status;
	if (m && m->client_used && !uv_is_closing((uv_handle_t *)&m->client))
		uv_close((uv_handle_t *)&m->client, NULL);
}

static void await_mock_on_conn(uv_stream_t *server, int status)
{
	await_mock_http *m = server->data;
	uv_buf_t buf;

	if (status < 0 || !m)
		return;
	if (m->accepted)
		return;
	if (uv_tcp_init(server->loop, &m->client))
		return;
	m->client_used = 1;
	if (uv_accept(server, (uv_stream_t *)&m->client))
		return;
	m->accepted = 1;
	if (m->hang)
		return;
	m->write_req.data = m;
	buf = uv_buf_init(m->response, (unsigned int)m->response_len);
	if (uv_write(&m->write_req, (uv_stream_t *)&m->client, &buf, 1, await_mock_on_write) == 0)
		m->writes++;
}

static int await_mock_listen(await_mock_http *m, const char *response, int hang)
{
	struct sockaddr_in addr;
	struct sockaddr_in got;
	int namelen = sizeof(got);

	memset(m, 0, sizeof(*m));
	m->hang = hang;
	if (response) {
		m->response = strdup(response);
		m->response_len = strlen(response);
	}
	if (uv_tcp_init(ac->loop, &m->server))
		return -1;
	m->server.data = m;
	uv_ip4_addr("127.0.0.1", 0, &addr);
	if (uv_tcp_bind(&m->server, (const struct sockaddr *)&addr, 0))
		return -1;
	if (uv_listen((uv_stream_t *)&m->server, 128, await_mock_on_conn))
		return -1;
	if (uv_tcp_getsockname(&m->server, (struct sockaddr *)&got, &namelen))
		return -1;
	m->port = ntohs(got.sin_port);
	return m->port > 0 ? 0 : -1;
}

static void await_mock_stop(await_mock_http *m)
{
	int i;

	if (m->client_used && !uv_is_closing((uv_handle_t *)&m->client))
		uv_close((uv_handle_t *)&m->client, NULL);
	if (!uv_is_closing((uv_handle_t *)&m->server))
		uv_close((uv_handle_t *)&m->server, NULL);
	for (i = 0; i < 32; i++) {
		if (!uv_run(ac->loop, UV_RUN_NOWAIT))
			break;
	}
	free(m->response);
	m->response = NULL;
}

static void test_aggregator_oneshot_await_http_success(void)
{
	await_mock_http mock;
	aggregator_await_res_t res;
	context_arg hint;
	char url[64];
	char *query;
	int rc;

	await_runtime_init();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		await_mock_listen(&mock,
			"HTTP/1.0 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello", 0));

	memset(&hint, 0, sizeof(hint));
	hint.timeout = 2000;
	snprintf(url, sizeof(url), "http://127.0.0.1:%d/ok", mock.port);
	query = gen_http_query(HTTP_GET, "/ok", NULL, "127.0.0.1", "alligator", NULL, NULL, NULL, NULL, NULL);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, query);

	memset(&res, 0, sizeof(res));
	rc = aggregator_oneshot_await(&hint, url, strlen(url), query, strlen(query),
		NULL, "oneshot_await_ok", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL, &res);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, rc);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 200, res.http_code);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, res.body != NULL && res.body_len >= 5);
	if (res.body && res.body_len >= 5)
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, memcmp(res.body, "hello", 5));

	aggregator_await_res_free(&res);
	await_mock_stop(&mock);
}

static void test_aggregator_oneshot_await_http_error(void)
{
	await_mock_http mock;
	aggregator_await_res_t res;
	context_arg hint;
	char url[64];
	char *query;
	int rc;

	await_runtime_init();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		await_mock_listen(&mock,
			"HTTP/1.0 500 Internal Server Error\r\nContent-Length: 4\r\nConnection: close\r\n\r\nbad\n", 0));

	memset(&hint, 0, sizeof(hint));
	hint.timeout = 2000;
	snprintf(url, sizeof(url), "http://127.0.0.1:%d/fail", mock.port);
	query = gen_http_query(HTTP_GET, "/fail", NULL, "127.0.0.1", "alligator", NULL, NULL, NULL, NULL, NULL);
	memset(&res, 0, sizeof(res));
	rc = aggregator_oneshot_await(&hint, url, strlen(url), query, strlen(query),
		NULL, "oneshot_await_err", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL, &res);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, rc);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 500, res.http_code);

	aggregator_await_res_free(&res);
	await_mock_stop(&mock);
}

static void test_aggregator_oneshot_await_connect_refuse(void)
{
	aggregator_await_res_t res;
	context_arg hint;
	char url[80];
	char *query;
	int rc;

	await_runtime_init();
	memset(&hint, 0, sizeof(hint));
	hint.timeout = 1000;
	snprintf(url, sizeof(url), "http://127.0.0.1:1/refused");
	query = gen_http_query(HTTP_GET, "/refused", NULL, "127.0.0.1", "alligator", NULL, NULL, NULL, NULL, NULL);
	memset(&res, 0, sizeof(res));
	rc = aggregator_oneshot_await(&hint, url, strlen(url), query, strlen(query),
		NULL, "oneshot_await_refuse", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL, &res);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, rc < 0);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, res.http_code);

	aggregator_await_res_free(&res);
}

static void test_aggregator_oneshot_await_timeout(void)
{
	await_mock_http mock;
	aggregator_await_res_t res;
	context_arg hint;
	char url[64];
	char *query;
	int rc;

	await_runtime_init();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, await_mock_listen(&mock, NULL, 1));

	memset(&hint, 0, sizeof(hint));
	hint.timeout = 200;
	snprintf(url, sizeof(url), "http://127.0.0.1:%d/hang", mock.port);
	query = gen_http_query(HTTP_GET, "/hang", NULL, "127.0.0.1", "alligator", NULL, NULL, NULL, NULL, NULL);
	memset(&res, 0, sizeof(res));
	rc = aggregator_oneshot_await(&hint, url, strlen(url), query, strlen(query),
		NULL, "oneshot_await_timeout", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL, &res);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, rc < 0);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, UV_ETIMEDOUT, rc);

	aggregator_await_res_free(&res);
	await_mock_stop(&mock);
}

static void test_aggregator_oneshot_await_hostname_dns(void)
{
	await_mock_http mock;
	aggregator_await_res_t res;
	context_arg hint;
	char url[80];
	char *query;
	int rc;

	await_runtime_init();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		await_mock_listen(&mock,
			"HTTP/1.0 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\ndns", 0));

	memset(&hint, 0, sizeof(hint));
	hint.timeout = 3000;
	snprintf(url, sizeof(url), "http://localhost:%d/dns", mock.port);
	query = gen_http_query(HTTP_GET, "/dns", NULL, "localhost", "alligator", NULL, NULL, NULL, NULL, NULL);
	memset(&res, 0, sizeof(res));
	rc = aggregator_oneshot_await(&hint, url, strlen(url), query, strlen(query),
		NULL, "oneshot_await_dns", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL, &res);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, rc);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 200, res.http_code);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, res.body != NULL && res.body_len >= 3);

	aggregator_await_res_free(&res);
	await_mock_stop(&mock);
}

static void test_aggregator_oneshot_await_ocsp_post(void)
{
	await_mock_http mock;
	aggregator_await_res_t res;
	context_arg hint;
	alligator_ht *env;
	string body;
	char url[80];
	char *query;
	char clen[16];
	const char *der = "OCSP";
	int rc;
	size_t qlen;
	char *sep;

	await_runtime_init();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		await_mock_listen(&mock,
			"HTTP/1.0 200 OK\r\nContent-Type: application/ocsp-response\r\nContent-Length: 7\r\nConnection: close\r\n\r\nocsp-ok", 0));

	memset(&hint, 0, sizeof(hint));
	hint.timeout = 2000;
	env = alligator_ht_init(NULL);
	env_struct_push_alloc(env, "Content-Type", "application/ocsp-request");
	snprintf(clen, sizeof(clen), "%zu", strlen(der));
	env_struct_push_alloc(env, "Content-Length", clen);
	env_struct_push_alloc(env, "Connection", "close");
	body.s = (char *)der;
	body.l = strlen(der);
	body.m = body.l;
	snprintf(url, sizeof(url), "http://127.0.0.1:%d/ocsp", mock.port);
	query = gen_http_query(HTTP_POST, "/ocsp", NULL, "127.0.0.1", "alligator", NULL, NULL, env, NULL, &body);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, query);
	sep = strstr(query, "\r\n\r\n");
	qlen = sep ? (size_t)(sep - query) + 4 + body.l : strlen(query);

	memset(&res, 0, sizeof(res));
	rc = aggregator_oneshot_await(&hint, url, strlen(url), query, qlen,
		NULL, "oneshot_await_ocsp", NULL, NULL, 1, NULL, NULL, 0, NULL, NULL, &res);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, rc);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 200, res.http_code);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, res.body != NULL && strstr(res.body, "ocsp-ok") != NULL);

	aggregator_await_res_free(&res);
	env_free(env);
	await_mock_stop(&mock);
}

static void test_aggregator_oneshot_await_https_url_refuse(void)
{
	aggregator_await_res_t res;
	context_arg hint;
	char url[80];
	char *query;
	int rc;

	await_runtime_init();
	memset(&hint, 0, sizeof(hint));
	hint.timeout = 1000;
	snprintf(url, sizeof(url), "https://127.0.0.1:1/ocsp");
	query = gen_http_query(HTTP_POST, "/ocsp", NULL, "127.0.0.1", "alligator", NULL, NULL, NULL, NULL, NULL);
	memset(&res, 0, sizeof(res));
	rc = aggregator_oneshot_await(&hint, url, strlen(url), query, strlen(query),
		NULL, "oneshot_await_https", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL, &res);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, rc < 0);

	aggregator_await_res_free(&res);
}

static void test_aggregator_oneshot_await_suite(void)
{
	int i;

	test_aggregator_oneshot_await_http_success();
	test_aggregator_oneshot_await_http_error();
	test_aggregator_oneshot_await_connect_refuse();
	test_aggregator_oneshot_await_timeout();
	test_aggregator_oneshot_await_hostname_dns();
	test_aggregator_oneshot_await_ocsp_post();
	test_aggregator_oneshot_await_https_url_refuse();
	for (i = 0; i < 64; i++) {
		if (!uv_run(ac->loop, UV_RUN_NOWAIT))
			break;
	}
}
