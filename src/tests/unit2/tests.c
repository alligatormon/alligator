#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/patricia.h"

/*      UINT64_MAX 18446744073709551615ULL */
#define P10_UINT64 10000000000000000000ULL   /* 19 zeroes */
#define P10_INT64 10000000000000000000ULL   /* 19 zeroes */
#define E10_UINT64 19

#define STRINGIZER(x)   # x
#define TO_STRING(x)    STRINGIZER(x)


static int print_u128_u(uint128_t u128)
{
    int rc;
    if (u128 > UINT64_MAX)
    {
        uint128_t leading  = u128 / P10_UINT64;
        uint64_t  trailing = u128 % P10_UINT64;
        rc = print_u128_u(leading);
        rc += fprintf(stderr, "%." TO_STRING(E10_UINT64) PRIu64, trailing);
    }
    else
    {
        uint64_t u64 = u128;
        rc = fprintf(stderr, "%" PRIu64, u64);
    }
    return rc;
}

static int print_128_d(int128_t d128)
{
    int rc;
    if (d128 > UINT64_MAX)
    {
        int128_t leading  = d128 / P10_INT64;
        int64_t  trailing = d128 % P10_INT64;
        rc = print_128_d(leading);
        rc += fprintf(stderr, "%." TO_STRING(E10_UINT64) PRIu64, trailing);
    }
    else
    {
        int64_t d64 = d128;
        rc = fprintf(stderr, "%" PRId64, d64);
    }
    return rc;
}


int assert_equal_uint(const char *file, const char *func, int line, uint128_t expected, uint128_t value) {
    if (expected != value) {
        fprintf(stderr, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        print_u128_u(value);
        fprintf(stderr, "', expected: '");
        print_u128_u(expected);
        fprintf(stderr, "'\n");
        fflush(stderr);
        exit(1);
    }

    return 1;
}

int assert_equal_int(const char *file, const char *func, int line, int128_t expected, int128_t value) {
    if (expected != value) {
        fprintf(stderr, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        print_128_d(value);
        fprintf(stderr, "', expected: '");
        print_128_d(expected);
        fprintf(stderr, "'\n");
        fflush(stderr);
        exit(1);
    }

    return 1;
}

int assert_equal_string(const char *file, const char *func, int line, char *expected, char *value) {
    if (strcmp(expected, value)) {
        fprintf(stderr, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        fprintf(stderr, "%s", value);
        fprintf(stderr, "', expected: '");
        fprintf(stderr, "%s", expected);
        fprintf(stderr, "'\n");
        fflush(stderr);
        exit(1);
    }

    return 1;
}

void glog(int priority, const char *format, ...)
{
}

#include "netlib.h"

int main() {
    test_ip_check_access_1();
    test_ip_to_int();
    test_integer_to_ip();
    test_ip_get_version();
}
