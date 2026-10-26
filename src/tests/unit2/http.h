#define TEST_AUTH_HEADER_1 "Authorization"
#define TEST_AUTH_BASIC_HEADER_1_1 "root:qwerty"
#define TEST_AUTH_BASIC_QUERY_1_1 "GET / HTTP/1.1\r\nAuthorization: basic cm9vdDpxd2VydHk=\r\n\r\n"
#define TEST_AUTH_BASIC_RES_1_1 1
#define TEST_AUTH_BASIC_HEADER_1_2 "admin:user"
#define TEST_AUTH_BASIC_QUERY_1_2 "GET / HTTP/1.1\r\nAuthorization: basic YWRtaW46dXNlcg==\r\n\r\n"
#define TEST_AUTH_BASIC_RES_1_2 1
#define TEST_AUTH_BASIC_HEADER_1_3 "user:password"
#define TEST_AUTH_BASIC_QUERY_1_3 "GET / HTTP/1.1\r\nAuthorization: basic dXNlcjpwYXNzd29yZA==\r\n\r\n"
#define TEST_AUTH_BASIC_RES_1_3 1
#define TEST_AUTH_BASIC_QUERY_1_4 "GET / HTTP/1.1\r\nAuthorization: basic ZXJyOmVycg==\r\n\r\n"
#define TEST_AUTH_BASIC_RES_1_4 0
#define TEST_AUTH_BASIC_QUERY_1_5 "GET / HTTP/1.1\r\n\r\n"
#define TEST_AUTH_BASIC_RES_1_5 -1

#define TEST_AUTH_BEARER_HEADER_2_1 "ZV0aFdP2kx44WVWRSkFCQsDhKvAHuA6Hhw4Kzr6OhoGe13RKxyUjgZo7ODco34sq"
#define TEST_AUTH_BEARER_QUERY_2_1 "GET / HTTP/1.1\r\nAuthorization: bearer ZV0aFdP2kx44WVWRSkFCQsDhKvAHuA6Hhw4Kzr6OhoGe13RKxyUjgZo7ODco34sq\r\n\r\n"
#define TEST_AUTH_BEARER_RES_2_1 1

#define TEST_AUTH_BEARER_HEADER_2_2 "F89rMiV1h8YyVhMIk9rI1GLSxW3fquSHCjf1PuAReABa47ivUbjshmTVZkD5bpXs"
#define TEST_AUTH_BEARER_QUERY_2_2 "GET / HTTP/1.1\r\nAuthorization: bearer F89rMiV1h8YyVhMIk9rI1GLSxW3fquSHCjf1PuAReABa47ivUbjshmTVZkD5bpXs\r\n\r\n"
#define TEST_AUTH_BEARER_RES_2_2 1
#define TEST_AUTH_BEARER_QUERY_2_3 "GET / HTTP/1.1\r\nAuthorization: bearer F89rMiV1h8YyVhMIk9rI11PuAReABa47ivUbjshmTVZkD5bpXs\r\n\r\n"
#define TEST_AUTH_BEARER_RES_2_3 0

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "main.h"
#include "common/selector.h"
#include "events/context_arg.h"
#include "common/base64.h"
#include "common/auth.h"
#include "common/authorize.h"
#include "common/http_limit.h"
#include "parsers/http_proto.h"
#include <time.h>
#include "parsers/multiparser.h"
#include "common/http_entrypoint.h"
#include "dstructures/ht.h"
int http_parser(char *buf, size_t len, string *response, context_arg *carg);
string* tcp_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings);
string* blackbox_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings);
void tcp_parser_push();
void blackbox_parser_push();
void http_parser_push();
void process_parser_push();

void check_basic(context_arg *carg) {
	http_reply_data *http_data;
	int8_t access;
	http_data = http_proto_get_request_data(TEST_AUTH_BASIC_QUERY_1_1, strlen(TEST_AUTH_BASIC_QUERY_1_1), carg->auth_header);
	access = http_check_auth(carg, http_data);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, TEST_AUTH_BASIC_RES_1_1, access);

	http_data = http_proto_get_request_data(TEST_AUTH_BASIC_QUERY_1_2, strlen(TEST_AUTH_BASIC_QUERY_1_2), carg->auth_header);
	access = http_check_auth(carg, http_data);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, TEST_AUTH_BASIC_RES_1_2, access);

	http_data = http_proto_get_request_data(TEST_AUTH_BASIC_QUERY_1_3, strlen(TEST_AUTH_BASIC_QUERY_1_3), carg->auth_header);
	access = http_check_auth(carg, http_data);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, TEST_AUTH_BASIC_RES_1_3, access);

	http_data = http_proto_get_request_data(TEST_AUTH_BASIC_QUERY_1_4, strlen(TEST_AUTH_BASIC_QUERY_1_4), carg->auth_header);
	access = http_check_auth(carg, http_data);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, TEST_AUTH_BASIC_RES_1_4, access);

	http_data = http_proto_get_request_data(TEST_AUTH_BASIC_QUERY_1_5, strlen(TEST_AUTH_BASIC_QUERY_1_5), carg->auth_header);
	access = http_check_auth(carg, http_data);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, TEST_AUTH_BASIC_RES_1_5, access);
}

void check_bearer(context_arg *carg) {
	http_reply_data *http_data;
	int8_t access;
	http_data = http_proto_get_request_data(TEST_AUTH_BEARER_QUERY_2_1, strlen(TEST_AUTH_BEARER_QUERY_2_1), carg->auth_header);
	access = http_check_auth(carg, http_data);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, TEST_AUTH_BEARER_RES_2_1, access);

	http_data = http_proto_get_request_data(TEST_AUTH_BEARER_QUERY_2_2, strlen(TEST_AUTH_BEARER_QUERY_2_2), carg->auth_header);
	access = http_check_auth(carg, http_data);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, TEST_AUTH_BEARER_RES_2_2, access);

	http_data = http_proto_get_request_data(TEST_AUTH_BEARER_QUERY_2_3, strlen(TEST_AUTH_BEARER_QUERY_2_3), carg->auth_header);
	access = http_check_auth(carg, http_data);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, TEST_AUTH_BEARER_RES_2_3, access);
}

