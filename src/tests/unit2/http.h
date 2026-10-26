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
#include "main.h"
#include "common/selector.h"
#include "events/context_arg.h"
#include "common/base64.h"
#include "common/auth.h"
#include "common/authorize.h"
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

    string_free(response);
    alligator_ht_foreach_arg(carg->env, env_struct_free, NULL);
    alligator_ht_done(carg->env);
    free(carg->env);
    free(carg->authorize_secret);
    free(carg->authorize_header_name);
    free(carg);
}
