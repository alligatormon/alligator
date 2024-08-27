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

#include "main.h"
#include "common/selector.h"
#include "events/context_arg.h"
#include "common/base64.h"
#include "common/auth.h"

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
	puts("=========\ntest_http_access_1\n============\n");

	context_arg *carg = calloc(1, sizeof(*carg));
	uint64_t b64sz;

	carg->auth_basic = alligator_ht_init(NULL);

    http_auth_push(carg->auth_basic, base64_encode(TEST_AUTH_BASIC_HEADER_1_1, strlen(TEST_AUTH_BASIC_HEADER_1_1), &b64sz));
    http_auth_push(carg->auth_basic, base64_encode(TEST_AUTH_BASIC_HEADER_1_2, strlen(TEST_AUTH_BASIC_HEADER_1_2), &b64sz));
    http_auth_push(carg->auth_basic, base64_encode(TEST_AUTH_BASIC_HEADER_1_3, strlen(TEST_AUTH_BASIC_HEADER_1_3), &b64sz));

	carg->auth_header = TEST_AUTH_HEADER_1;

	check_basic(carg);
}

void test_http_access_2()
{
	puts("=========\ntest_http_access_2\n============\n");
	context_arg *carg = calloc(1, sizeof(*carg));
	uint64_t b64sz;

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