void test_http_access_1()
{
	context_arg *carg = calloc(1, sizeof(*carg));
	size_t b64sz;

	carg->auth_basic = alligator_ht_init(NULL);

    http_auth_push(carg->auth_basic, base64_encode(TEST_AUTH_BASIC_HEADER_1_1, strlen(TEST_AUTH_BASIC_HEADER_1_1), &b64sz));
    http_auth_push(carg->auth_basic, base64_encode(TEST_AUTH_BASIC_HEADER_1_2, strlen(TEST_AUTH_BASIC_HEADER_1_2), &b64sz));
    http_auth_push(carg->auth_basic, base64_encode(TEST_AUTH_BASIC_HEADER_1_3, strlen(TEST_AUTH_BASIC_HEADER_1_3), &b64sz));

	carg->auth_header = TEST_AUTH_HEADER_1;

	check_basic(carg);
}

void test_http_access_2()
{
	context_arg *carg = calloc(1, sizeof(*carg));
	size_t b64sz;

	carg->auth_basic = alligator_ht_init(NULL);

    http_auth_push(carg->auth_basic, base64_encode(TEST_AUTH_BASIC_HEADER_1_1, strlen(TEST_AUTH_BASIC_HEADER_1_1), &b64sz));
    http_auth_push(carg->auth_basic, base64_encode(TEST_AUTH_BASIC_HEADER_1_2, strlen(TEST_AUTH_BASIC_HEADER_1_2), &b64sz));
    http_auth_push(carg->auth_basic, base64_encode(TEST_AUTH_BASIC_HEADER_1_3, strlen(TEST_AUTH_BASIC_HEADER_1_3), &b64sz));

	carg->auth_bearer = alligator_ht_init(NULL);
    http_auth_push(carg->auth_bearer, TEST_AUTH_BEARER_HEADER_2_1);
    http_auth_push(carg->auth_bearer, TEST_AUTH_BEARER_HEADER_2_2);

	carg->auth_header = TEST_AUTH_HEADER_1;

	check_basic(carg);
	check_bearer(carg);
}

void test_http_proto_parsers()
{
    char *req = "PATCH /api/v1/config HTTP/1.1\r\nX-Expire-Time: 5s\r\nAuthorization: Token abc\r\nContent-Length: 4\r\n\r\ntest";
    http_reply_data *r = http_proto_get_request_data(req, strlen(req), "Authorization");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, r);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, HTTP_METHOD_PATCH, r->method);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/api/v1/config", r->uri);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 11, r->http_version);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, r->content_length);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 5, r->expire);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, r->auth_other);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "Token abc", r->auth_other);
    http_reply_data_free(r);

    char *resp = "HTTP/1.1 302 Found\r\nLocation: /next\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n";
    http_reply_data *hr = http_reply_parser(resp, strlen(resp));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hr);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 302, hr->http_code);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, hr->chunked_expect);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hr->location);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/next", hr->location);
    http_reply_data_free(hr);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        http_reply_parser("NOTHTTP", strlen("NOTHTTP")) != NULL);
}

void test_http_proto_methods_and_edges()
{
    struct method_case {
        const char *req;
        uint8_t method;
    } cases[] = {
        {"GET /g HTTP/1.1\r\n\r\n", HTTP_METHOD_GET},
        {"POST /p HTTP/1.1\r\n\r\n", HTTP_METHOD_POST},
        {"PUT /u HTTP/1.1\r\n\r\n", HTTP_METHOD_PUT},
        {"DELETE /d HTTP/1.1\r\n\r\n", HTTP_METHOD_DELETE},
        {"OPTIONS /o HTTP/1.1\r\n\r\n", HTTP_METHOD_OPTIONS},
        {"HEAD /h HTTP/1.1\r\n\r\n", HTTP_METHOD_HEAD},
        {"CONNECT /c HTTP/1.1\r\n\r\n", HTTP_METHOD_CONNECT},
        {"TRACE /t HTTP/1.1\r\n\r\n", HTTP_METHOD_TRACE},
        {"PATCH /x HTTP/1.1\r\n\r\n", HTTP_METHOD_PATCH},
    };

    for (uint64_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        http_reply_data *r = http_proto_get_request_data((char*)cases[i].req, strlen(cases[i].req), "Authorization");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, r);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, cases[i].method, r->method);
        http_reply_data_free(r);
    }

    http_reply_data *resp_req = http_proto_get_request_data("HTTP/1.1 200 OK\r\n\r\n", strlen("HTTP/1.1 200 OK\r\n\r\n"), "Authorization");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, resp_req);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, HTTP_METHOD_RESPONSE, resp_req->method);
    http_reply_data_free(resp_req);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        http_proto_get_request_data("BREW /coffee HTTP/1.1\r\n\r\n", strlen("BREW /coffee HTTP/1.1\r\n\r\n"), "Authorization") != NULL);

    http_reply_data *old_nl = http_reply_parser("HTTP/1.0 200 OK\nHeader: x\n\nbody", strlen("HTTP/1.0 200 OK\nHeader: x\n\nbody"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, old_nl);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10, old_nl->http_version);
    http_reply_data_free(old_nl);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        http_reply_parser("HTTP/1.1 700 Nope\r\n\r\n", strlen("HTTP/1.1 700 Nope\r\n\r\n")) != NULL);
}

static char *mp_capture_buf = NULL;
static size_t mp_capture_cap = 0;
static size_t mp_capture_len = 0;

static void mp_capture_reset(void)
{
    mp_capture_len = 0;
    if (mp_capture_buf && mp_capture_cap)
        mp_capture_buf[0] = 0;
}

static void mp_capture_ensure(size_t need)
{
    size_t want = need + 1;
    if (want <= mp_capture_cap)
        return;
    size_t n = want < 512 ? 512 : want;
    char *p = realloc(mp_capture_buf, n);
    if (!p)
        return;
    mp_capture_buf = p;
    mp_capture_cap = n;
}

static void mp_capture_done(void)
{
    free(mp_capture_buf);
    mp_capture_buf = NULL;
    mp_capture_cap = 0;
    mp_capture_len = 0;
}

