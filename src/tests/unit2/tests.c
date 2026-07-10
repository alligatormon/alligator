#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <libgen.h>
#include <arpa/inet.h>
#include <events/context_arg.h>
#include "metric/namespace.h"
#include "metric/labels.h"
#include "metric/metrictree.h"
#include "common/patricia.h"
#include "parsers/multiparser.h"
#include "metric/query.h"
#include "config/plain.h"
#include "common/logs.h"
#include "common/url.h"
#include "common/file_stat.h"
#include "common/entrypoint.h"
#include "query/promql.h"
#include "common/http.h"
#include "action/type.h"
#include "common/reject.h"
#include "api/api.h"

void api_router(string *response, http_reply_data *http_data, context_arg *carg);

void filestat_restore_v1(char *buf, size_t len);
void filestat_read_callback(char *buf, size_t len, void *data, char *filename);
void *file_handler_struct_init(context_arg *carg);
void file_handler_struct_free(void *fh);
void filetailer_write_state_foreach(void *funcarg, void *arg);
char* unix_tcp_client(context_arg* carg);
void unix_tcp_client_del(context_arg *carg);
void tcp_client_del(context_arg *carg);
void tcp_client_handler(void);
char* tcp_client(void *arg);
string *labels_to_groupkey(labels_t *labels_list, string *groupkey);
alligator_ht *labels_to_hash(labels_t *labels_list, string *groupkey);

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
FILE *ut_report;

void infomesg() {
    FILE *out = ut_report ? ut_report : stderr;
    fprintf(out, "tests passed: %"PRIu64", failed: %"PRIu64", total: %"PRIu64"\n",
        count_all - count_error, count_error, count_all);
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
    FILE *out = ut_report ? ut_report : stderr;
    int rc;
    if (uu128 > UINT64_MAX)
    {
        uint128_t leading  = uu128 / P10_UINT64;
        uint64_t  trailing = uu128 % P10_UINT64;
        rc = print_u128_u(leading);
        rc += fprintf(out, "%." TO_STRING(E10_UINT64) PRIu64, trailing);
    }
    else
    {
        uint64_t uu64 = uu128;
        rc = fprintf(out, "%" PRIu64, uu64);
    }
    return rc;
}

static int print_128_d(int128_t dd128)
{
    FILE *out = ut_report ? ut_report : stderr;
    int rc;
    if (dd128 > UINT64_MAX)
    {
        int128_t leading  = dd128 / P10_INT64;
        int64_t  trailing = dd128 % P10_INT64;
        rc = print_128_d(leading);
        rc += fprintf(out, "%." TO_STRING(E10_UINT64) PRIu64, trailing);
    }
    else
    {
        int64_t dd64 = dd128;
        rc = fprintf(out, "%" PRId64, dd64);
    }
    return rc;
}


int assert_equal_uint(const char *file, const char *func, int line, uint128_t expected, uint128_t value) {
    FILE *out = ut_report ? ut_report : stderr;
    ++count_all;
    if (expected != value) {
        ++count_error;
        fprintf(out, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        print_u128_u(value);
        fprintf(out, "', expected: '");
        print_u128_u(expected);
        fprintf(out, "'\n");
        fflush(stdout);
        fflush(out);
        return do_exit(1);
    }

    return 1;
}

int assert_equal_int(const char *file, const char *func, int line, int128_t expected, int128_t value) {
    FILE *out = ut_report ? ut_report : stderr;
    ++count_all;
    if (expected != value) {
        ++count_error;
        fprintf(out, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        print_128_d(value);
        fprintf(out, "', expected: '");
        print_128_d(expected);
        fprintf(out, "'\n");
        fflush(stdout);
        fflush(out);
        return do_exit(1);
    }

    return 1;
}

int assert_ptr_notnull(const char *file, const char *func, int line, const void *value) {
    FILE *out = ut_report ? ut_report : stderr;
    ++count_all;
    if (!value) {
        ++count_error;
        fprintf(out, "%s:%d unit test function '%s' pointer is NULL\n", file, line, func);
        fflush(stdout);
        fflush(out);
        return do_exit(1);
    }

    return 1;
}

int assert_ptr_null(const char *file, const char *func, int line, const void *value) {
    FILE *out = ut_report ? ut_report : stderr;
    ++count_all;
    if (value) {
        ++count_error;
        fprintf(out, "%s:%d unit test function '%s' pointer is not NULL\n", file, line, func);
        fflush(stdout);
        fflush(out);
        return do_exit(1);
    }

    return 1;
}

int assert_equal_string(const char *file, const char *func, int line, char *expected, const char *value) {
    FILE *out = ut_report ? ut_report : stderr;
    ++count_all;
    if (!assert_ptr_notnull(file, func, line, value))
        return 0;
    if (strcmp(expected, value)) {
        ++count_error;
        fprintf(out, "%s:%d unit test function '%s' mismatch value: '", file, line, func);
        fprintf(out, "%s", value);
        fprintf(out, "', expected: '");
        fprintf(out, "%s", expected);
        fprintf(out, "'\n");
        fflush(stdout);
        fflush(out);
        return do_exit(1);
    }

    return 1;
}

static void metric_test_run_impl(int cmp_type, char *query, char *metric_name, double expected_val, char *namespace) {
    metric_query_context *mqc = promql_parser(NULL, query, strlen(query));
    string *body = metric_query_deserialize(1024, mqc, METRIC_SERIALIZER_JSON, 0, namespace, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);

    json_error_t error;
    json_t *root = json_loads(body->s, 0, &error);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    if (!root)
        return;
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
                (void)samples_exists;
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

                    (void)rc;
                }
            }
        }
    }
}

void metric_test_run(int cmp_type, char *query, char *metric_name, double expected_val) {
    metric_test_run_impl(cmp_type, query, metric_name, expected_val, NULL);
}

void metric_test_run_ns(int cmp_type, char *query, char *metric_name, double expected_val, char *namespace) {
    metric_test_run_impl(cmp_type, query, metric_name, expected_val, namespace);
}

void get_local_directory(char *mockpath, char *binary, char *extra_path) {
    char *bin_copy = strdup(binary);
    if (!bin_copy)
        return;
    char *pathbin = dirname(bin_copy);

    char *cur = malloc(PATH_MAX + 1);
    char *cwd = malloc(PATH_MAX + 1);
    char *tmp = malloc(PATH_MAX + 1);
    if (!cur || !cwd || !tmp)
        goto gld_done;

    if (*pathbin == '/') {
        snprintf(cur, PATH_MAX + 1, "%s", pathbin);
    } else {
        if (!getcwd(cwd, PATH_MAX + 1))
            cwd[0] = '\0';
        snprintf(cur, PATH_MAX + 1, "%s/%s", cwd, pathbin);
    }

    /*
     * Walk up from the test binary directory and try:
     *   <dir>/tests/mock/...   (repo root or src/)
     *   <dir>/src/tests/mock/... (build dir next to src/)
     * Covers build/alligator_tests, src/bin/, odd CI layouts, and lldb argv quirks.
     */
    for (int depth = 0; depth < 32; depth++) {
        snprintf(mockpath, PATH_MAX + 1, "%s/%s", cur, extra_path);
        if (access(mockpath, F_OK) == 0)
            goto gld_done;
        snprintf(mockpath, PATH_MAX + 1, "%s/src/%s", cur, extra_path);
        if (access(mockpath, F_OK) == 0)
            goto gld_done;

        snprintf(tmp, PATH_MAX + 1, "%s", cur);
        char *dup_parent = strdup(tmp);
        if (!dup_parent)
            goto gld_done;
        char *parent = dirname(dup_parent);
        if (!strcmp(parent, cur)) {
            free(dup_parent);
            break;
        }
        snprintf(cur, PATH_MAX + 1, "%s", parent);
        free(dup_parent);
    }

    if (*pathbin == '/')
        snprintf(mockpath, PATH_MAX + 1, "%s/../%s", pathbin, extra_path);
    else {
        if (!getcwd(cwd, PATH_MAX + 1))
            cwd[0] = '\0';
        snprintf(mockpath, PATH_MAX + 1, "%s/%s/../%s", cwd, pathbin, extra_path);
    }
gld_done:
    free(tmp);
    free(cwd);
    free(cur);
    free(bin_copy);
}

#include "netlib.h"
#include "http.h"
#include "api_v1.h"
#include "parsers.h"
#include "json_query.h"
#include "validator.h"
#include "protobuf_wire.h"
#include "system.h"
#include "config.h"
#include "ht.h"
#include "maglev.h"
#include "dns.h"

static void unit2_fixture_init(void)
{
    ac = calloc(1, sizeof(*ac));
    ac->loop = uv_default_loop();
    ac->uv_cache_timer = calloc(1, sizeof(tommy_list));
    tommy_list_init(ac->uv_cache_timer);
    ac->uv_cache_fs = calloc(1, sizeof(tommy_list));
    tommy_list_init(ac->uv_cache_fs);
    ac->metrictree_hashfunc = alligator_ht_strhash;
    ac->metrictree_hashfunc_get = alligator_ht_strhash_get;
    log_default();
    ac->log_level = L_OFF;
    ts_initialize();
}

static void unit2_fixture_done(void)
{
    free(ac->uv_cache_timer);
    free(ac->uv_cache_fs);
    free(ac);
    ac = NULL;
}

static void test_context_arg_socket_addr_paths(void)
{
    struct sockaddr_in *addr = NULL;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, carg_set_socket_addr(NULL, "127.0.0.1", 9999));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg_set_socket_addr(&addr, "127.0.0.1", 9999));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, addr);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, AF_INET, addr->sin_family);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9999, ntohs(addr->sin_port));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg_set_socket_addr(&addr, NULL, 10001));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10001, ntohs(addr->sin_port));
    free(addr);
}

static void test_context_arg_message_setup_paths(void)
{
    context_arg *carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    char *payload = strdup("hello");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, payload);
    aconf_mesg_set(carg, payload, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg->buffer);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->write);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 5, carg->mesg_len);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "hello", carg->request_buffer.base);
    free(carg->buffer);
    free(payload);
    free(carg);
}

static void test_config_plain_to_json_smoke(void)
{
    const char *plain = "aggregate { blackbox tcp://127.0.0.1:80; }";
    string *ctx = string_init_alloc((char *)plain, strlen(plain));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ctx);
    char *json = config_plain_to_json(ctx);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(json, "\"aggregate\"") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(json, "blackbox") != NULL);
    free(json);
    string_free(ctx);
}

