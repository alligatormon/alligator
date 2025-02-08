#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <events/context_arg.h>
#include "metric/namespace.h"
#include "main.h"
#include "common/patricia.h"
#include "parsers/multiparser.h"
#include "metric/query.h"

/*      UINT64_MAX 18446744073709551615ULL */
#define P10_UINT64 10000000000000000000ULL   /* 19 zeroes */
#define P10_INT64 10000000000000000000ULL   /* 19 zeroes */
#define E10_UINT64 19

#define STRINGIZER(x)   # x
#define TO_STRING(x)    STRINGIZER(x)
#define CMP_EQUAL 0
#define CMP_GREATER 1
#define CMP_LESSER 2

aconf *ac;
uint64_t count_all;
uint64_t count_error;
uint8_t exit_on_error;

void infomesg() {
    fprintf(stderr, "tests passed: %"PRIu64", failed: %"PRIu64", total: %"PRIu64"\n", count_all - count_error, count_error, count_all);
}

int do_exit(int code) {
    if (exit_on_error) {
        infomesg();
        exit(code);
    }

    return 0;
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
        fflush(stdout);
        fflush(stderr);
        return do_exit(1);
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
        fflush(stdout);
        fflush(stderr);
        return do_exit(1);
    }

    return 1;
}

int assert_ptr_notnull(const char *file, const char *func, int line, const void *value) {
    ++count_all;
    if (!value) {
        ++count_error;
        fprintf(stderr, "%s:%d unit test function '%s' pointer is NULL\n", file, line, func);
        fflush(stdout);
        fflush(stderr);
        return do_exit(1);
    }

    return 1;
}

int assert_equal_string(const char *file, const char *func, int line, char *expected, const char *value) {
    ++count_all;
    if (!assert_ptr_notnull(file, func, line, value))
        return 0;
    if (strcmp(expected, value)) {
        ++count_error;
        fprintf(stderr, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        fprintf(stderr, "%s", value);
        fprintf(stderr, "', expected: '");
        fprintf(stderr, "%s", expected);
        fprintf(stderr, "'\n");
        fflush(stdout);
        fflush(stderr);
        return do_exit(1);
    }

    return 1;
}

void metric_test_run(int cmp_type, char *query, char *metric_name, double expected_val) {
    metric_query_context *mqc = promql_parser(NULL, query, strlen(query));
    string *body = metric_query_deserialize(1024, mqc, METRIC_SERIALIZER_JSON, 0, NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);

    json_error_t error;
    json_t *root = json_loads(body->s, 0, &error);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    if (!root)
    {
        fprintf(stderr, "json error on line %d: %s\n'%s'\n", error.line, error.text, body->s);
        return;
    }
    //printf("body s is '%s'\n", body->s);
    //fflush(stdout);

    uint64_t obj_size = json_array_size(root);
    for (uint64_t i = 0; i < obj_size; i++)
    {
        json_t *metric = json_array_get(root, i);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, metric);

        json_t *labels = json_object_get(metric, "labels");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, labels);
        //uint64_t labels_size = json_array_size(labels);
        //for (uint64_t j = 0; j < labels_size; j++)
        const char *sname;
        json_t *value;
	    json_object_foreach(labels, sname, value)
        {
            //json_t *label = json_array_get(labels, j);
            //assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, label);

            //json_t *name = json_object_get(label, "name");
            //assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, name);
            //const char *sname = json_string_value(name);
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sname);

            //json_t *value = json_object_get(label, "value");
            //assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, value);
            const char *svalue = json_string_value(value);
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, svalue);

            if (!strcmp(sname, "__name__")) {
                if (strcmp(metric_name, svalue))
                    continue;

                assert_equal_string(__FILE__, __FUNCTION__, __LINE__, metric_name, svalue);
                json_t *samples = json_object_get(metric, "samples");
                assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, samples);

                uint64_t samples_size = json_array_size(samples);
                int samples_exists = assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, samples_size > 0);
                if (!samples_exists) {
                    printf("checked metric '%s': expected %lf, error: no samples!\n", metric_name, expected_val);
                    fflush(stdout);
                }
                for (uint64_t k = 0; k < samples_size; k++)
                {
                    json_t *sample = json_array_get(samples, k);
                    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sample);

                    json_t *sample_value = json_object_get(sample, "value");
                    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sample_value);
                    double dsample_value = 0;
                    if (json_typeof(sample_value) == JSON_INTEGER)
                        dsample_value = json_integer_value(sample_value);
                    else if (json_typeof(sample_value) == JSON_REAL)
                        dsample_value = json_real_value(sample_value);

                    int rc = 0;
                    if (cmp_type == CMP_EQUAL)
                        rc = assert_equal_int(__FILE__, __FUNCTION__, __LINE__, expected_val, dsample_value);
                    else if (cmp_type == CMP_GREATER)
                        rc = assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, expected_val < dsample_value);
                    else if (cmp_type == CMP_LESSER)
                        rc = assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, expected_val > dsample_value);

                    if (!rc) {
                        printf("checked metric '%s': expected %lf, sample %lf, rc: %d\n", metric_name, expected_val, dsample_value, rc);
                        fflush(stdout);
                    }
                }
            }
        }
    }
}

void get_local_directory(char *mockpath, char *binary, char *extra_path) {
    binary = strdup(binary);
    char *pathbin = dirname(binary);
    free(binary);
    if (*pathbin == '/') {
        snprintf(mockpath, PATH_MAX, "%s/../%s", pathbin, extra_path);
    }
    else {
        char cwd[PATH_MAX + 1];
        getcwd(cwd, sizeof(cwd));
        snprintf(mockpath, PATH_MAX, "%s/%s/../%s", cwd, pathbin, extra_path);
    }
}

#include "netlib.h"
#include "http.h"
#include "api_v1.h"
#include "parsers.h"
#include "system.h"
#include "config.h"

int main(int argc, char **argv) {
    count_all = 0;
    count_error = 0;
    exit_on_error = 1;
    if ((argc > 1) && !strcmp(argv[1], "pass"))
        exit_on_error = 0;

    ac = calloc(1, sizeof(*ac));
    ac->loop = uv_default_loop();
    ac->process_script_dir = "/var/lib/alligator/spawner";

    ac->uv_cache_timer = calloc(1, sizeof(tommy_list));
    tommy_list_init(ac->uv_cache_timer);

	ac->uv_cache_fs = calloc(1, sizeof(tommy_list));
	tommy_list_init(ac->uv_cache_fs);

    log_default();

    ts_initialize();

    test_ip_check_access_1();
    test_ip_to_int();
    test_integer_to_ip();
    test_ip_get_version();
    test_http_access_1();
    test_http_access_2();
    api_test_query_1();
    api_test_action_1();
    api_test_scheduler_1();
    api_test_lang_1();
    api_test_cluster_1();
    api_test_parser_ntp();
    api_test_parser_nsd();
    api_test_parser_syslogng();
    api_test_parser_zookeeper_dont_work();
    api_test_parser_zookeeper();
    api_test_parser_memcached();
    api_test_parser_beanstalkd();
    api_test_parser_uwsgi();
    api_test_parser_lighttpd();
    api_test_parser_httpd();
    api_test_parser_nats();
    api_test_parser_flower();
    api_test_parser_rabbitmq();
    api_test_parser_elasticsearch(argv[0]);
    system_test(argv[0]);
    test_config();
    infomesg();
}