static void mp_capture_handler(char *metrics, size_t size, context_arg *carg)
{
    if (!mp_capture_buf || mp_capture_cap < 2)
        return;
    size_t n = size < mp_capture_cap - 1 ? size : mp_capture_cap - 1;
    memcpy(mp_capture_buf, metrics, n);
    mp_capture_buf[n] = 0;
    mp_capture_len = n;
    if (carg)
        carg->parser_status = 1;
}

void test_multiparser_proxy_paths()
{
    context_arg *carg = calloc(1, sizeof(*carg));
    char *msg = "HTTP/1.1 200 OK\r\nHeader: x\r\n\r\npayload";

    mp_capture_ensure(511);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mp_capture_buf);
    mp_capture_reset();
    carg->is_http_query = 1;
    carg->headers_pass = 0;
    carg->headers_size = 0;
    alligator_multiparser(msg, strlen(msg), mp_capture_handler, NULL, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parsed);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "payload", mp_capture_buf);

    mp_capture_reset();
    carg->headers_pass = 1;
    alligator_multiparser(msg, strlen(msg), mp_capture_handler, NULL, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, mp_capture_len > strlen("payload"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(mp_capture_buf, "HTTP/1.1 200 OK") != NULL);

    mp_capture_reset();
    carg->headers_pass = 0;
    carg->headers_size = strlen(msg);
    alligator_multiparser(msg, strlen(msg), mp_capture_handler, NULL, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, mp_capture_len);

    /* NULL input should be ignored safely */
    alligator_multiparser(NULL, 0, mp_capture_handler, NULL, carg);
    free(carg);
    mp_capture_done();
}

void test_http_parser_route_and_auth_edges()
{
    context_arg *carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    /* http_parser -> http_proto_get_request_data expects non-NULL auth_header */
    carg->auth_header = "Authorization";
    string *response = string_init(4096);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, response);

    /* OPTIONS path should return allow list */
    char *req_options = "OPTIONS / HTTP/1.1\r\n\r\n";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_options, strlen(req_options), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "405") == NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "Allow: OPTIONS, GET, PUT, POST") != NULL);

    /* HEAD should be method not allowed */
    string_null(response);
    char *req_head = "HEAD / HTTP/1.1\r\n\r\n";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_head, strlen(req_head), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "405 Method Not Allowed") != NULL);

    /* GET /ready should return ready answer */
    string_null(response);
    char *req_ready = "GET /ready HTTP/1.1\r\n\r\n";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_ready, strlen(req_ready), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "200 OK") != NULL);

    /* GET / should return OpenMetrics content type and EOF marker */
    string_null(response);
    char *req_metrics = "GET / HTTP/1.1\r\n\r\n";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_metrics, strlen(req_metrics), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "Content-Type: application/openmetrics-text; version=1.0.0; charset=utf-8\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "# EOF\n") != NULL);

    /* entrypoint format prometheus config overrides default OpenMetrics */
    string_null(response);
    carg->metrics_openmetrics_set = 1;
    carg->metrics_openmetrics = 0;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_metrics, strlen(req_metrics), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "Content-Type: text/plain\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, strstr(response->s, "# EOF\n") != NULL);

    /* query ?format=openmetrics overrides entrypoint format prometheus */
    string_null(response);
    char *req_metrics_om = "GET /?format=openmetrics HTTP/1.1\r\n\r\n";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_metrics_om, strlen(req_metrics_om), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "Content-Type: application/openmetrics-text; version=1.0.0; charset=utf-8\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "# EOF\n") != NULL);

    carg->metrics_openmetrics_set = 0;
    carg->metrics_openmetrics = 0;

    /* GET /probe with unknown module: HTTP 400 + Content-Length so keep-alive clients do not hang */
    {
        alligator_ht *saved_probe = ac->probe;
        char *hdr_end;
        char *cl;
        size_t declared;
        size_t actual;

        ac->probe = alligator_ht_init(NULL);
        string_null(response);
        char *req_probe = "GET /probe?module=http_2xx&target=example.com HTTP/1.1\r\n\r\n";
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_probe, strlen(req_probe), response, carg));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "400 Bad Request") != NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "no such module 'http_2xx'") != NULL);
        hdr_end = strstr(response->s, "\r\n\r\n");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hdr_end);
        cl = strstr(response->s, "Content-Length:");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cl);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, cl < hdr_end);
        declared = (size_t)strtoull(cl + strlen("Content-Length:"), NULL, 10);
        actual = response->l - (size_t)(hdr_end + 4 - response->s);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)declared, (int)actual);

        string_null(response);
        char *req_probe_nomod = "GET /probe?target=example.com HTTP/1.1\r\n\r\n";
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_probe_nomod, strlen(req_probe_nomod), response, carg));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "400 Bad Request") != NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "no arg 'module'") != NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "Content-Length:") != NULL);

        alligator_ht_done(ac->probe);
        free(ac->probe);
        ac->probe = saved_probe;
    }

    /* unauthorized (auth required header missing) */
    string_null(response);
    carg->auth_basic = alligator_ht_init(NULL);
    char *req_no_auth = "GET / HTTP/1.1\r\n\r\n";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_no_auth, strlen(req_no_auth), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "401 Unauthorized") != NULL);

    /* forbidden (auth present but wrong) */
    string_null(response);
    size_t b64sz = 0;
    http_auth_push(carg->auth_basic, base64_encode("root:qwerty", strlen("root:qwerty"), &b64sz));
    char *req_bad_auth = "GET / HTTP/1.1\r\nAuthorization: basic ZXJyOmVycg==\r\n\r\n";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req_bad_auth, strlen(req_bad_auth), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "403 Access Forbidden") != NULL);

    alligator_ht_foreach_arg(carg->auth_basic, http_auth_foreach_free, NULL);
    alligator_ht_done(carg->auth_basic);
    free(carg->auth_basic);
    string_free(response);
    free(carg);
}