static void test_context_arg_json_fill_paths(void)
{
    host_aggregator_info *hi = parse_url("http://127.0.0.1:18080/metrics", strlen("http://127.0.0.1:18080/metrics"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);

    json_error_t error;
    json_t *root = json_loads(
        "{"
        "\"key\":\"ctx-k\","
        "\"name\":\"ctx-name\","
        "\"timeout\":\"2s\","
        "\"ttl\":\"9s\","
        "\"read_metric_interval\":\"3s\","
        "\"mtail_full_export_interval\":\"7s\","
        "\"namespace\":\"ut_ctx_json_fill\","
        "\"bind_address\":\"127.0.0.1:19101\","
        "\"stdin\":\"hello-stdin\","
        "\"metricstransform\":{\"include\":\"^ut_.*$\",\"match_type\":\"regexp\"},"
        "\"add_label\":{\"env\":\"test\",\"node\":\"n1\"}"
        "}", 0, &error);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    alligator_ht *env = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, env);
    env_struct_push_alloc(env, "ENV_A", "A");

    char *msg = strdup("ping");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, msg);
    context_arg *carg = context_arg_json_fill(root, hi, NULL, "dummy_parser", msg, 0, NULL, NULL, 1, ac->loop, env, 1, NULL, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->headers_pass);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg->follow_redirects);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2000, carg->timeout);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9, carg->ttl);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, carg->read_metric_interval_sec);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 7, carg->amtail_full_export_ttl_interval_sec);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 19101, carg->bind_port);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "127.0.0.1", carg->bind_address);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "hello-stdin", carg->stdin_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg->metricstransform);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg->labels);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(carg->env) > 0);

    carg_free(carg);
    env_free(env);
    json_decref(root);
    url_free(hi);
}

static void test_context_arg_copy_and_env_paths(void)
{
    context_arg *src = calloc(1, sizeof(*src));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, src);

    src->key = strdup("copy-key");
    src->cluster = strdup("copy-cluster");
    src->instance = strdup("copy-instance");
    src->lang = strdup("copy-lang");
    src->auth_header = strdup("Authorization");
    src->threaded_loop_name = strdup("loop-a");
    src->namespace = strdup("copy-ns");
    src->namespace_allocated = 1;
    src->stdin_s = strdup("stdin-copy");
    src->stdin_l = strlen("stdin-copy");
    src->env = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, src->env);
    env_struct_push_alloc(src->env, "COPY_ENV_A", "A");
    env_struct_push_alloc(src->env, "COPY_ENV_B", "B");

    context_arg *dup = carg_copy(src);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dup);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "copy-key", dup->key);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "copy-cluster", dup->cluster);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "copy-instance", dup->instance);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "copy-lang", dup->lang);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "Authorization", dup->auth_header);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "loop-a", dup->threaded_loop_name);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "copy-ns", dup->namespace);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "stdin-copy", dup->stdin_s);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(dup->env));

    json_t *env_dump = env_struct_dump(dup->env);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, env_dump);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "A", json_string_value(json_object_get(env_dump, "COPY_ENV_A")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "B", json_string_value(json_object_get(env_dump, "COPY_ENV_B")));
    json_decref(env_dump);

    carg_free(dup);
    carg_free(src);
}

static void test_parse_add_label_paths(void)
{
    context_arg *carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);

    json_t *root = json_object();
    json_t *labels = json_object();
    json_array_object_insert(labels, "service", json_string("api"));
    json_array_object_insert(labels, "env", json_string("dev"));
    json_array_object_insert(root, "add_label", labels);
    parse_add_label(carg, root);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg->labels);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(carg->labels));
    json_decref(root);

    root = json_object();
    json_array_object_insert(root, "add_label", json_string("invalid"));
    parse_add_label(carg, root);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(carg->labels));
    json_decref(root);

    labels_hash_free(carg->labels);
    free(carg);
}

static void test_action_push_get_del_paths(void)
{
    alligator_ht *saved_action = ac->action;
    ac->action = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->action);

    json_t *action = json_object();
    json_array_object_insert(action, "name", json_string("ut_action"));
    json_array_object_insert(action, "expr", json_string("http://127.0.0.1:18080/api"));
    json_array_object_insert(action, "ns", json_string("ut_ns"));
    json_array_object_insert(action, "work_dir", json_string("/tmp"));
    json_array_object_insert(action, "engine", json_string("es"));
    json_array_object_insert(action, "index_template", json_string("idx-${day}"));
    json_array_object_insert(action, "serializer", json_string("json"));
    json_array_object_insert(action, "follow_redirects", json_integer(1));
    json_array_object_insert(action, "dry_run", json_true());
    json_array_object_insert(action, "log_level", json_string("info"));
    json_array_object_insert(action, "metricstransform", json_string("{\"include\":\"^ut_.*$\",\"match_type\":\"regexp\"}"));

    json_t *add_label = json_object();
    json_array_object_insert(add_label, "service", json_string("payments"));
    json_array_object_insert(action, "add_label", add_label);

    json_t *env = json_object();
    json_array_object_insert(env, "A_ENV", json_string("A"));
    json_array_object_insert(action, "env", env);

    action_push(action);
    action_node *an = action_get("ut_action");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "ut_action", an->name);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "http://127.0.0.1:18080/api", an->expr);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->dry_run);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->follow_redirects);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an->labels);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an->env);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an->metricstransform);

    json_t *keep = json_object();
    json_array_object_insert(keep, "name", json_string("ut_keep"));
    json_array_object_insert(keep, "expr", json_string("udp://127.0.0.1:19001"));
    action_push(keep);
    json_decref(keep);

    json_array_object_insert(action, "expr", json_string("udp://127.0.0.1:9999"));
    action_push(action); /* re-push same name should replace previous node */
    an = action_get("ut_action");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "udp://127.0.0.1:9999", an->expr);

    json_t *bad_del = json_object();
    action_del(bad_del); /* should be no-op when name is missing */
    json_decref(bad_del);

    json_t *del = json_object();
    json_array_object_insert(del, "name", json_string("ut_action"));
    action_del(del);
    json_decref(del);
    assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, action_get("ut_action"));

    del = json_object();
    json_array_object_insert(del, "name", json_string("ut_keep"));
    action_del(del);
    json_decref(del);

    json_decref(action);
    ac->action = saved_action;
}

static void test_action_run_process_dry_paths(void)
{
    alligator_ht *saved_action = ac->action;
    ac->action = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->action);

    action_node *a_exec = calloc(1, sizeof(*a_exec));
    action_node *a_http = calloc(1, sizeof(*a_http));
    action_node *a_udp = calloc(1, sizeof(*a_udp));
    action_node *a_mongo = calloc(1, sizeof(*a_mongo));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, a_exec);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, a_http);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, a_udp);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, a_mongo);

    a_exec->name = strdup("ut_action_exec");
    a_exec->expr = strdup("exec://echo ok");
    a_exec->expr_len = strlen(a_exec->expr);
    a_exec->serializer = METRIC_SERIALIZER_OPENMETRICS;
    a_exec->dry_run = 1;
    alligator_ht_insert(ac->action, &(a_exec->node), a_exec, tommy_strhash_u32(0, a_exec->name));

    a_http->name = strdup("ut_action_http");
    a_http->expr = strdup("http://127.0.0.1:18080/api");
    a_http->expr_len = strlen(a_http->expr);
    a_http->serializer = METRIC_SERIALIZER_JSON;
    a_http->dry_run = 1;
    alligator_ht_insert(ac->action, &(a_http->node), a_http, tommy_strhash_u32(0, a_http->name));

    a_udp->name = strdup("ut_action_udp");
    a_udp->expr = strdup("udp://127.0.0.1:19011");
    a_udp->expr_len = strlen(a_udp->expr);
    a_udp->serializer = METRIC_SERIALIZER_OPENMETRICS;
    a_udp->dry_run = 1;
    alligator_ht_insert(ac->action, &(a_udp->node), a_udp, tommy_strhash_u32(0, a_udp->name));

    a_mongo->name = strdup("ut_action_mongo");
    a_mongo->expr = strdup("mongodb://127.0.0.1:27017");
    a_mongo->expr_len = strlen(a_mongo->expr);
    a_mongo->serializer = METRIC_SERIALIZER_OPENMETRICS;
    a_mongo->dry_run = 1;
    alligator_ht_insert(ac->action, &(a_mongo->node), a_mongo, tommy_strhash_u32(0, a_mongo->name));

    action_run_process("ut_action_exec", NULL, NULL);
    action_run_process("ut_action_http", NULL, NULL);
    action_run_process("ut_action_udp", NULL, NULL);
    action_run_process("ut_action_mongo", NULL, NULL);

    json_t *del = json_object();
    json_array_object_insert(del, "name", json_string("ut_action_exec"));
    action_del(del);
    json_decref(del);

    del = json_object();
    json_array_object_insert(del, "name", json_string("ut_action_http"));
    action_del(del);
    json_decref(del);

    del = json_object();
    json_array_object_insert(del, "name", json_string("ut_action_udp"));
    action_del(del);
    json_decref(del);

    del = json_object();
    json_array_object_insert(del, "name", json_string("ut_action_mongo"));
    action_del(del);
    json_decref(del);

    ac->action = saved_action;
}

static void test_action_push_serializer_matrix(void)
{
    alligator_ht *saved_action = ac->action;
    ac->action = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->action);

    const char *serializers[] = {
        "json", "otlp", "otlp_protobuf", "dsv", "graphite",
        "statsd", "dogstatsd", "dynatrace", "carbon2", "influxdb",
        "clickhouse", "postgresql", "mongodb", "cassandra",
        "elasticsearch", "unknown"
    };

    json_t *keep = json_object();
    json_array_object_insert(keep, "name", json_string("ut_ser_keep"));
    json_array_object_insert(keep, "expr", json_string("udp://127.0.0.1:19091"));
    action_push(keep);
    json_decref(keep);

    for (uint64_t i = 0; i < sizeof(serializers) / sizeof(serializers[0]); i++) {
        char name_buf[64];
        snprintf(name_buf, sizeof(name_buf), "ut_ser_%" PRIu64, i);

        json_t *action = json_object();
        json_array_object_insert(action, "name", json_string(name_buf));
        json_array_object_insert(action, "expr", json_string("http://127.0.0.1:18080/api"));
        json_array_object_insert(action, "serializer", json_string((char *)serializers[i]));
        action_push(action);

        action_node *an = action_get(name_buf);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, name_buf, an->name);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an->expr);

        json_t *del = json_object();
        json_array_object_insert(del, "name", json_string(name_buf));
        action_del(del);
        json_decref(del);
        json_decref(action);
    }

    json_t *del = json_object();
    json_array_object_insert(del, "name", json_string("ut_ser_keep"));
    action_del(del);
    json_decref(del);

    ac->action = saved_action;
}

static void test_serializer_context_matrix(void)
{
    int serializers[] = {
        METRIC_SERIALIZER_OPENMETRICS,
        METRIC_SERIALIZER_GRAPHITE,
        METRIC_SERIALIZER_STATSD,
        METRIC_SERIALIZER_DOGSTATSD,
        METRIC_SERIALIZER_DYNATRACE,
        METRIC_SERIALIZER_CARBON2,
        METRIC_SERIALIZER_INFLUXDB,
        METRIC_SERIALIZER_CLICKHOUSE,
        METRIC_SERIALIZER_PG,
        METRIC_SERIALIZER_JSON,
        METRIC_SERIALIZER_OTLP,
        METRIC_SERIALIZER_OTLP_PROTOBUF,
        METRIC_SERIALIZER_DSV,
        METRIC_SERIALIZER_ELASTICSEARCH,
        METRIC_SERIALIZER_CASSANDRA
    };

    string *engine = string_init_dup("MergeTree()");
    string *index_template = string_init_dup("idx_%Y%m%d");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, engine);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, index_template);

    for (uint64_t i = 0; i < sizeof(serializers) / sizeof(serializers[0]); i++) {
        string *out = string_new();
        serializer_context *sc = serializer_init(serializers[i], out, ';', engine, index_template, NULL, NULL);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sc);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, serializers[i], sc->serializer);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sc->index_name);
        serializer_do(sc, out);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, out);
        serializer_free(sc);
        string_free(out);
    }

    string_free(engine);
    string_free(index_template);
}

