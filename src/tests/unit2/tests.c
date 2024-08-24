#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "common/patricia.h"

/*      UINT64_MAX 18446744073709551615ULL */
#define P10_UINT64 10000000000000000000ULL   /* 19 zeroes */
#define P10_INT64 10000000000000000000ULL   /* 19 zeroes */
#define E10_UINT64 19

#define STRINGIZER(x)   # x
#define TO_STRING(x)    STRINGIZER(x)

aconf *ac;
uint64_t count_all;
uint64_t count_error;
uint8_t exit_on_error;

void infomesg() {
    fprintf(stderr, "tests passed: %"PRIu64", failed: %"PRIu64", total: %"PRIu64"\n", count_all - count_error, count_error, count_all);
}

void do_exit(int code) {
    if (exit_on_error) {
        infomesg();
        exit(code);
    }
}

static int print_u128_u(uint128_t uu128)
{
    int rc;
    if (uu128 > UINT64_MAX)
    {
        uint128_t leading  = uu128 / P10_UINT64;
        uint64_t  trailing = uu128 % P10_UINT64;
        rc = print_u128_u(leading);
        rc += fprintf(stderr, "%." TO_STRING(E10_UINT64) PRIu64, trailing);
    }
    else
    {
        uint64_t uu64 = uu128;
        rc = fprintf(stderr, "%" PRIu64, uu64);
    }
    return rc;
}

static int print_128_d(int128_t dd128)
{
    int rc;
    if (dd128 > UINT64_MAX)
    {
        int128_t leading  = dd128 / P10_INT64;
        int64_t  trailing = dd128 % P10_INT64;
        rc = print_128_d(leading);
        rc += fprintf(stderr, "%." TO_STRING(E10_UINT64) PRIu64, trailing);
    }
    else
    {
        int64_t dd64 = dd128;
        rc = fprintf(stderr, "%" PRId64, dd64);
    }
    return rc;
}


int assert_equal_uint(const char *file, const char *func, int line, uint128_t expected, uint128_t value) {
    ++count_all;
    if (expected != value) {
        ++count_error;
        fprintf(stderr, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        print_u128_u(value);
        fprintf(stderr, "', expected: '");
        print_u128_u(expected);
        fprintf(stderr, "'\n");
        fflush(stderr);
        do_exit(1);
    }

    return 1;
}

int assert_equal_int(const char *file, const char *func, int line, int128_t expected, int128_t value) {
    ++count_all;
    if (expected != value) {
        ++count_error;
        fprintf(stderr, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        print_128_d(value);
        fprintf(stderr, "', expected: '");
        print_128_d(expected);
        fprintf(stderr, "'\n");
        fflush(stderr);
        do_exit(1);
    }

    return 1;
}

int assert_equal_string(const char *file, const char *func, int line, char *expected, char *value) {
    ++count_all;
    if (strcmp(expected, value)) {
        ++count_error;
        fprintf(stderr, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        fprintf(stderr, "%s", value);
        fprintf(stderr, "', expected: '");
        fprintf(stderr, "%s", expected);
        fprintf(stderr, "'\n");
        fflush(stderr);
        do_exit(1);
    }

    return 1;
}

int assert_ptr_notnull(const char *file, const char *func, int line, void *value) {
    ++count_all;
    if (!value) {
        ++count_error;
        fprintf(stderr, "%s:%d unit test function '%s' pointer is NULL\n", file, line, func);
        fflush(stderr);
        do_exit(1);
    }

    return 1;
}

#include "netlib.h"
#include "http.h"
#include "api_v1.h"

int main(int argc, char **argv) {
    count_all = 0;
    count_error = 0;
    exit_on_error = 1;
    if ((argc > 1) && !strcmp(argv[1], "pass"))
        exit_on_error = 0;

    ac = calloc(1, sizeof(*ac));
    test_ip_check_access_1();
    test_ip_to_int();
    test_integer_to_ip();
    test_ip_get_version();
    test_http_access_1();
    test_http_access_2();
    api_test_query_1();
    infomesg();
}