void test_multiparser_mesg_helpers()
{
    host_aggregator_info *hi = calloc(1, sizeof(*hi));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    hi->query = "/metrics";
    string *msg = tcp_mesg(hi, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, msg);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/metrics", msg->s);
    string_free(msg);

    hi->query = NULL;
    msg = tcp_mesg(hi, NULL, NULL, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, msg != NULL);
    free(hi);

    host_aggregator_info *hb = calloc(1, sizeof(*hb));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hb);
    hb->proto = APROTO_HTTP;
    hb->query = "/x";
    hb->host = strdup("example.org");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hb->host);
    hb->auth = NULL;
    msg = blackbox_mesg(hb, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, msg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(msg->s, "GET /x HTTP/1.0\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(msg->s, "Host: example.org\r\n") != NULL);
    string_free(msg);

    hb->proto = APROTO_TCP;
    hb->query = "PING\r\n";
    msg = blackbox_mesg(hb, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, msg);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "PING\r\n", msg->s);
    string_free(msg);

    hb->query = NULL;
    msg = blackbox_mesg(hb, NULL, NULL, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, msg != NULL);
    free(hb->host);
    free(hb);
}

void test_multiparser_fallback_and_null_helper()
{
    context_arg *carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    carg->auth_header = "Authorization";
    string *response = string_init(512);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, response);

    mp_capture_ensure(511);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mp_capture_buf);
    mp_capture_reset();
    /* Safe parser bookkeeping path with explicit handler (no multicollector side effects). */
    alligator_multiparser("mini_payload", strlen("mini_payload"), mp_capture_handler, response, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parsed);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)strlen("mini_payload"), (int)mp_capture_len);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->exec_time_finish.sec >= carg->exec_time.sec);

    /* blackbox_null is tiny but currently uncovered helper. */
    carg->parser_status = 0;
    blackbox_null(NULL, 0, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->parser_status);

    string_free(response);
    free(carg);
    mp_capture_done();
}

static void test_free_actx_by_key(alligator_ht *ht, const char *key)
{
    aggregate_context *actx = alligator_ht_remove(ht, actx_compare, key, tommy_strhash_u32(0, key));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, actx);
    free(actx->handler);
    free(actx->key);
    free(actx);
}

void test_multiparser_parser_push_paths()
{
    alligator_ht *saved = ac->aggregate_ctx;
    ac->aggregate_ctx = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->aggregate_ctx);

    tcp_parser_push();
    blackbox_parser_push();
    http_parser_push();
    process_parser_push();

    aggregate_context *tcp = alligator_ht_search(ac->aggregate_ctx, actx_compare, "tcp", tommy_strhash_u32(0, "tcp"));
    aggregate_context *blackbox = alligator_ht_search(ac->aggregate_ctx, actx_compare, "blackbox", tommy_strhash_u32(0, "blackbox"));
    aggregate_context *http = alligator_ht_search(ac->aggregate_ctx, actx_compare, "http", tommy_strhash_u32(0, "http"));
    aggregate_context *process = alligator_ht_search(ac->aggregate_ctx, actx_compare, "process", tommy_strhash_u32(0, "process"));

    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, tcp);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, blackbox);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, http);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, process);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, tcp->handlers);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, blackbox->handlers);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http->handlers);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, process->handlers);

    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "tcp", tcp->handler[0].key);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "blackbox", blackbox->handler[0].key);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "http", http->handler[0].key);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "process", process->handler[0].key);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, tcp->handler[0].mesg_func == tcp_mesg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, blackbox->handler[0].mesg_func == blackbox_mesg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http->handler[0].mesg_func == blackbox_mesg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, process->handler[0].mesg_func == NULL);

    test_free_actx_by_key(ac->aggregate_ctx, "tcp");
    test_free_actx_by_key(ac->aggregate_ctx, "blackbox");
    test_free_actx_by_key(ac->aggregate_ctx, "http");
    test_free_actx_by_key(ac->aggregate_ctx, "process");
    alligator_ht_done(ac->aggregate_ctx);
    free(ac->aggregate_ctx);
    ac->aggregate_ctx = saved;
}

static int8_t authorize_check_req(context_arg *carg, const char *req)
{
    http_reply_data *http = http_proto_get_request_data((char *)req, strlen(req), "Authorization");
    int8_t rc;

    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, http);
    if (!http)
        return -2;
    rc = http_check_auth(carg, http);
    http_reply_data_free(http);
    return rc;
}

static void authorize_check_deny(context_arg *carg, const char *req, int8_t expect, const char *reason)
{
    http_reply_data *http = http_proto_get_request_data((char *)req, strlen(req), "Authorization");
    int8_t rc;

    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, http);
    rc = http_check_auth(carg, http);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, expect, rc);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, (char *)reason, http->auth_deny);
    http_reply_data_free(http);
}

static int authorize_body_has_code(const char *body, const char *secret, uint64_t period, uint64_t t0, uint64_t t1)
{
    char hex[AUTHORIZE_HEX_LEN + 1];
    uint64_t b;

    if (!body || !period)
        return 0;
    if (t1 < t0)
        t1 = t0;
    for (b = t0 / period; b <= t1 / period; b++)
    {
        if (!authorize_compute(secret, b, hex))
            return 0;
        if (strstr(body, hex))
            return 1;
    }
    return 0;
}

static int authorize_body_has_bound(const char *body, const char *secret, uint64_t period, const authorize_bind *binds, uint8_t nbinds, const char *const *values, uint64_t t0, uint64_t t1)
{
    char hex[AUTHORIZE_HEX_LEN + 1];
    uint64_t b;

    if (!body || !period)
        return 0;
    if (t1 < t0)
        t1 = t0;
    for (b = t0 / period; b <= t1 / period; b++)
    {
        if (!authorize_compute_bound(secret, b, binds, nbinds, values, hex))
            return 0;
        if (strstr(body, hex))
            return 1;
    }
    return 0;
}