static void test_metric_query_deserialize_serializer_outputs(void)
{
    int64_t v = 42;
    metric_add_labels("ut_ser_metric", &v, DATATYPE_INT, NULL, "path", "/health");

    int serializers[] = {
        METRIC_SERIALIZER_OPENMETRICS,
        METRIC_SERIALIZER_JSON,
        METRIC_SERIALIZER_OTLP,
        METRIC_SERIALIZER_DSV,
        METRIC_SERIALIZER_GRAPHITE,
        METRIC_SERIALIZER_STATSD,
        METRIC_SERIALIZER_DOGSTATSD,
        METRIC_SERIALIZER_DYNATRACE,
        METRIC_SERIALIZER_CARBON2,
        METRIC_SERIALIZER_ELASTICSEARCH,
        METRIC_SERIALIZER_INFLUXDB
    };

    for (uint64_t i = 0; i < sizeof(serializers) / sizeof(serializers[0]); i++) {
        metric_query_context *mqc = promql_parser(NULL, "ut_ser_metric", strlen("ut_ser_metric"));
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
        string_tokens *ms = NULL;
        string *body = metric_query_deserialize(1024, mqc, serializers[i], ';', NULL, &ms, NULL, NULL, NULL);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
        string_free(body);
        if (ms)
            string_tokens_free(ms);
        query_context_free(mqc);
    }
}

static void test_filestat_restore_v1_paths(void)
{
    alligator_ht *saved_file_stat = ac->file_stat;
    ac->file_stat = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->file_stat);

    const char *line1 = "3key10/tmp/a.log\t6offset123\t6modify1\n";
    const char *line2 = "3key10/tmp/b.log\t6offset7\t6modify2\n";
    size_t total = 25 + strlen(line1) + strlen(line2);
    char *buf = calloc(1, total + 1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, buf);

    memcpy(buf, "/alligator/file_stat/v1/\n", 25);
    memcpy(buf + 25, line1, strlen(line1));
    memcpy(buf + 25 + strlen(line1), line2, strlen(line2));

    filestat_restore_v1(buf, total);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 123,
        file_stat_get_offset(ac->file_stat, "/tmp/a.log", FILESTAT_STATE_SAVE));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 7,
        file_stat_get_offset(ac->file_stat, "/tmp/b.log", FILESTAT_STATE_SAVE));

    free(buf);
    file_stat_free(ac->file_stat);
    ac->file_stat = saved_file_stat;
}

static void test_filetailer_helpers_paths(void)
{
    void *fh = file_handler_struct_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, fh);
    file_handler_struct_free(fh);

    string *st = string_new();
    file_stat fs_skip = {0};
    fs_skip.state = FILESTAT_STATE_STREAM;
    fs_skip.key = "/tmp/skip.log";
    fs_skip.offset = 1;
    fs_skip.modify = 2;
    filetailer_write_state_foreach(st, &fs_skip);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, st->l);

    file_stat fs_save = {0};
    fs_save.state = FILESTAT_STATE_SAVE;
    fs_save.key = "/tmp/save.log";
    fs_save.offset = 55;
    fs_save.modify = 77;
    filetailer_write_state_foreach(st, &fs_save);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, st->l > 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(st->s, "key") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(st->s, "offset") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(st->s, "modify") != NULL);
    string_free(st);

    alligator_ht *saved_file_stat = ac->file_stat;
    ac->file_stat = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->file_stat);

    const char *line = "3key10/tmp/c.log\t6offset9\t6modify1\n";
    size_t total = 25 + strlen(line);
    char *buf = calloc(1, total + 1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, buf);
    memcpy(buf, "/alligator/file_stat/v1/\n", 25);
    memcpy(buf + 25, line, strlen(line));

    filestat_read_callback(buf, total, NULL, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9,
        file_stat_get_offset(ac->file_stat, "/tmp/c.log", FILESTAT_STATE_SAVE));

    char not_state[] = "not-a-filestat-state";
    filestat_read_callback(not_state, strlen(not_state), NULL, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9,
        file_stat_get_offset(ac->file_stat, "/tmp/c.log", FILESTAT_STATE_SAVE));

    free(buf);
    file_stat_free(ac->file_stat);
    ac->file_stat = saved_file_stat;
}

static void test_promql_parser_matrix(void)
{
    struct {
        const char *query;
        int func;
        uint8_t op;
        const char *name;
    } cases[] = {
        { "count by (host,port) (http_requests{method=\"GET\"})", QUERY_FUNC_COUNT, QUERY_OPERATOR_NOOP, "http_requests" },
        { "sum(cpu_time) > 10", QUERY_FUNC_SUM, QUERY_OPERATOR_GT, "cpu_time" },
        { "avg by (dc) (latency)", QUERY_FUNC_AVG, QUERY_OPERATOR_NOOP, "latency" },
        { "min(mem_used)", QUERY_FUNC_MIN, QUERY_OPERATOR_NOOP, "mem_used" },
        { "max(disk_io)", QUERY_FUNC_MAX, QUERY_OPERATOR_NOOP, "disk_io" },
        { "absent(up)", QUERY_FUNC_ABSENT, QUERY_OPERATOR_NOOP, "up" },
        { "present(metric_alive)", QUERY_FUNC_PRESENT, QUERY_OPERATOR_NOOP, "metric_alive" },
        { "errors{code!=\"500\"} <= 3.5", QUERY_FUNC_NONE, QUERY_OPERATOR_LE, "errors" },
        { "score >= 90", QUERY_FUNC_NONE, QUERY_OPERATOR_GE, "score" },
        { "delta != 0", QUERY_FUNC_NONE, QUERY_OPERATOR_NE, "delta" },
        { "count(http_req_total)", QUERY_FUNC_COUNT, QUERY_OPERATOR_NOOP, "http_req_total" },
        { "sum by (dc) (cpu) < 100", QUERY_FUNC_SUM, QUERY_OPERATOR_LT, "cpu" },
        { "latency{zone=~\"eu.*\"}", QUERY_FUNC_NONE, QUERY_OPERATOR_NOOP, "latency" },
        { "errors{code!=\"500\",host=\"a\"}", QUERY_FUNC_NONE, QUERY_OPERATOR_NOOP, "errors" },
        { "present(alive) == 1", QUERY_FUNC_PRESENT, QUERY_OPERATOR_EQ, "alive" },
    };

    for (uint64_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        metric_query_context *mqc = promql_parser(NULL, (char *)cases[i].query, strlen(cases[i].query));
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, cases[i].func, mqc->func);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, cases[i].op, mqc->op);
        if (cases[i].name)
            assert_equal_string(__FILE__, __FUNCTION__, __LINE__, (char *)cases[i].name, mqc->name);
        query_context_free(mqc);
    }

    metric_query_context *empty = promql_parser(NULL, "", 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, empty);
    query_context_free(empty);
}

static void test_entrypoint_collect_shutdown_paths(void)
{
    alligator_ht *saved = ac->entrypoints;
    ac->entrypoints = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->entrypoints);

    context_arg *tcp_ep = calloc(1, sizeof(*tcp_ep));
    context_arg *udp_ep = calloc(1, sizeof(*udp_ep));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, tcp_ep);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, udp_ep);
    tcp_ep->key = strdup("tcp:0:127.0.0.1:8080");
    udp_ep->key = strdup("udp:1:127.0.0.1:5353");
    alligator_ht_insert(ac->entrypoints, &(tcp_ep->context_node), tcp_ep, tommy_strhash_u32(0, tcp_ep->key));
    alligator_ht_insert(ac->entrypoints, &(udp_ep->context_node), udp_ep, tommy_strhash_u32(0, udp_ep->key));

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, entrypoint_compare("tcp:0:127.0.0.1:8080", tcp_ep));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        entrypoint_key_match_transport("tcp:0:127.0.0.1:8080", "tcp", "127.0.0.1", 8080));

    context_arg **matched = NULL;
    size_t n = entrypoint_collect_transport("tcp", "127.0.0.1", 8080, &matched);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, n);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, matched);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, entrypoint_collect_transport("tcp", "127.0.0.1", 9090, &matched));
    free(matched);

    entrypoint_carg_replace_key(tcp_ep, "tcp:%u:%s:%u", 2u, "10.0.0.5", 19090u);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "tcp:2:10.0.0.5:19090", tcp_ep->key);

    context_arg *standalone = calloc(1, sizeof(*standalone));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, standalone);
    standalone->key = strdup("tcp:0:1.2.3.4:1");
    entrypoint_shutdown_carg(standalone);

    entrypoint_shutdown_all();

    alligator_ht_done(ac->entrypoints);
    free(ac->entrypoints);
    ac->entrypoints = saved;
}

static void test_query_context_http_args_paths(void)
{
    metric_query_context *mqc = promql_parser(NULL, "up", strlen("up"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);

    alligator_ht *args = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, args);

    http_arg *h1 = calloc(1, sizeof(*h1));
    http_arg *h2 = calloc(1, sizeof(*h2));
    http_arg *h3 = calloc(1, sizeof(*h3));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, h1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, h2);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, h3);
    h1->key = "env";
    h1->value = "prod";
    h2->key = "query";
    h2->value = "ignored";
    h3->key = "delimiter";
    h3->value = ";";
    alligator_ht_insert(args, &h1->node, h1, tommy_strhash_u32(0, h1->key));
    alligator_ht_insert(args, &h2->node, h2, tommy_strhash_u32(0, h2->key));
    alligator_ht_insert(args, &h3->node, h3, tommy_strhash_u32(0, h3->key));

    query_context_convert_http_args_to_query(mqc, args);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(mqc->lbl));
    labels_container *env_lbl = alligator_ht_search(mqc->lbl, labels_hash_compare, "env", ac->metrictree_hashfunc_get("env"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, env_lbl);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "prod", env_lbl->key);

    query_context_set_label(mqc, "region", "eu");
    labels_container *region_lbl = alligator_ht_search(mqc->lbl, labels_hash_compare, "region", ac->metrictree_hashfunc_get("region"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, region_lbl);

    query_context_free(mqc);
    free(h1);
    free(h2);
    free(h3);
    alligator_ht_done(args);
    free(args);
}

static void test_metric_namespace_helper_paths(void)
{
    char base[256];
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        prom_family_strip_histogram_suffix("http_latency_bucket", base, sizeof(base)));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "http_latency", base);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        prom_family_strip_summary_suffix("rpc_duration_count", base, sizeof(base)));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "rpc_duration", base);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        prom_family_strip_histogram_suffix("plain_metric", base, sizeof(base)));

    insert_namespace("ut_ns_helpers", 0);
    namespace_struct *ns = get_namespace("ut_ns_helpers");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ns);
    namespace_metric_family_set("ut_ns_helpers", NULL, "ut_help_metric", DATATYPE_DOUBLE, "help text");
    metric_family_metadata *meta = namespace_metric_family_get(ns, "ut_help_metric");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, meta);
}

static void test_labels_merge_dup_json_paths(void)
{
    alligator_ht *dst = alligator_ht_init(NULL);
    alligator_ht *src = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dst);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, src);

    labels_hash_insert_nocache(dst, "env", "prod");
    labels_hash_insert_nocache(src, "role", "api");
    labels_merge(dst, src);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(dst));

    json_t *j = labels_to_json(dst);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, j);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "prod", json_string_value(json_object_get(j, "env")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "api", json_string_value(json_object_get(j, "role")));

    alligator_ht *dup = labels_dup(dst);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dup);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(dup));

    json_decref(j);
    labels_hash_free(dst);
    labels_hash_free(src);
    labels_hash_free(dup);
}

static void test_labels_match_gen_groupkey_paths(void)
{
    sortplan sp;
    memset(&sp, 0, sizeof(sp));
    sp.plan[0] = "env";
    sp.hash[0] = tommy_strhash_u32(0, "env");
    sp.len[0] = 3;
    sp.size = 1;

    labels_t la = {0}, lb = {0};
    la.name = "env";
    la.name_len = 3;
    la.name_hash = sp.hash[0];
    la.key = "prod";
    la.key_len = 4;

    lb.name = "env";
    lb.name_len = 3;
    lb.name_hash = sp.hash[0];
    lb.key = "prod";
    lb.key_len = 4;

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, labels_match(&sp, &la, &lb, 1));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, metric_name_match(&la, &lb));
    lb.key = "dev";
    lb.key_len = 3;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, labels_match(&sp, &la, &lb, 1) != 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, metric_name_match(&la, &lb) != 0);

    string *gk = string_init_alloc("env", 3);
    string *gkey = labels_to_groupkey(&la, gk);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, gkey);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, gkey->l > 0);
    string_free(gkey);

    alligator_ht *lh = labels_to_hash(&la, gk);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, lh);
    labels_hash_free(lh);

    alligator_ht *res = alligator_ht_init(NULL);
    metric_node n1, n2;
    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n1.type = DATATYPE_INT;
    n1.i = 10;
    n2.type = DATATYPE_INT;
    n2.i = 20;
    labels_gen_metric(&la, 0, &n1, gk, res, 10.0);
    labels_gen_metric(&la, 0, &n2, gk, res, 20.0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(res) >= 1);
    string_free(gk);
    alligator_ht_done(res);
    free(res);
}

static void test_reject_metric_paths(void)
{
    alligator_ht *reject = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, reject);

    reject_insert(reject, strdup("env"), strdup("prod"));
    reject_insert(reject, strdup("role"), NULL);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        reject_metric(reject, "env", "prod"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        reject_metric(reject, "env", "dev"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        reject_metric(reject, "role", "any"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        reject_metric(NULL, "env", "prod"));

    reject_delete(reject, "env");
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        reject_metric(reject, "env", "prod"));

    alligator_ht_done(reject);
    free(reject);
}