void test_authorize_secret_and_satisfy(void)
{
    char cur[AUTHORIZE_HEX_LEN + 1];
    char prev[AUTHORIZE_HEX_LEN + 1];
    char next[AUTHORIZE_HEX_LEN + 1];
    char far[AUTHORIZE_HEX_LEN + 1];
    char again[AUTHORIZE_HEX_LEN + 1];
    char other[AUTHORIZE_HEX_LEN + 1];
    char req[640];
    int64_t now = 100000;
    uint64_t period = 10;
    uint64_t bucket = (uint64_t)now / period;
    context_arg *carg;
    size_t b64sz = 0;
    char *b64;
    string *response;
    uint64_t t0;
    uint64_t t1;
    const char *bad64 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket, cur));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket, again));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, cur, again);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, AUTHORIZE_HEX_LEN, (int)strlen(cur));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket - 1, prev));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket + 1, next));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket + 2, far));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("other", bucket, other));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strcmp(cur, far) != 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strcmp(cur, other) != 0);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_validate("master", period, cur, now));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_validate("master", period, prev, now));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_validate("master", period, next, now));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_validate("master", period, far, now));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_validate("master", period, other, now));
    {
        char back[AUTHORIZE_HEX_LEN + 1];
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket - 2, back));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_validate("master", period, back, now));
    }
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_validate("master", period, NULL, now));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_validate("master", 0, cur, now));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_validate("master", period, "short", now));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_validate("master", period, bad64, now));
    {
        authorize_bind binds[3];
        const char *vals[3];
        const char *other_peer[3];
        char bound_hex[AUTHORIZE_HEX_LEN + 1];
        char again_hex[AUTHORIZE_HEX_LEN + 1];
        char peer[AUTHORIZE_PEER_SIZE];
        struct sockaddr_in in4;
        struct sockaddr_in6 in6;

        memset(&in4, 0, sizeof(in4));
        in4.sin_family = AF_INET;
        in4.sin_port = htons(443);
        inet_pton(AF_INET, "203.0.113.9", &in4.sin_addr);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_format_peer((struct sockaddr *)&in4, peer, sizeof(peer)));
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "203.0.113.9", peer);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, strstr(peer, "443") != NULL);

        memset(&in6, 0, sizeof(in6));
        in6.sin6_family = AF_INET6;
        in6.sin6_addr.s6_addr[10] = 0xff;
        in6.sin6_addr.s6_addr[11] = 0xff;
        memcpy(in6.sin6_addr.s6_addr + 12, &in4.sin_addr, 4);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_format_peer((struct sockaddr *)&in6, peer, sizeof(peer)));
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "203.0.113.9", peer);

        memset(&in6, 0, sizeof(in6));
        in6.sin6_family = AF_INET6;
        in6.sin6_addr.s6_addr[15] = 1;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_format_peer((struct sockaddr *)&in6, peer, sizeof(peer)));
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "::1", peer);

        memset(binds, 0, sizeof(binds));
        binds[0].kind = AUTHORIZE_BIND_REMOTE_ADDR;
        binds[1].kind = AUTHORIZE_BIND_HEADER;
        binds[1].name = "X-Client";
        binds[2].kind = AUTHORIZE_BIND_COOKIE;
        binds[2].name = "sid";
        vals[0] = "203.0.113.9";
        vals[1] = "scraper-a";
        vals[2] = "sess-1";
        other_peer[0] = "203.0.113.10";
        other_peer[1] = vals[1];
        other_peer[2] = vals[2];
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute_bound("master", bucket, binds, 3, vals, bound_hex));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute_bound("master", bucket, binds, 3, vals, again_hex));
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, bound_hex, again_hex);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strcmp(bound_hex, cur) != 0);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute_bound("master", bucket, binds, 3, other_peer, again_hex));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strcmp(bound_hex, again_hex) != 0);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_compute_bound("master", bucket, binds, 3, NULL, again_hex));
        vals[1] = NULL;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_compute_bound("master", bucket, binds, 3, vals, again_hex));
        vals[1] = "scraper-a";
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_validate_bound("master", period, bound_hex, now, binds, 3, vals));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_validate_bound("master", period, bound_hex, now, binds, 3, other_peer));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_validate_bound("master", period, cur, now, binds, 3, vals));
    }

    snprintf(req, sizeof(req),
        "GET / HTTP/1.1\r\nCookie: foo=1; alligator_auth=%s; bar=2\r\nX-Alligator-Auth: %s\r\nAuthorization: basic cm9vdDpxd2VydHk=\r\n\r\n",
        cur, next);
    {
        http_reply_data *http = http_proto_get_request_data(req, strlen(req), "Authorization");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, http);
        http_reply_authorize_extract(http, "X-Alligator-Auth", "alligator_auth");
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, cur, http->authorize_cookie);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, next, http->authorize_header);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, http->auth_basic);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "cm9vdDpxd2VydHk=", http->auth_basic);
        http_reply_data_free(http);
    }

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    carg->auth_header = "Authorization";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, "GET / HTTP/1.1\r\n\r\n"));

    carg->authorize_secret = strdup("master");
    carg->authorize_period = 0;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, "GET / HTTP/1.1\r\n\r\n"));
    carg->authorize_period = 100000000;

    now = time(NULL);
    bucket = (uint64_t)now / carg->authorize_period;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket, cur));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket + 1, next));
    if (bucket)
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket - 1, prev));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute("master", bucket + 2, far));

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, "GET / HTTP/1.1\r\n\r\n"));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", bad64);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(carg, req));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req));
    if (bucket)
    {
        snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", prev);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req));
    }
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", next);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", far);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(carg, req));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, "GET /authorize?x=1 HTTP/1.1\r\n\r\n"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_uri_is_authorize("/authorize"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_uri_is_authorize("/authorize?x=1"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, http_uri_is_authorize("/authorized"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, http_uri_is_authorize("/authorize/"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, "GET /authorized HTTP/1.1\r\n\r\n"));
    snprintf(req, sizeof(req), "POST /authorize HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, "POST /authorize HTTP/1.1\r\n\r\n"));

    carg->authorize_validate = AUTHORIZE_VALIDATE_HEADER;
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nX-Alligator-Auth: %s\r\nCookie: alligator_auth=%s\r\n\r\n", far, cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(carg, req));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nX-Alligator-Auth: %s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req));
    carg->authorize_validate = AUTHORIZE_VALIDATE_COOKIE;

    carg->auth_basic = alligator_ht_init(NULL);
    b64 = base64_encode("root:qwerty", strlen("root:qwerty"), &b64sz);
    http_auth_push(carg->auth_basic, b64);
    free(b64);

    carg->satisfy = SATISFY_ANY;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, "GET / HTTP/1.1\r\n\r\n"));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, TEST_AUTH_BASIC_QUERY_1_1));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(carg, TEST_AUTH_BASIC_QUERY_1_4));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", far);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(carg, req));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nAuthorization: basic ZXJyOmVycg==\r\nCookie: alligator_auth=%s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, "GET /authorize HTTP/1.1\r\nAuthorization: basic cm9vdDpxd2VydHk=\r\n\r\n"));

    carg->satisfy = SATISFY_ALL;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, TEST_AUTH_BASIC_QUERY_1_1));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, req));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nAuthorization: basic cm9vdDpxd2VydHk=\r\nCookie: alligator_auth=%s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nAuthorization: basic cm9vdDpxd2VydHk=\r\nCookie: alligator_auth=%s\r\n\r\n", far);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(carg, req));
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nAuthorization: basic ZXJyOmVycg==\r\nCookie: alligator_auth=%s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(carg, req));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, "GET /authorize HTTP/1.1\r\nAuthorization: basic cm9vdDpxd2VydHk=\r\n\r\n"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n"));
    snprintf(req, sizeof(req), "GET /authorize HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, req));

    carg->authorize_validate = AUTHORIZE_VALIDATE_HEADER;
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nAuthorization: basic cm9vdDpxd2VydHk=\r\nX-Alligator-Auth: %s\r\n\r\n", cur);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req));

    alligator_ht_foreach_arg(carg->auth_basic, http_auth_foreach_free, NULL);
    alligator_ht_done(carg->auth_basic);
    free(carg->auth_basic);
    carg->auth_basic = NULL;
    free(carg->authorize_secret);
    carg->authorize_secret = NULL;

    carg->authorize_secret = strdup("master");
    carg->authorize_period = 10;
    carg->authorize_set = AUTHORIZE_SET_JSON;
    carg->authorize_validate = AUTHORIZE_VALIDATE_COOKIE;
    carg->satisfy = SATISFY_ANY;
    carg->env = alligator_ht_init(NULL);
    env_struct_push_alloc(carg->env, "X-Test-Hdr", "yes");
    response = string_init(4096);
    t0 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize HTTP/1.1\r\n\r\n", strlen("GET /authorize HTTP/1.1\r\n\r\n"), response, carg));
    t1 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "200 OK") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "Content-Type: application/json\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "\"period\":10") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "X-Test-Hdr: yes\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_body_has_code(response->s, "master", 10, t0, t1));

    string_null(response);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET / HTTP/1.1\r\n\r\n", strlen("GET / HTTP/1.1\r\n\r\n"), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "401 Unauthorized") != NULL);

    string_null(response);
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nCookie: alligator_auth=%s\r\n\r\n", bad64);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser(req, strlen(req), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "403 Access Forbidden") != NULL);

    carg->authorize_set = AUTHORIZE_SET_COOKIE;
    string_null(response);
    t0 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize?fresh=1 HTTP/1.1\r\n\r\n", strlen("GET /authorize?fresh=1 HTTP/1.1\r\n\r\n"), response, carg));
    t1 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "Set-Cookie: alligator_auth=") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "; Path=/; Max-Age=10; HttpOnly\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "X-Test-Hdr: yes\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_body_has_code(response->s, "master", 10, t0, t1));

    carg->authorize_set = AUTHORIZE_SET_HEADER;
    carg->authorize_header_name = strdup("X-Alligator-Auth");
    string_null(response);
    t0 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize HTTP/1.1\r\n\r\n", strlen("GET /authorize HTTP/1.1\r\n\r\n"), response, carg));
    t1 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "X-Alligator-Auth: ") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_body_has_code(response->s, "master", 10, t0, t1));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, strstr(response->s, "401") != NULL);

    {
        json_error_t jerr;
        json_t *jbind;
        context_arg *jc;
        const char *vals[3];
        char bound_hex[AUTHORIZE_HEX_LEN + 1];
        char req_bind[768];
        uint64_t live_bucket;

        jc = calloc(1, sizeof(*jc));
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, jc);
        jc->authorize_secret = strdup("master");
        jc->authorize_period = 10;
        jc->authorize_header_name = "X-Alligator-Auth";
        jc->authorize_cookie_name = "alligator_auth";
        jbind = json_loads("[{\"type\":\"remote_addr\"},{\"type\":\"header\",\"name\":\"X-Client\"},{\"type\":\"cookie\",\"name\":\"sid\"}]", 0, &jerr);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, jbind);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, authorize_binds_apply_json(jc, jbind));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, jc->authorize_config_error);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, AUTHORIZE_BIND_REMOTE_ADDR, jc->authorize_binds[0].kind);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, AUTHORIZE_BIND_HEADER, jc->authorize_binds[1].kind);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "X-Client", jc->authorize_binds[1].name);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, AUTHORIZE_BIND_COOKIE, jc->authorize_binds[2].kind);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "sid", jc->authorize_binds[2].name);
        json_decref(jbind);
        authorize_binds_free(jc->authorize_binds, jc->authorize_bind_count);
        jc->authorize_binds = NULL;
        jc->authorize_bind_count = 0;
        jbind = json_loads("[{\"type\":\"remote_addr\"},{\"type\":\"cookie\",\"name\":\"alligator_auth\"},{\"type\":\"nope\"}]", 0, &jerr);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, jbind);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_binds_apply_json(jc, jbind));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, jc->authorize_config_error);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, jc->authorize_bind_count);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(jc, "GET /authorize HTTP/1.1\r\n\r\n"));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(jc, "GET / HTTP/1.1\r\nCookie: alligator_auth=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n\r\n"));
        json_decref(jbind);
        free(jc->authorize_secret);
        free(jc);

        carg->authorize_cookie_name = strdup("alligator_auth");
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_bind_add(carg, AUTHORIZE_BIND_HEADER, "x-alligator-auth"));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_bind_add(carg, AUTHORIZE_BIND_COOKIE, "alligator_auth"));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, carg->authorize_bind_count);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_bind_add(carg, AUTHORIZE_BIND_REMOTE_ADDR, NULL));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_bind_add(carg, AUTHORIZE_BIND_HEADER, "X-Client"));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_bind_add(carg, AUTHORIZE_BIND_COOKIE, "sid"));

        string_null(response);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize HTTP/1.1\r\nX-Client: scraper-a\r\nCookie: sid=sess-1\r\n\r\n", strlen("GET /authorize HTTP/1.1\r\nX-Client: scraper-a\r\nCookie: sid=sess-1\r\n\r\n"), response, carg));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "500 Internal Server Error") != NULL);

        strlcpy(carg->authorize_peer, "203.0.113.9", sizeof(carg->authorize_peer));
        string_null(response);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize HTTP/1.1\r\nCookie: sid=sess-1\r\n\r\n", strlen("GET /authorize HTTP/1.1\r\nCookie: sid=sess-1\r\n\r\n"), response, carg));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "400 Bad Request") != NULL);

        string_null(response);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize HTTP/1.1\r\nX-Client: a\r\nX-Client: b\r\nCookie: sid=sess-1\r\n\r\n", strlen("GET /authorize HTTP/1.1\r\nX-Client: a\r\nX-Client: b\r\nCookie: sid=sess-1\r\n\r\n"), response, carg));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "400 Bad Request") != NULL);

        vals[0] = "203.0.113.9";
        vals[1] = "scraper-a";
        vals[2] = "sess-1";
        string_null(response);
        t0 = (uint64_t)time(NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize HTTP/1.1\r\nX-Client: scraper-a\r\nCookie: sid=sess-1\r\n\r\n", strlen("GET /authorize HTTP/1.1\r\nX-Client: scraper-a\r\nCookie: sid=sess-1\r\n\r\n"), response, carg));
        t1 = (uint64_t)time(NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "200 OK") != NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_body_has_bound(response->s, "master", 10, carg->authorize_binds, carg->authorize_bind_count, vals, t0, t1));

        carg->authorize_period = 100000000;
        live_bucket = (uint64_t)time(NULL) / carg->authorize_period;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute_bound("master", live_bucket, carg->authorize_binds, carg->authorize_bind_count, vals, bound_hex));
        snprintf(req_bind, sizeof(req_bind), "GET / HTTP/1.1\r\nX-Client: scraper-a\r\nCookie: sid=sess-1; alligator_auth=%s\r\n\r\n", bound_hex);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req_bind));
        strlcpy(carg->authorize_peer, "198.51.100.4", sizeof(carg->authorize_peer));
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_check_req(carg, req_bind));
        strlcpy(carg->authorize_peer, "203.0.113.9", sizeof(carg->authorize_peer));
        snprintf(req_bind, sizeof(req_bind), "GET / HTTP/1.1\r\nX-Client: other\r\nCookie: sid=sess-1; alligator_auth=%s\r\n\r\n", bound_hex);
        authorize_check_deny(carg, req_bind, 0, "authorize code rejected");
        snprintf(req_bind, sizeof(req_bind), "GET / HTTP/1.1\r\nX-Client: scraper-a\r\nCookie: alligator_auth=%s\r\n\r\n", bound_hex);
        authorize_check_deny(carg, req_bind, 0, "authorize bind cookie:sid rejected");
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, authorize_check_req(carg, "GET / HTTP/1.1\r\nX-Client: scraper-a\r\nCookie: sid=sess-1\r\n\r\n"));
        snprintf(req_bind, sizeof(req_bind), "GET /authorize HTTP/1.1\r\nX-Client: scraper-a\r\nCookie: sid=sess-1\r\n\r\n");
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(carg, req_bind));

        {
            context_arg *hc = calloc(1, sizeof(*hc));
            authorize_bind *two;
            const char *hvals[2];
            char two_hex[AUTHORIZE_HEX_LEN + 1];
            uint64_t two_bucket;

            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hc);
            hc->authorize_secret = strdup("master");
            hc->authorize_period = 100000000;
            hc->authorize_validate = AUTHORIZE_VALIDATE_COOKIE;
            hc->authorize_header_name = strdup("X-Alligator-Auth");
            hc->authorize_cookie_name = strdup("alligator_auth");
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_bind_add(hc, AUTHORIZE_BIND_HEADER, "X-Client"));
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_bind_add(hc, AUTHORIZE_BIND_HEADER, "X-User"));
            two = hc->authorize_binds;
            hvals[0] = "scraper-a";
            hvals[1] = "alice";
            two_bucket = (uint64_t)time(NULL) / hc->authorize_period;
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_compute_bound("master", two_bucket, two, 2, hvals, two_hex));
            snprintf(req_bind, sizeof(req_bind), "GET / HTTP/1.1\r\nX-Client: scraper-a\r\nX-User: alice\r\nCookie: alligator_auth=%s\r\n\r\n", two_hex);
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_check_req(hc, req_bind));
            snprintf(req_bind, sizeof(req_bind), "GET / HTTP/1.1\r\nX-Client: scraper-a\r\nCookie: alligator_auth=%s\r\n\r\n", two_hex);
            authorize_check_deny(hc, req_bind, 0, "authorize bind header:X-User rejected");
            snprintf(req_bind, sizeof(req_bind), "GET / HTTP/1.1\r\nX-Client: scraper-a\r\nX-User: bob\r\nCookie: alligator_auth=%s\r\n\r\n", two_hex);
            authorize_check_deny(hc, req_bind, 0, "authorize code rejected");
            free(hc->authorize_secret);
            free(hc->authorize_header_name);
            free(hc->authorize_cookie_name);
            authorize_binds_free(hc->authorize_binds, hc->authorize_bind_count);
            free(hc);
        }
    }

    string_free(response);
    alligator_ht_foreach_arg(carg->env, env_struct_free, NULL);
    alligator_ht_done(carg->env);
    free(carg->env);
    free(carg->authorize_secret);
    free(carg->authorize_header_name);
    free(carg->authorize_cookie_name);
    authorize_binds_free(carg->authorize_binds, carg->authorize_bind_count);
    free(carg);
}