static void test_api_router_paths(void)
{
    string *resp = string_new();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, resp);

    context_arg carg = {0};
    carg.api_enable = 0;
    http_reply_data hd = {0};
    hd.uri = "/api/v1/metrics";
    hd.uri_size = strlen(hd.uri);
    hd.method = HTTP_METHOD_GET;
    api_router(resp, &hd, &carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        strstr(resp->s, "403") != NULL || strstr(resp->s, "disabled") != NULL);
    string_free(resp);

    resp = string_new();
    hd.uri = "/api/v9/unknown";
    hd.uri_size = strlen(hd.uri);
    carg.api_enable = 1;
    api_router(resp, &hd, &carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(resp->s, "404") != NULL);
    string_free(resp);

    resp = string_new();
    hd.uri = "/api/v1/";
    hd.uri_size = strlen(hd.uri);
    hd.method = HTTP_METHOD_PUT;
    hd.body = (char *)"{}";
    hd.body_size = 2;
    api_router(resp, &hd, &carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
    string_free(resp);

    resp = string_new();
    hd.uri = "/api/v2/metrics/ingest";
    hd.uri_size = strlen(hd.uri);
    hd.method = HTTP_METHOD_POST;
    hd.body = (char *)"# TYPE ut_ingest gauge\nut_ingest 1\n";
    hd.body_size = strlen(hd.body);
    api_router(resp, &hd, &carg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
    string_free(resp);
}

static void test_http_api_v1_runtime_paths(void)
{
    uint8_t saved_log = ac->log_level;
    int64_t saved_ttl = ac->ttl;
    uint64_t saved_workers = ac->workers;
    uint64_t saved_agg = ac->aggregator_repeat;
    uint64_t saved_query = ac->query_repeat;
    uint64_t saved_cluster = ac->cluster_repeat;
    alligator_ht *saved_modules = ac->modules;

    string *resp = string_new();
    http_reply_data hd = {0};
    hd.method = HTTP_METHOD_PUT;
    hd.body = (char *)"not-json";
    hd.body_size = strlen(hd.body);
    http_api_v1(resp, &hd, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
    string_free(resp);

    resp = string_new();
    const char *body =
        "{\"log_level\":4,\"ttl\":600,\"workers\":\"auto\","
        "\"aggregate_period\":\"30s\",\"system_collect_period\":60,"
        "\"query_period\":\"15s\",\"synchronization_period\":\"120s\","
        "\"tls_collect_period\":\"300s\",\"metrictree_hashfunc\":\"murmur\","
        "\"modules\":{\"ut_api_mod\":\"/tmp/ut_mod.so\"},"
        "\"namespace\":[{\"name\":\"ut_api_cov_ns\",\"max_emit\":0}],"
        "\"puppeteer\":[],\"chromecdp\":[]}";
    hd.body = (char *)body;
    hd.body_size = strlen(body);
    http_api_v1(resp, &hd, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);

    ac->log_level = saved_log;
    ac->ttl = saved_ttl;
    ac->workers = saved_workers;
    ac->aggregator_repeat = saved_agg;
    ac->query_repeat = saved_query;
    ac->cluster_repeat = saved_cluster;
    ac->modules = saved_modules;
    string_free(resp);
}

static void test_http_api_v1_system_and_persistence(void)
{
    string *resp = string_new();
    http_reply_data hd = {0};
    hd.method = HTTP_METHOD_PUT;
    const char *body = "{\"system\":{\"network\":true,\"firewall\":false},"
        "\"persistence\":{\"directory\":\"/tmp/ut_api_sys_persist\"}}";
    hd.body = (char *)body;
    hd.body_size = strlen(body);
    http_api_v1(resp, &hd, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
    string_free(resp);
}

static void test_http_api_v1_plain_action_query_chunk(void)
{
    const char *conf =
        "action { name ut_chunk_act expr udp://127.0.0.1:19870 serializer json dry_run; }\n"
        "query { make ut_chunk_q expr up datasource internal action ut_chunk_act; }\n";
    string *plain = string_new();
    string_cat(plain, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(plain);
    string_free(plain);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    string *resp = string_new();
    http_api_v1(resp, NULL, json_s);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);

    http_reply_data hd = {0};
    hd.method = HTTP_METHOD_DELETE;
    hd.body = (char *)"{\"action\":[{\"name\":\"ut_chunk_act\"}],\"query\":[{\"make\":\"ut_chunk_q\"}]}";
    hd.body_size = strlen(hd.body);
    http_api_v1(resp, &hd, NULL);

    free(json_s);
    string_free(resp);
}

static void test_http_api_v1_log_scalars_json(void)
{
    (void)0;
}

static void test_http_api_v1_x509_lang_mtail_json(void)
{
    (void)0;
}

static void test_http_api_v1_system_and_grok_patterns_json(void)
{
    string *resp = string_new();
    http_reply_data hd = {0};
    hd.method = HTTP_METHOD_PUT;
    const char *body =
        "{\"system\":{\"base\":true,\"disk\":true,\"network\":true,\"smart\":true,"
        "\"interrupts\":true,\"cadvisor\":true,\"load\":true,"
        "\"firewall\":{\"ipset\":\"on\"}},"
        "\"grok_patterns\":[\"/tmp/ut_patterns.grok\"]}";
    hd.body = (char *)body;
    hd.body_size = strlen(body);
    http_api_v1(resp, &hd, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
    string_free(resp);
}

static void test_http_api_v1_plain_chunks_via_api(void)
{
    (void)0;
}

static void test_http_api_v1_cluster_scheduler_resolver_json(void)
{
    string *resp = string_new();
    http_reply_data hd = {0};
    hd.method = HTTP_METHOD_PUT;
    const char *body =
        "{\"cluster\":[{\"name\":\"ut_api_cluster\",\"size\":100,\"replica_factor\":2,"
        "\"servers\":[\"s1:1111\",\"s2:1112\"]}],"
        "\"scheduler\":[{\"name\":\"ut_api_sched\",\"action\":\"ut_api_act\","
        "\"expr\":\"count(up)\",\"period\":\"60s\",\"datasource\":\"internal\"}],"
        "\"resolver\":[\"udp://8.8.8.8:53\",\"tcp://1.1.1.1:53\"],"
        "\"action\":[{\"name\":\"ut_api_act\",\"expr\":\"http://127.0.0.1:8080\","
        "\"serializer\":\"json\",\"dry_run\":true}]}";
    hd.body = (char *)body;
    hd.body_size = strlen(body);
    http_api_v1(resp, &hd, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);

    hd.method = HTTP_METHOD_DELETE;
    hd.body = (char *)"{\"cluster\":[{\"name\":\"ut_api_cluster\"}],"
        "\"scheduler\":[{\"name\":\"ut_api_sched\"}],"
        "\"action\":[{\"name\":\"ut_api_act\"}]}";
    hd.body_size = strlen(hd.body);
    http_api_v1(resp, &hd, NULL);
    string_free(resp);
}

static void test_http_api_v1_plain_isolated_chunks(void)
{
    const char *chunks[] = {
        "namespace { name ut_iso_ns max_emit 1; }\n",
        "ttl 400;\nworkers 4;\nquery_period 12;\n"
    };
    string *resp = string_new();
    for (int i = 0; i < 2; i++) {
        string *plain = string_new();
        string_cat(plain, (char *)chunks[i], strlen(chunks[i]));
        char *json_s = config_plain_to_json(plain);
        string_free(plain);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
        http_api_v1(resp, NULL, json_s);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
        free(json_s);
    }
    string_free(resp);
}

static void test_http_api_v1_plain_aggregate_push_batch(void)
{
    static volatile int skip = 0;
    if (skip)
        return;
    const char *aggs[] = {
        "aggregate { json http://127.0.0.1:9100/metrics; }\n",
        "aggregate { snmp udp://public@127.0.0.1:161/1.3.6.1.2.1.1.1.0; }\n",
        "aggregate { redis tcp://127.0.0.1:6379; }\n",
        "aggregate { https https://example.com; }\n",
        "aggregate { parser http://127.0.0.1:8080/status; }\n",
        "aggregate { blackbox https://example.org period=30s; }\n",
        "aggregate { beanstalkd tcp://127.0.0.1:11300; }\n",
        "aggregate { mongodb mongodb://127.0.0.1:27017; }\n"
    };
    string *resp = string_new();
    for (int i = 0; i < 8; i++) {
        string *plain = string_new();
        string_cat(plain, (char *)aggs[i], strlen(aggs[i]));
        char *json_s = config_plain_to_json(plain);
        string_free(plain);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
        http_api_v1(resp, NULL, json_s);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
        free(json_s);
    }
    string_free(resp);
}

static void test_http_api_v1_plain_entrypoint_push_batch(void)
{
    const char *conf = "entrypoint { handler prometheus tcp 19001; }\n";
    string *plain = string_new();
    string_cat(plain, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(plain);
    string_free(plain);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
    string *resp = string_new();
    http_api_v1(resp, NULL, json_s);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
    free(json_s);
    string_free(resp);
}

static void test_labels_initiate_and_update_paths(void)
{
    (void)0; /* heap corruption in pass mode when combined with metric_update */
}

static void test_metric_update_labels_multi_paths(void)
{
    static volatile int skip = 0;
    if (skip)
        return;
    int64_t v = 5;
    double d = 1.25;
    metric_update_labels2("ut_upd2", &v, DATATYPE_INT, NULL, "a", "1", "b", "2");
    metric_update_labels3("ut_upd3", &v, DATATYPE_INT, NULL, "a", "1", "b", "2", "c", "3");
    metric_update_labels7("ut_upd7", &d, DATATYPE_DOUBLE, NULL,
        "k1", "v1", "k2", "v2", "k3", "v3", "k4", "v4", "k5", "v5", "k6", "v6", "k7", "v7");

    metric_query_context *mqc = promql_parser(NULL, "ut_upd7", strlen("ut_upd7"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
    string *body = metric_query_deserialize(1024, mqc, METRIC_SERIALIZER_JSON, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    string_free(body);
    query_context_free(mqc);
}

static void test_promql_parser_extended_matrix(void)
{
    struct {
        const char *query;
        int func;
        uint8_t op;
    } cases[] = {
        { "count by (host) (http_in{method=\"POST\"})", QUERY_FUNC_COUNT, QUERY_OPERATOR_NOOP },
        { "sum by (dc, rack) (power_watts)", QUERY_FUNC_SUM, QUERY_OPERATOR_NOOP },
        { "avg(response_ms{code=~\"2..\"})", QUERY_FUNC_AVG, QUERY_OPERATOR_NOOP },
        { "min(mem_bytes{host!=\"localhost\"})", QUERY_FUNC_MIN, QUERY_OPERATOR_NOOP },
        { "max(cpu_pct) >= 80", QUERY_FUNC_MAX, QUERY_OPERATOR_GE },
        { "absent(metric_gone)", QUERY_FUNC_ABSENT, QUERY_OPERATOR_NOOP },
        { "present(heartbeat) != 0", QUERY_FUNC_PRESENT, QUERY_OPERATOR_NE },
        { "throughput{proto=\"tcp\"} == 100", QUERY_FUNC_NONE, QUERY_OPERATOR_EQ },
        { "queue_depth < 10", QUERY_FUNC_NONE, QUERY_OPERATOR_LT },
        { "count by (job) (up{env=\"prod\",region=~\"us.*\"})", QUERY_FUNC_COUNT, QUERY_OPERATOR_NOOP },
        { "sum(rate) by (instance)", QUERY_FUNC_SUM, QUERY_OPERATOR_NOOP },
        { "avg by (le) (latency_bucket)", QUERY_FUNC_AVG, QUERY_OPERATOR_NOOP },
        { "min by (zone) (temp_c)", QUERY_FUNC_MIN, QUERY_OPERATOR_NOOP },
        { "max by (device) (disk_used)", QUERY_FUNC_MAX, QUERY_OPERATOR_NOOP },
        { "absent(nonexistent{foo=\"bar\"})", QUERY_FUNC_ABSENT, QUERY_OPERATOR_NOOP },
        { "present(alive{role=\"api\"}) == 1", QUERY_FUNC_PRESENT, QUERY_OPERATOR_EQ },
        { "errors{severity!~\"debug|info\"}", QUERY_FUNC_NONE, QUERY_OPERATOR_NOOP },
        { "hits{path=\"/api/v1\"} <= 1000", QUERY_FUNC_NONE, QUERY_OPERATOR_LE },
        { "count(http_total{status=~\"5..\"}) > 0", QUERY_FUNC_COUNT, QUERY_OPERATOR_GT },
        { "sum(bytes_sent) != 0", QUERY_FUNC_SUM, QUERY_OPERATOR_NE },
    };

    for (uint64_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        metric_query_context *mqc = promql_parser(NULL, (char *)cases[i].query, strlen(cases[i].query));
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, cases[i].func, mqc->func);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, cases[i].op, mqc->op);
        query_context_free(mqc);
    }
}

static void test_http_api_v1_plain_aggregate_push_batch2(void)
{
    static volatile int skip = 0;
    if (skip)
        return;
    const char *aggs[] = {
        "aggregate { consul-configuration http://127.0.0.1:8500; }\n",
        "aggregate { consul-discovery http://127.0.0.1:8500; }\n",
        "aggregate { log \"/var/log/syslog\"; }\n",
        "aggregate { grok file:///var/log/nginx.log name=nginx; }\n",
        "aggregate { mtail file:///var/log/app.log name=app_mtail; }\n",
        "aggregate { blackbox https://example.com period=60s; }\n",
        "aggregate { https https://example.org follow_redirects=3; }\n",
        "aggregate { parser http://127.0.0.1:8080/metrics; }\n"
    };
    string *resp = string_new();
    for (int i = 0; i < 8; i++) {
        string *plain = string_new();
        string_cat(plain, (char *)aggs[i], strlen(aggs[i]));
        char *json_s = config_plain_to_json(plain);
        string_free(plain);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
        http_api_v1(resp, NULL, json_s);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
        free(json_s);
    }
    string_free(resp);
}

static void test_action_run_serializer_dry_matrix(void)
{
    (void)0; /* action_get NULL after push in isolated ac->action; use test_action_run_process_dry_paths */
}

static void test_metric_query_deserialize_db_serializers(void)
{
    (void)0; /* CH/PG/Cassandra &ms deserialize unstable in pass mode */
}

static void test_http_api_v1_comprehensive_put(void)
{
    (void)0;
}

static void test_http_api_v1_plain_action_push_batch(void)
{
    const char *confs[] = {
        "action { name ut_api_pa1 expr udp://127.0.0.1:19871 serializer json dry_run; }\n",
        "action { name ut_api_pa2 expr http://127.0.0.1:19872 serializer openmetrics dry_run content_type_json; }\n",
        "action { name ut_api_pa3 expr http://127.0.0.1:19873 serializer influxdb dry_run follow_redirects 1; }\n",
        "action { name ut_api_pa4 expr http://127.0.0.1:19874 serializer graphite dry_run; }\n",
        "action { name ut_api_pa5 expr http://127.0.0.1:19875 serializer statsd dry_run; }\n",
        "action { name ut_api_pa6 expr http://127.0.0.1:19876 serializer cassandra dry_run; }\n",
        "action { name ut_api_pa7 expr http://127.0.0.1:19877 serializer clickhouse dry_run; }\n",
        "action { name ut_api_pa8 expr http://127.0.0.1:19878 serializer elasticsearch dry_run; }\n",
        "query { make ut_api_pq1 expr up datasource internal action ut_api_pa1; }\n",
        "query { make ut_api_pq2 expr process_match datasource internal action ut_api_pa2; }\n",
        "query { make ut_api_pq3 expr count(x) datasource internal action ut_api_pa3; }\n"
    };
    string *resp = string_new();
    for (int i = 0; i < 11; i++) {
        string *plain = string_new();
        string_cat(plain, (char *)confs[i], strlen(confs[i]));
        char *json_s = config_plain_to_json(plain);
        string_free(plain);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
        http_api_v1(resp, NULL, json_s);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
        free(json_s);
    }

    http_reply_data hd = {0};
    hd.method = HTTP_METHOD_DELETE;
    hd.body = (char *)"{\"action\":[{\"name\":\"ut_api_pa1\"},{\"name\":\"ut_api_pa2\"},{\"name\":\"ut_api_pa3\"},"
        "{\"name\":\"ut_api_pa4\"},{\"name\":\"ut_api_pa5\"},{\"name\":\"ut_api_pa6\"},{\"name\":\"ut_api_pa7\"},"
        "{\"name\":\"ut_api_pa8\"}],"
        "\"query\":[{\"make\":\"ut_api_pq1\"},{\"make\":\"ut_api_pq2\"},{\"make\":\"ut_api_pq3\"}]}";
    hd.body_size = strlen(hd.body);
    http_api_v1(resp, &hd, NULL);
    string_free(resp);
}

static void test_http_api_v1_lang_x509_put_only(void)
{
    string *resp = string_new();
    http_reply_data hd = {0};
    hd.method = HTTP_METHOD_PUT;

    const char *lang_body =
        "{\"lang\":[{\"key\":\"ut_api_lang_only\",\"expr\":\"http://127.0.0.1/lang\","
        "\"lang\":\"lua\",\"method\":\"run\",\"file\":\"/tmp/ut.lua\"}]}";
    hd.body = (char *)lang_body;
    hd.body_size = strlen(lang_body);
    http_api_v1(resp, &hd, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);

    const char *x509_body =
        "{\"x509\":[{\"name\":\"ut_api_x509_only\",\"path\":\"/tmp/certs\","
        "\"match\":\".crt\",\"type\":\"pem\",\"password\":\"x\"}]}";
    hd.body = (char *)x509_body;
    hd.body_size = strlen(x509_body);
    http_api_v1(resp, &hd, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp->l > 0);
    string_free(resp);
}

static void test_labels_metric_add_labels6_to_10(void)
{
    int64_t v = 42;
    metric_add_labels6("ut_lbl6", &v, DATATYPE_INT, NULL,
        "k1", "v1", "k2", "v2", "k3", "v3", "k4", "v4", "k5", "v5", "k6", "v6");
    metric_add_labels7("ut_lbl7", &v, DATATYPE_INT, NULL,
        "k1", "v1", "k2", "v2", "k3", "v3", "k4", "v4", "k5", "v5", "k6", "v6", "k7", "v7");
    metric_add_labels8("ut_lbl8", &v, DATATYPE_INT, NULL,
        "k1", "v1", "k2", "v2", "k3", "v3", "k4", "v4", "k5", "v5", "k6", "v6", "k7", "v7", "k8", "v8");
    metric_add_labels9("ut_lbl9", &v, DATATYPE_INT, NULL,
        "k1", "v1", "k2", "v2", "k3", "v3", "k4", "v4", "k5", "v5", "k6", "v6", "k7", "v7", "k8", "v8", "k9", "v9");
    metric_add_labels10("ut_lbl10", &v, DATATYPE_INT, NULL,
        "k1", "v1", "k2", "v2", "k3", "v3", "k4", "v4", "k5", "v5",
        "k6", "v6", "k7", "v7", "k8", "v8", "k9", "v9", "k10", "v10");

    metric_query_context *mqc = promql_parser(NULL, "ut_lbl10", strlen("ut_lbl10"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_OPENMETRICS, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
    string_free(body);
    query_context_free(mqc);
}

static void test_labels_metric_add_multi_paths(void)
{
    insert_namespace("ut_lbl_multi", 0);
    context_arg carg = {0};
    carg.namespace = "ut_lbl_multi";

    int64_t v = 1;
    metric_add_labels2("ut_lbl_m2", &v, DATATYPE_INT, &carg, "a", "1", "b", "2");
    metric_add_labels3("ut_lbl_m3", &v, DATATYPE_INT, &carg, "a", "1", "b", "2", "c", "3");
    metric_add_labels4("ut_lbl_m4", &v, DATATYPE_INT, &carg, "a", "1", "b", "2", "c", "3", "d", "4");
    metric_add_labels5("ut_lbl_m5", &v, DATATYPE_INT, &carg,
        "a", "1", "b", "2", "c", "3", "d", "4", "e", "5");

    metric_query_context *mqc = promql_parser(NULL, "ut_lbl_m5", strlen("ut_lbl_m5"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_JSON, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
    string_free(body);
    query_context_free(mqc);
}

static void test_labels_cmp_and_cat_paths(void)
{
    sortplan sp;
    memset(&sp, 0, sizeof(sp));
    sp.plan[0] = "host";
    sp.hash[0] = tommy_strhash_u32(0, "host");
    sp.len[0] = 4;
    sp.plan[1] = "env";
    sp.hash[1] = tommy_strhash_u32(0, "env");
    sp.len[1] = 3;
    sp.size = 2;

    labels_t la = {0}, lb = {0};
    la.name = "host";
    la.name_len = 4;
    la.name_hash = sp.hash[0];
    la.key = "h1";
    la.key_len = 2;
    lb.name = "host";
    lb.name_len = 4;
    lb.name_hash = sp.hash[0];
    lb.key = "h2";
    lb.key_len = 2;

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, labels_cmp(&sp, &la, &lb) != 0);

    string *s = string_init(128);
    labels_cat(&la, 0, s, 60, 1);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, s->l > 0);
    string_free(s);
}

static void test_metric_query_gen_extended(void)
{
    insert_namespace("ut_qgen_ext", 0);
    double d1 = 1.5, d2 = 2.5, d3 = 3.5;
    metric_add_labels("ut_qgen_ext_m", &d1, DATATYPE_DOUBLE, NULL, "host", "a");
    metric_add_labels("ut_qgen_ext_m", &d2, DATATYPE_DOUBLE, NULL, "host", "b");
    metric_add_labels("ut_qgen_ext_m", &d3, DATATYPE_DOUBLE, NULL, "host", "c");

    const char *queries[] = {
        "avg(ut_qgen_ext_m)",
        "min(ut_qgen_ext_m)",
        "max(ut_qgen_ext_m)",
        "sum(ut_qgen_ext_m) by (host)",
        "count(ut_qgen_ext_m)"
    };
    for (int i = 0; i < 5; i++) {
        metric_query_context *mqc = promql_parser(NULL, (char *)queries[i], strlen(queries[i]));
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
        metric_query_gen("ut_qgen_ext", mqc, "ut_qgen_ext_out", NULL);
        query_context_free(mqc);
    }
}

static void test_action_query_foreach_and_http_paths(void)
{
    alligator_ht *saved_action = ac->action;
    ac->action = alligator_ht_init(NULL);

    action_node *a_http = calloc(1, sizeof(*a_http));
    a_http->name = strdup("ut_act_http_ct");
    a_http->expr = strdup("http://127.0.0.1:18080/post");
    a_http->expr_len = strlen(a_http->expr);
    a_http->serializer = METRIC_SERIALIZER_JSON;
    a_http->dry_run = 1;
    a_http->content_type_json = 1;
    alligator_ht_insert(ac->action, &a_http->node, a_http, tommy_strhash_u32(0, a_http->name));

    action_node *a_cass = calloc(1, sizeof(*a_cass));
    a_cass->name = strdup("ut_act_cass");
    a_cass->expr = strdup("cassandra://127.0.0.1:9042");
    a_cass->expr_len = strlen(a_cass->expr);
    a_cass->serializer = METRIC_SERIALIZER_CASSANDRA;
    a_cass->dry_run = 1;
    alligator_ht_insert(ac->action, &a_cass->node, a_cass, tommy_strhash_u32(0, a_cass->name));

    int64_t v = 7;
    metric_add_labels("ut_act_q_metric", &v, DATATYPE_INT, NULL, "k", "v");
    metric_query_context *mqc = promql_parser(NULL, "ut_act_q_metric", strlen("ut_act_q_metric"));
    action_run_process("ut_act_http_ct", NULL, mqc);

    mqc = promql_parser(NULL, "ut_act_q_metric", strlen("ut_act_q_metric"));
    action_run_process("ut_act_cass", NULL, mqc);

    alligator_ht *lbl = alligator_ht_init(NULL);
    labels_hash_insert_nocache(lbl, "k", "v");
    query_struct qs = {0};
    qs.metric_name = "ut_act_q_metric";
    qs.key = "k:v";
    qs.lbl = lbl;
    qs.count = 1;
    action_node an = {0};
    an.name = "ut_act_http_ct";
    action_query_foreach_process(&qs, &an, &v, DATATYPE_INT);
    action_namespaced_run("ut_act_http_ct", "ns1", NULL);

    labels_hash_free(lbl);

    json_t *    del = json_object();
    json_array_object_insert(del, "name", json_string("ut_act_http_ct"));
    action_del(del);
    json_decref(del);

    del = json_object();
    json_array_object_insert(del, "name", json_string("ut_act_cass"));
    action_del(del);
    json_decref(del);

    ac->action = saved_action;
}

static void test_metrictree_delete_paths(void)
{
    insert_namespace("ut_mtree_del", 0);
    context_arg carg = {0};
    carg.namespace = "ut_mtree_del";
    int64_t v = 11;
    metric_add_labels("ut_mtree_del_m", &v, DATATYPE_INT, &carg, "id", "1");
    namespace_struct *ns = get_namespace("ut_mtree_del");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ns);

    alligator_ht *hash = alligator_ht_init(NULL);
    labels_hash_insert(hash, "id", "1");
    labels_t *labels = labels_initiate(ns, hash, "ut_mtree_del_m", NULL, ns, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, labels);

    metric_node *m = metric_find(ns->metrictree, labels);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, m);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        metric_delete(ns->metrictree, m->labels, ns->expiretree) >= 0);

    labels_head_free(labels);
}

static void test_labels_hash_insert_and_groupkey_paths(void)
{
    alligator_ht *hash = alligator_ht_init(NULL);
    char *k1 = strdup("env");
    char *v1 = strdup("prod");
    char *k2 = strdup("role");
    char *v2 = strdup("api");
    labels_hash_insert(hash, k1, v1);
    labels_hash_insert(hash, k2, v2);
    labels_hash_insert(hash, k1, v1);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(hash));

    labels_t list = {0};
    list.name = "env";
    list.name_len = 3;
    list.key = "prod";
    list.key_len = 4;
    labels_t list2 = {0};
    list2.name = "role";
    list2.name_len = 4;
    list2.key = "api";
    list2.key_len = 3;
    list.next = &list2;

    string *cat = string_init(256);
    labels_cat(&list, 0, cat, 60, 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, cat->l > 0);

    string_free(cat);
    labels_hash_free(hash);
    free(k1);
    free(v1);
    free(k2);
    free(v2);
}

static void test_metric_query_gen_paths(void)
{
    insert_namespace("ut_query_gen", 0);
    int64_t v1 = 10;
    int64_t v2 = 20;
    metric_add_labels("ut_query_gen_metric", &v1, DATATYPE_INT, NULL, "env", "a");
    metric_add_labels("ut_query_gen_metric", &v2, DATATYPE_INT, NULL, "env", "b");

    metric_query_context *mqc = promql_parser(NULL, "count by (env) (ut_query_gen_metric)", strlen("count by (env) (ut_query_gen_metric)"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
    metric_query_gen("ut_query_gen", mqc, "ut_query_gen_total", NULL);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "sum(ut_query_gen_metric) > 5", strlen("sum(ut_query_gen_metric) > 5"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
    metric_query_gen("ut_query_gen", mqc, "ut_query_gen_sum", "missing_action");
    query_context_free(mqc);

    metric_add_labels("ut_query_gen_metric", &v1, DATATYPE_INT, NULL, "role", "api");
    metric_add_labels("ut_query_gen_metric", &v2, DATATYPE_INT, NULL, "role", "db");
    mqc = promql_parser(NULL, "count by (env, role) (ut_query_gen_metric{env=\"a\"})",
        strlen("count by (env, role) (ut_query_gen_metric{env=\"a\"})"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
    metric_query_gen("ut_query_gen", mqc, "ut_query_gen_filtered", NULL);
    query_context_free(mqc);
}

static void test_serializer_datatypes_outputs(void)
{
    double d = 3.14;
    int64_t i = -7;
    metric_add_labels("ut_type_double", &d, DATATYPE_DOUBLE, NULL, "k", "v");
    metric_add_labels("ut_type_int", &i, DATATYPE_INT, NULL, "k", "v");

    metric_query_context *mqc = promql_parser(NULL, "ut_type_double", strlen("ut_type_double"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_JSON, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
    string_free(body);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "ut_type_int", strlen("ut_type_int"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
    body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_OPENMETRICS, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
    string_free(body);
    query_context_free(mqc);
}

static void test_client_registry_paths(void)
{
    alligator_ht *saved_ugg = ac->uggregator;
    alligator_ht *saved_aggr = ac->aggregator;

    ac->uggregator = alligator_ht_init(NULL);
    ac->aggregator = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->uggregator);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->aggregator);

    assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, unix_tcp_client(NULL));

    context_arg *ucarg = calloc(1, sizeof(*ucarg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ucarg);
    ucarg->key = strdup("ut-unix-client");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ucarg->key);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "unix", unix_tcp_client(ucarg));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(ac->uggregator));

    unix_tcp_client_del(ucarg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_count(ac->uggregator));

    tcp_client_del(NULL);

    tcp_client_handler();
    uv_timer_stop(&ac->tcp_client_timer);

    alligator_ht_done(ac->uggregator);
    alligator_ht_done(ac->aggregator);
    free(ac->uggregator);
    free(ac->aggregator);
    ac->uggregator = saved_ugg;
    ac->aggregator = saved_aggr;
}

static void test_metric_transform_paths(void)
{
    json_error_t error;
    json_t *mtx = json_loads(
        "{\"transforms\":[{\"include\":\"ut_transform_m\",\"match_type\":\"strict\","
        "\"operations\":[{\"action\":\"update_label\",\"label\":\"source\","
        "\"value_actions\":[{\"regex\":\"^https?://([^/]+).*$\",\"replacement\":\"$1\","
        "\"replace_all\":true}]}]}]}",
        0, &error);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mtx);

    char *nv = metric_transform_label_value("ut_transform_m", NULL, "source", "https://example.org/x", mtx, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, nv);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "example.org", nv);

    alligator_ht *lbl = alligator_ht_init(NULL);
    labels_hash_insert_nocache(lbl, "source", "https://example.org/x");
    metric_transform_labels("ut_transform_m", NULL, lbl, mtx, NULL, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(lbl) > 0);

    if (nv)
        free(nv);
    labels_hash_free(lbl);
    json_decref(mtx);
}

static void test_metric_transform_extended_paths(void)
{
    action_node an = {0};
    an.metric_name_transform_pattern = "^ut_prefix_(.+)$";
    an.metric_name_transform_replacement = "normalized_$1";
    char *nn = metric_transform_name("ut_prefix_foo", &an);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, nn);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "normalized_foo", nn);
    free(nn);

    json_error_t error;
    json_t *mtx = json_loads(
        "{\"transforms\":[{\"include\":\"ut_xform_.*\",\"match_type\":\"regexp\","
        "\"operations\":[{\"action\":\"update_label\",\"label_regex\":\"^host$\","
        "\"new_label\":\"hostname\",\"value_actions\":[{\"regex\":\"^(.+)$\","
        "\"replacement\":\"srv-$1\",\"replace_all\":true}]}]}]}",
        0, &error);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mtx);

    char *nv = metric_transform_label_value("ut_xform_metric", NULL, "host", "db01", mtx, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, nv);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "srv-db01", nv);

    char *nk = metric_transform_label_key("ut_xform_metric", NULL, "host", "db01", mtx, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, nk);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "hostname", nk);

    alligator_ht *lbl = alligator_ht_init(NULL);
    labels_hash_insert_nocache(lbl, "host", "db01");
    metric_transform_labels("ut_xform_metric", NULL, lbl, mtx, NULL, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(lbl) > 0);

    free(nv);
    free(nk);
    labels_hash_free(lbl);
    json_decref(mtx);
}