static int limit_apply(context_arg *carg, const char *json)
{
    json_error_t err;
    json_t *j;
    int n;

    j = json_loads(json, 0, &err);
    if (!j)
        return -2;
    n = http_limit_apply_json(carg, j);
    json_decref(j);
    return n;
}

static int limit_check_req(context_arg *carg, const char *req, uint64_t now, uint64_t *retry)
{
    http_reply_data *http;
    int rc;

    http = http_proto_get_request_data((char *)req, strlen(req), "Authorization");
    if (!http)
        return -1;
    rc = http_limit_check(carg, http, now, retry);
    http_reply_data_free(http);
    return rc;
}

void test_http_limit(void)
{
    context_arg *carg;
    context_arg *dup;
    uint64_t t0 = 1000000000000ULL;
    uint64_t interval = 1000000000ULL / 10;
    uint64_t retry = 0;
    int i;
    string *response;
    uint64_t wall0;
    uint64_t wall1;

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_apply(carg, "[{\"path\":\"/authorize\",\"rps\":10}]"));
    for (i = 0; i < 10; i++)
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, retry >= 1);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize?x=1 HTTP/1.1\r\n\r\n", t0, &retry) == 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorized HTTP/1.1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0 + interval, &retry));
    carg_free(carg);

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_apply(carg, "[{\"path\":\"/authorize\",\"rps\":1,\"key\":[\"remote_addr\"]}]"));
    strlcpy(carg->authorize_peer, "203.0.113.9", sizeof(carg->authorize_peer));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    strlcpy(carg->authorize_peer, "203.0.113.10", sizeof(carg->authorize_peer));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    carg->authorize_peer[0] = '\0';
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    carg_free(carg);

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_apply(carg, "[{\"path\":\"/authorize\",\"rps\":1,\"key\":[\"header:X-Client\"]}]"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: a\r\nX-Client: b\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: a\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: a\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: b\r\n\r\n", t0, &retry));
    carg_free(carg);

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_apply(carg, "[{\"path\":\"/authorize\",\"rps\":1,\"key\":[\"header:X-Client\",\"cookie:sid\"]}]"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: a\r\nCookie: sid=1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: a\r\nCookie: sid=1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: a\r\nCookie: sid=2\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: b\r\nCookie: sid=1\r\n\r\n", t0, &retry));
    carg_free(carg);

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, limit_apply(carg,
        "[{\"path\":\"/authorize\",\"rps\":2},{\"path\":\"/authorize\",\"rps\":1,\"key\":[\"header:X-Client\"]}]"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: a\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: a\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: b\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\nX-Client: c\r\n\r\n", t0, &retry));
    carg_free(carg);

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_apply(carg, "[{\"path\":\"/nope\"},{\"path\":\"/authorize\",\"rps\":0},{\"path\":\"/stats\",\"rps\":1}]"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->http_limit_config_error);
    assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, carg->http_limit);
    carg_free(carg);

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_apply(carg, "[{\"path\":\"/cap\",\"rps\":1,\"key\":[\"header:X-Client\"]}]"));
    for (i = 0; i < 4096; i++)
    {
        char req[96];

        snprintf(req, sizeof(req), "GET /cap HTTP/1.1\r\nX-Client: k%d\r\n\r\n", i);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, req, t0, &retry));
    }
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /cap HTTP/1.1\r\nX-Client: fresh\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 60, (int)retry);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /cap HTTP/1.1\r\nX-Client: fresh\r\n\r\n", t0 + 30ULL * 1000000000ULL, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /cap HTTP/1.1\r\nX-Client: fresh\r\n\r\n", t0 + 60ULL * 1000000000ULL, &retry));
    carg_free(carg);

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_apply(carg, "[{\"path\":\"/authorize\",\"rps\":1}]"));
    dup = carg_copy(carg);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dup);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->http_limit == dup->http_limit);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(dup, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    carg_free(dup);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, limit_check_req(carg, "GET /authorize HTTP/1.1\r\n\r\n", t0, &retry));
    carg_free(carg);

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    carg->auth_header = strdup("Authorization");
    carg->authorize_secret = strdup("master");
    carg->authorize_period = 10;
    carg->authorize_set = AUTHORIZE_SET_JSON;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, limit_apply(carg, "[{\"path\":\"/authorize\",\"rps\":1}]"));
    response = string_init(4096);
    wall0 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize HTTP/1.1\r\n\r\n", strlen("GET /authorize HTTP/1.1\r\n\r\n"), response, carg));
    wall1 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "200 OK") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, authorize_body_has_code(response->s, "master", 10, wall0, wall1));
    string_null(response);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize HTTP/1.1\r\n\r\n", strlen("GET /authorize HTTP/1.1\r\n\r\n"), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "429 Too Many Requests") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "Retry-After: ") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_body_has_code(response->s, "master", 10, wall0, wall1));
    string_null(response);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorized HTTP/1.1\r\n\r\n", strlen("GET /authorized HTTP/1.1\r\n\r\n"), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "401 Unauthorized") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, strstr(response->s, "429") != NULL);
    string_null(response);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("OPTIONS /authorize HTTP/1.1\r\n\r\n", strlen("OPTIONS /authorize HTTP/1.1\r\n\r\n"), response, carg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "429 Too Many Requests") != NULL);
    string_free(response);
    carg_free(carg);

    carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    carg->auth_header = strdup("Authorization");
    carg->authorize_secret = strdup("master");
    carg->authorize_period = 10;
    carg->authorize_set = AUTHORIZE_SET_JSON;
    carg->http_limit_config_error = 1;
    response = string_init(4096);
    wall0 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_parser("GET /authorize HTTP/1.1\r\n\r\n", strlen("GET /authorize HTTP/1.1\r\n\r\n"), response, carg));
    wall1 = (uint64_t)time(NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(response->s, "500 Internal Server Error") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, authorize_body_has_code(response->s, "master", 10, wall0, wall1));
    string_free(response);
    carg_free(carg);
}