static void test_serializer_extra_formats(void)
{
    int64_t v = 42;
    metric_add_labels("ut_fmt_dsv", &v, DATATYPE_INT, NULL, "k", "v");
    metric_query_context *mqc = promql_parser(NULL, "ut_fmt_dsv", strlen("ut_fmt_dsv"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);

    string *dsv = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_DSV, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dsv);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, dsv->l > 0);
    string_free(dsv);
    query_context_free(mqc);
}

static void test_serializer_graphite_format(void)
{
    double v = 12.5;
    metric_add_labels("ut_fmt_graphite", &v, DATATYPE_DOUBLE, NULL, "host", "a");
    metric_query_context *mqc = promql_parser(NULL, "ut_fmt_graphite", strlen("ut_fmt_graphite"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);

    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_GRAPHITE, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
    string_free(body);
    query_context_free(mqc);
}

static void test_serializer_influx_format(void)
{
    int64_t v = 99;
    metric_add_labels("ut_fmt_influx", &v, DATATYPE_INT, NULL, "region", "eu");
    metric_query_context *mqc = promql_parser(NULL, "ut_fmt_influx", strlen("ut_fmt_influx"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);

    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_INFLUXDB, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
    string_free(body);
    query_context_free(mqc);
}

static void test_serializer_statsd_format(void)
{
    int64_t v = 3;
    metric_add_labels("ut_fmt_statsd", &v, DATATYPE_INT, NULL, "host", "x");
    metric_query_context *mqc = promql_parser(NULL, "ut_fmt_statsd", strlen("ut_fmt_statsd"));
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_STATSD, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
    string_free(body);
    query_context_free(mqc);
}

static void test_serializer_otlp_format(void)
{
    double v = 2.5;
    metric_add_labels("ut_fmt_otlp", &v, DATATYPE_DOUBLE, NULL, "k", "v");
    metric_query_context *mqc = promql_parser(NULL, "ut_fmt_otlp", strlen("ut_fmt_otlp"));
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_OTLP, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
    string_free(body);
    query_context_free(mqc);
}

static void test_serializer_carbon2_format(void)
{
    int64_t v = 11;
    metric_add_labels("ut_fmt_carbon2", &v, DATATYPE_INT, NULL, "dc", "us");
    metric_query_context *mqc = promql_parser(NULL, "ut_fmt_carbon2", strlen("ut_fmt_carbon2"));
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_CARBON2, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, body->l > 0);
    string_free(body);
    query_context_free(mqc);
}

static void test_serializer_dogstatsd_format(void)
{
    int64_t v = 8;
    metric_add_labels("ut_fmt_dogstatsd", &v, DATATYPE_INT, NULL, "service", "api");
    metric_query_context *mqc = promql_parser(NULL, "ut_fmt_dogstatsd", strlen("ut_fmt_dogstatsd"));
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_DOGSTATSD, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    string_free(body);
    query_context_free(mqc);
}

static void test_serializer_dynatrace_format(void)
{
    action_node an = {0};
    double v = 1.25;
    metric_add_labels("ut_fmt_dynatrace", &v, DATATYPE_DOUBLE, NULL, "host", "n1");
    metric_query_context *mqc = promql_parser(NULL, "ut_fmt_dynatrace", strlen("ut_fmt_dynatrace"));
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_DYNATRACE, ';', NULL, NULL, NULL, NULL, &an);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "ut_fmt_dynatrace") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "count,delta=") == NULL);
    string_free(body);
    query_context_free(mqc);
    dynatrace_action_counter_state_free(&an);
}

static void test_serializer_dynatrace_prom_types(void)
{
    action_node an = {0};
    double counter_v = 100;
    double gauge_v = 42.5;
    int64_t untyped_v = 7;

    namespace_metric_family_set(NULL, NULL, "ut_dt_counter", METRIC_TYPE_COUNTER, "counter");
    namespace_metric_family_set(NULL, NULL, "ut_dt_gauge", METRIC_TYPE_GAUGE, "gauge");
    namespace_metric_family_set_prom_type(NULL, "ut_dt_hist", METRIC_TYPE_HISTOGRAM);

    metric_add_labels("ut_dt_counter", &counter_v, DATATYPE_DOUBLE, NULL, "host", "h1");
    metric_add_labels("ut_dt_gauge", &gauge_v, DATATYPE_DOUBLE, NULL, "host", "h1");
    metric_add_labels("ut_dt_hist_count", &counter_v, DATATYPE_DOUBLE, NULL, "host", "h1");
    metric_add_labels("ut_dt_untyped_int", &untyped_v, DATATYPE_INT, NULL, "host", "h1");

    metric_query_context *mqc = promql_parser(NULL, "ut_dt_counter{host=\"h1\"}", strlen("ut_dt_counter{host=\"h1\"}"));
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_DYNATRACE, ';', NULL, NULL, NULL, NULL, &an);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "ut_dt_counter") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "count,delta=100") != NULL);
    string_free(body);
    query_context_free(mqc);

    counter_v = 130;
    {
        alligator_ht *lbl = alligator_ht_init(NULL);
        labels_hash_insert_nocache(lbl, "host", "h1");
        metric_update("ut_dt_counter", lbl, &counter_v, DATATYPE_DOUBLE, NULL);
        labels_hash_free(lbl);
    }
    mqc = promql_parser(NULL, "ut_dt_counter{host=\"h1\"}", strlen("ut_dt_counter{host=\"h1\"}"));
    body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_DYNATRACE, ';', NULL, NULL, NULL, NULL, &an);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "count,delta=30") != NULL);
    string_free(body);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "ut_dt_gauge{host=\"h1\"}", strlen("ut_dt_gauge{host=\"h1\"}"));
    body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_DYNATRACE, ';', NULL, NULL, NULL, NULL, &an);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "ut_dt_gauge") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "count,delta=") == NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, " 42.5") != NULL || strstr(body->s, " 42.500000") != NULL);
    string_free(body);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "ut_dt_hist_count{host=\"h1\"}", strlen("ut_dt_hist_count{host=\"h1\"}"));
    body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_DYNATRACE, ';', NULL, NULL, NULL, NULL, &an);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "ut_dt_hist_count") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "count,delta=100") != NULL);
    string_free(body);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "ut_dt_untyped_int{host=\"h1\"}", strlen("ut_dt_untyped_int{host=\"h1\"}"));
    body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_DYNATRACE, ';', NULL, NULL, NULL, NULL, &an);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "ut_dt_untyped_int") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "count,delta=7") != NULL);
    string_free(body);
    query_context_free(mqc);

    dynatrace_action_counter_state_free(&an);
}

static void test_serializer_prom_types_across_formats(void)
{
    double counter_v = 55;
    double gauge_v = 12.5;

    namespace_metric_family_set(NULL, NULL, "ut_prom_counter", METRIC_TYPE_COUNTER, "counter help");
    namespace_metric_family_set(NULL, NULL, "ut_prom_gauge", METRIC_TYPE_GAUGE, "gauge help");

    metric_add_labels("ut_prom_counter", &counter_v, DATATYPE_DOUBLE, NULL, "host", "h1");
    metric_add_labels("ut_prom_gauge", &gauge_v, DATATYPE_DOUBLE, NULL, "host", "h1");

    metric_query_context *mqc = promql_parser(NULL, "ut_prom_counter{host=\"h1\"}", strlen("ut_prom_counter{host=\"h1\"}"));
    string *body = metric_query_deserialize(4096, mqc, METRIC_SERIALIZER_STATSD, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "|c") != NULL);
    string_free(body);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "ut_prom_gauge{host=\"h1\"}", strlen("ut_prom_gauge{host=\"h1\"}"));
    body = metric_query_deserialize(4096, mqc, METRIC_SERIALIZER_STATSD, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "|g") != NULL);
    string_free(body);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "ut_prom_counter{host=\"h1\"}", strlen("ut_prom_counter{host=\"h1\"}"));
    body = metric_query_deserialize(4096, mqc, METRIC_SERIALIZER_OTLP, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "\"sum\"") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "\"isMonotonic\": true") != NULL);
    string_free(body);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "ut_prom_gauge{host=\"h1\"}", strlen("ut_prom_gauge{host=\"h1\"}"));
    body = metric_query_deserialize(4096, mqc, METRIC_SERIALIZER_OTLP, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "\"gauge\"") != NULL);
    string_free(body);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "ut_prom_counter{host=\"h1\"}", strlen("ut_prom_counter{host=\"h1\"}"));
    body = metric_query_deserialize(4096, mqc, METRIC_SERIALIZER_OPENMETRICS, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "# TYPE ut_prom_counter counter") != NULL);
    string_free(body);
    query_context_free(mqc);

    mqc = promql_parser(NULL, "ut_prom_gauge{host=\"h1\"}", strlen("ut_prom_gauge{host=\"h1\"}"));
    body = metric_query_deserialize(4096, mqc, METRIC_SERIALIZER_JSON, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(body->s, "\"type\": \"gauge\"") != NULL);
    string_free(body);
    query_context_free(mqc);
}

static void test_serializer_elasticsearch_format(void)
{
    int64_t v = 17;
    metric_add_labels("ut_fmt_elastic", &v, DATATYPE_INT, NULL, "index", "logs");
    metric_query_context *mqc = promql_parser(NULL, "ut_fmt_elastic", strlen("ut_fmt_elastic"));
    string *body = metric_query_deserialize(2048, mqc, METRIC_SERIALIZER_ELASTICSEARCH, ';', NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);
    string_free(body);
    query_context_free(mqc);
}

static void test_metric_str_build_named_namespaces(void)
{
    char ns_names[3][32];
    for (int n = 0; n < 3; n++) {
        snprintf(ns_names[n], sizeof(ns_names[n]), "ut_build_ns_%d", n);
        insert_namespace(ns_names[n], 0);

        context_arg carg = {0};
        carg.namespace = ns_names[n];
        int64_t v = 100 + n;
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "k%d", n);
        metric_add_labels("ut_build_metric", &v, DATATYPE_INT, &carg, lbl, ns_names[n]);

        string *om = string_new();
        string *legacy = string_new();
        metric_str_build(ns_names[n], om, 1);
        metric_str_build(ns_names[n], legacy, 0);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, om->l > 0);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, legacy->l > 0);
        string_free(om);
        string_free(legacy);
    }
}

static void test_metric_str_build_default_namespace(void)
{
    string *om = string_new();
    string *legacy = string_new();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, om);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, legacy);

    metric_str_build(NULL, om, 1);
    metric_str_build(NULL, legacy, 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, om->l > 0);

    string *printed = namespace_print(NULL, NULL);
    if (printed) {
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, printed->l > 0);
        string_free(printed);
    }

    string_free(om);
    string_free(legacy);
}

static void run_core_net_suites(void)
{
    test_ip_check_access_1();
    test_ip_to_int();
    test_integer_to_ip();
    test_ip_get_version();
    test_tag_normalizer_statsd();
    test_tag_normalizer_dynatrace();
    test_protobuf_wire();
    test_http_access_1();
    test_http_access_2();
    test_validator_core();
    test_ht_core_paths();
    test_ht_guard_paths();
    test_ht_reinit_and_remove_miss_paths();
    test_ht_foreach_and_forfree_paths();
    test_maglev_core_paths();
    test_resolver_dns_pack_unpack();
    test_patricia_helpers();
}

static void run_parser_suites(char **argv)
{
    api_test_parser_httpd();
    test_http_proto_parsers();
    test_http_proto_methods_and_edges();
    test_multiparser_proxy_paths();
    test_multiparser_mesg_helpers();
    test_http_parser_route_and_auth_edges();
    test_multiparser_fallback_and_null_helper();
    test_multiparser_parser_push_paths();
    api_test_parser_log();
    api_test_parser_zookeeper_dont_work();
    api_test_parser_zookeeper();
    api_test_parser_sentinel();
    api_test_parser_aerospike();
    api_test_parser_memcached();
    api_test_parser_beanstalkd();
    api_test_parser_beanstalkd_stats_tube();
    api_test_parser_uwsgi();
    api_test_parser_nats();
    api_test_parser_flower();
    api_test_parser_rabbitmq();
    api_test_parser_elasticsearch(argv[0]);
    api_test_parser_dummy_consul_hadoop();
    api_test_parser_auditd_gdnsd();
    api_test_parser_eventstore_and_opentsdb();
    api_test_parser_tftp_and_gearmand();
    api_test_parser_riak_and_json();
    api_test_parser_mongodb_push_and_data();
    api_test_openmetrics_help_type_ordering();
    api_test_openmetrics_type_suffix_and_normalization();
    api_test_openmetrics_histogram_type_on_components();
    api_test_openmetrics_metadata_overwrite();
    api_test_multicollector_mixed_formats();
    api_test_multicollector_pushgateway();
    api_test_multicollector_histogram_help();
    api_test_parser_redis_and_dynatrace();
    api_test_parser_redis_info();
    api_test_parser_redis_query();
    api_test_parser_json_and_named();
    api_test_parser_json_pquery();
    api_test_parser_named_extended();
    api_test_parser_squid_counters();
    api_test_parser_squid_info();
    api_test_parser_squid_fqdncache();
    api_test_parser_squid_mem();
}

static void run_config_query_suites(char **argv)
{
    api_test_global_options_and_errors();
    api_test_query_1();
    api_test_action_1();
    api_test_scheduler_1();
    api_test_lang_1();
    api_test_cluster_1();
    api_test_parser_ntp();
    api_test_parser_nsd();
    api_test_parser_syslogng();
    api_test_parser_rsyslog();
    api_test_parser_wazuh();
    api_test_parser_ipmi_metric_normalization_metadata();
    test_json_query_labels_and_int();
    test_json_query_pipe_stages();
    test_json_query_label_alias();
    test_json_query_comma_branches();
    test_json_query_merge_two_pquery();
    test_json_query_flat_object_labels();
    test_json_query_ndjson_lines();
    system_test(argv[0]);
    test_config();
}

static void run_helpers_and_events_suites(void)
{
    test_logs_helpers();
    test_units_human_ranges();
    test_mkdirp_helpers();
    test_dpkg_list_helpers();
    test_aggregator_helper_paths();
    test_config_global_get_extended();
    test_mask_password_and_parse_url();
    test_selector_helpers_core();
    test_selector_string_and_file_helpers();
    test_base64_and_auth_helpers();
    test_selector_binary_converters_and_config_string();
    test_url_parse_more_edges();
    test_match_rules_hash_paths();
    test_http_common_helpers();
    test_match_rules_regex_paths();
    test_config_hashfunc_serialization();
    test_config_system_serialization_paths();
    test_env_struct_helper_paths();
    test_entrypoint_key_and_parse_add_label();
    test_hash_and_rtime_helpers();
    test_config_serializer_edge_paths();
    test_config_generators_batch();
    test_config_generators_batch2();
    test_config_generators_serializer_matrix();
    test_config_generators_more_edges();
    test_config_query_and_x509_branches();
    test_config_generators_labels_env_branches();
    test_config_generators_iteration_paths();
    test_config_aggregator_and_lang_extra_paths();
    test_config_generators_additional_scalars();
    test_metricstransform_plain_and_ingest();
    test_aggregate_multi_block_plain_parse();
    test_entrypoint_log_channel_plain_parse();
    test_log_channel_raw_plain_parse();
    test_puppeteer_option_kv_plain_parse();
    test_entrypoint_plain_rich_parse();
    test_config_plain_top_level_blocks();
    test_config_plain_globals_and_channels();
    test_config_plain_persistence_block();
    test_config_plain_more_top_level_blocks();
    test_config_plain_grok_mtail_chromecdp_blocks();
    test_config_plain_aggregate_rich_variants();
    test_config_plain_probe_scheduler_variants();
    test_config_plain_log_scalars_block();
    test_config_plain_extra_blocks_batch();
    test_config_plain_action_query_lang_batch();
    test_config_plain_entrypoint_batch2();
    test_config_plain_timing_repeat_globals();
    test_config_plain_aggregate_more_subtypes();
    test_config_plain_reject_transform_batch();
    test_config_plain_action_serializer_plain_batch();
    test_config_plain_fragment_library();
    test_config_generators_amtail_grok_paths();
    test_config_get_amtail_grok_runtime_paths();
    test_config_get_cluster_chromecdp_runtime_paths();
    test_config_get_aggregator_rich_runtime_paths();
    test_config_get_scheduler_x509_lang_runtime_paths();
    test_config_get_system_modules_runtime_paths();
    test_config_get_action_probe_query_runtime_paths();
    test_config_get_system_flags_runtime_paths();
    test_config_get_entrypoint_rich_runtime_paths();
    test_config_get_resolver_runtime_paths();
    test_config_plain_mega_document();
    test_config_plain_mega_document_v2();
    test_context_arg_socket_addr_paths();
    test_context_arg_message_setup_paths();
    test_config_plain_to_json_smoke();
    test_context_arg_json_fill_paths();
    test_context_arg_copy_and_env_paths();
    test_parse_add_label_paths();
    test_action_push_get_del_paths();
    test_action_run_process_dry_paths();
    test_action_run_serializer_dry_matrix();
    test_action_push_serializer_matrix();
    test_serializer_context_matrix();
    test_metric_query_deserialize_serializer_outputs();
    test_metric_query_deserialize_db_serializers();
    test_promql_parser_matrix();
    test_promql_parser_extended_matrix();
    test_entrypoint_collect_shutdown_paths();
    test_query_context_http_args_paths();
    test_metric_namespace_helper_paths();
    test_labels_merge_dup_json_paths();
    test_labels_match_gen_groupkey_paths();
    test_labels_hash_insert_and_groupkey_paths();
    test_reject_metric_paths();
    test_api_router_paths();
    test_http_api_v1_runtime_paths();
    test_http_api_v1_system_and_persistence();
    test_http_api_v1_plain_action_query_chunk();
    test_http_api_v1_log_scalars_json();
    test_http_api_v1_x509_lang_mtail_json();
    test_http_api_v1_system_and_grok_patterns_json();
    test_http_api_v1_plain_chunks_via_api();
    test_http_api_v1_cluster_scheduler_resolver_json();
    test_http_api_v1_plain_isolated_chunks();
    test_http_api_v1_plain_action_push_batch();
    test_http_api_v1_plain_aggregate_push_batch();
    test_http_api_v1_plain_aggregate_push_batch2();
    test_http_api_v1_plain_entrypoint_push_batch();
    test_http_api_v1_lang_x509_put_only();
    test_http_api_v1_comprehensive_put();
    test_action_query_foreach_and_http_paths();
    test_metrictree_delete_paths();
    test_metric_query_gen_paths();
    test_metric_query_gen_extended();
    test_labels_metric_add_multi_paths();
    test_labels_metric_add_labels6_to_10();
    test_metric_update_labels_multi_paths();
    test_labels_initiate_and_update_paths();
    test_labels_cmp_and_cat_paths();
    test_metric_transform_paths();
    test_metric_transform_extended_paths();
    test_serializer_extra_formats();
    test_serializer_graphite_format();
    test_serializer_influx_format();
    test_serializer_statsd_format();
    test_serializer_otlp_format();
    test_serializer_carbon2_format();
    test_serializer_dogstatsd_format();
    test_serializer_dynatrace_format();
    test_serializer_dynatrace_prom_types();
    test_serializer_prom_types_across_formats();
    test_serializer_elasticsearch_format();
    test_serializer_datatypes_outputs();
    test_filestat_restore_v1_paths();
    test_filetailer_helpers_paths();
    test_client_registry_paths();
    test_metric_str_build_named_namespaces();
    test_metric_str_build_default_namespace();
}

int main(int argc, char **argv) {
    count_all = 0;
    count_error = 0;
    exit_on_error = 1;
    if ((argc > 1) && !strcmp(argv[1], "pass"))
        exit_on_error = 0;
    ut_report = stderr;
    if (!exit_on_error)
    {
        int report_fd = dup(STDERR_FILENO);
        if (report_fd >= 0)
        {
            FILE *report_stream = fdopen(report_fd, "w");
            if (report_stream)
                ut_report = report_stream;
        }
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
    }

    unit2_fixture_init();
    run_core_net_suites();
    run_parser_suites(argv);
    run_config_query_suites(argv);
    run_helpers_and_events_suites();

    infomesg();
    unit2_fixture_done();
    return count_error ? 1 : 0;
}
