#include <stdlib.h>
#include <string.h>
#include "config/get.h"
#include "common/url.h"
#include "common/selector.h"
#include "common/base64.h"
#include "common/auth.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/aggregator.h"
#include "common/units.h"
#include "common/mkdirp.h"
#include "common/murmurhash.h"
#include "common/crc32.h"
#include "common/xxh.h"
#include "common/rtime.h"
#include "main.h"
#include "lang/type.h"
#include "action/type.h"
#include "probe/probe.h"
#include "modules/modules.h"
#include "system/linux/sysctl.h"
#include "query/type.h"
#include "metric/namespace.h"
#include "x509/type.h"
#include "cluster/type.h"
#include "puppeteer/puppeteer.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

char *mask_password(const char *url);
uint64_t count_nl(char *buffer, uint64_t size);
size_t get_any_file_size(char *filename);
void config_global_get(json_t *dst);
void system_config_get(json_t *dst);
void system_mapper_generate_conf(void *funcarg, void* arg);
void system_pidfile_generate_conf(json_t *dst);
void lang_generate_conf(void *funcarg, void* arg);
void action_generate_conf(void *funcarg, void* arg);
void probe_generate_conf(void *funcarg, void* arg);
void modules_generate_conf(void *funcarg, void* arg);
void userprocess_generate_conf(void *funcarg, void* arg);
void groupprocess_generate_conf(void *funcarg, void* arg);
void sysctl_generate_conf(void *funcarg, void* arg);
void query_node_generate_conf(void *funcarg, void* arg);
void query_generate_conf(void *funcarg, void* arg);
void labels_kv_deserialize(void *funcarg, void* arg);
void env_struct_conf_deserialize(void *funcarg, void* arg);
void aggregator_generate_conf(void *funcarg, void* arg);
void fs_x509_generate_conf(void *funcarg, void* arg);
void cluster_generate_conf(void *funcarg, void* arg);
void entrypoints_generate_conf(void *funcarg, void* arg);
void resolver_generate_conf(json_t *dst);
uint64_t getrtime_ms(r_time t1, r_time t2);
void puppeteer_generate_conf(void *funcarg, void* arg);
void query_node_generate_field_conf(void *funcarg, void* arg);
uint8_t string_tokens_push_dupn(string_tokens *st, char *s, uint64_t l);
void glog(int priority, const char *format, ...);
void smart_aggregator_key_normalize(char *key);
void smart_aggregator_del_key_gen(char *transport_string, char *parser_name, char *host, char *port, char *query);
void aggregate_free_foreach(void *funcarg, void* arg);
size_t get_rec_dir (char *str, size_t len, uint64_t num, int *fin);
char * get_dir (char *str, uint64_t num, int *fin);
void dpkg_list(char *str, size_t len);

void test_config()
{
    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    json_decref(root);
}

void test_logs_helpers()
{
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_OFF, get_log_level_by_name("off", 3));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_FATAL, get_log_level_by_name("fatal", 5));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_ERROR, get_log_level_by_name("error", 5));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_WARN, get_log_level_by_name("warn", 4));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_INFO, get_log_level_by_name("info", 4));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_DEBUG, get_log_level_by_name("debug", 5));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_TRACE, get_log_level_by_name("trace", 5));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, get_log_level_by_name("4", 1));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_OFF, get_log_level_by_name("unknown", 7));

    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "off", get_log_level_by_id(L_OFF));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "fatal", get_log_level_by_id(L_FATAL));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "error", get_log_level_by_id(L_ERROR));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "warn", get_log_level_by_id(L_WARN));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "info", get_log_level_by_id(L_INFO));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "debug", get_log_level_by_id(L_DEBUG));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "trace", get_log_level_by_id(L_TRACE));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, get_log_level_by_id(777) != NULL);

    ac->log_dest = NULL;
    log_init();
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, fileno(stdout), ac->log_socket);

    ac->log_dest = "stdout";
    log_init();
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, fileno(stdout), ac->log_socket);

    ac->log_dest = "stderr";
    log_init();
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, fileno(stderr), ac->log_socket);

    ac->log_host = NULL;
    ac->log_port = 1514;
    ac->log_dest = "udp://127.0.0.1:1514";
    log_init();
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->log_socket >= 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->log_host);
    glog(L_INFO, "ut2 udp log\n");
    if (ac->log_socket > 2) {
        close(ac->log_socket);
    }
    if (ac->log_host) {
        free(ac->log_host);
        ac->log_host = NULL;
    }

    ac->log_dest = "file:///tmp/alligator-ut2.log";
    log_init();
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->log_socket >= 0);
    glog(L_INFO, "ut2 file log\n");
    if (ac->log_socket > 2) {
        close(ac->log_socket);
    }

    ac->log_dest = "some-unknown-dest";
    log_init();
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, fileno(stdout), ac->log_socket);
}

void test_units_human_ranges()
{
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4320000, get_ms_from_human_range("1h12m", strlen("1h12m")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 75000, get_ms_from_human_range("1m15s", strlen("1m15s")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1785852078LL, get_ms_from_human_range("18d64h4m12s78ms", strlen("18d64h4m12s78ms")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 123456000, get_ms_from_human_range("123456", strlen("123456")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, get_ms_from_human_range(NULL, 0));

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 123456, get_sec_from_human_range("123456", strlen("123456")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 75, get_sec_from_human_range("1m15s", strlen("1m15s")));
}

void test_mkdirp_helpers()
{
    char path1[] = "/tmp/alligator/unit2/a/b";
    int fin = 0;
    size_t rc0 = get_rec_dir(path1, strlen(path1), 0, &fin);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, rc0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, fin);

    fin = 0;
    size_t rc1 = get_rec_dir(path1, strlen(path1), 1, &fin);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 5, rc1);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, fin);

    fin = 0;
    size_t rc_last = get_rec_dir(path1, strlen(path1), 99, &fin);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)strlen(path1), (int)rc_last);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, fin);

    fin = 0;
    char *d0 = get_dir(path1, 0, &fin);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, d0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "", d0);
    free(d0);

    fin = 0;
    char *d1 = get_dir(path1, 2, &fin);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, d1);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/alligator/", d1);
    free(d1);
}

void test_dpkg_list_helpers()
{
    context_arg *saved_system_carg = ac->system_carg;
    match_rules *saved_packages_match = ac->packages_match;

    ac->system_carg = calloc(1, sizeof(*ac->system_carg));
    ac->packages_match = calloc(1, sizeof(*ac->packages_match));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->system_carg);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->packages_match);
    ac->packages_match->hash = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->packages_match->hash);

    char long_pkg[400];
    char long_ver[420];
    memset(long_pkg, 'p', sizeof(long_pkg) - 1);
    memset(long_ver, 'v', sizeof(long_ver) - 1);
    long_pkg[sizeof(long_pkg) - 1] = 0;
    long_ver[sizeof(long_ver) - 1] = 0;

    size_t payload_sz = 4096;
    char *payload = calloc(1, payload_sz);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, payload);
    snprintf(payload, payload_sz,
             "Package: %s\n"
             "Version: %s-1ubuntu1\n"
             "\n"
             "Package: shortpkg\n"
             "Version: 2.3.4-5\n"
             "\n",
             long_pkg, long_ver);

    dpkg_list(payload, strlen(payload));
    free(payload);

    metric_test_run(CMP_GREATER, "package_total", "package_total", 0);
    metric_test_run(CMP_GREATER, "package_installed", "package_installed", 0);

    alligator_ht_done(ac->packages_match->hash);
    free(ac->packages_match->hash);
    free(ac->packages_match);
    free(ac->system_carg);

    ac->packages_match = saved_packages_match;
    ac->system_carg = saved_system_carg;
}

void test_aggregator_helper_paths()
{
    char *key = calloc(512, 1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, key);
    int n = smart_aggregator_default_key(key, "tcp", "http", "127.0.0.1", "8080", "/metrics");
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, n > 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "tcp:http:127.0.0.1:8080/metrics", key);

    memset(key, 0, 512);
    smart_aggregator_default_key(key, "tcp", "http", "127.0.0.1", "8080", "metrics");
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "tcp:http:127.0.0.1:8080/metrics", key);

    char dirty[] = "ab{cd}~'\"ef\t\n";
    smart_aggregator_key_normalize(dirty);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "ab_cd____ef__", dirty);

    aggregate_context actx = {0};
    actx.key = "abc";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, actx_compare("abc", &actx));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, actx_compare("abd", &actx) > 0);

    context_arg carg_cmp = {0};
    carg_cmp.key = "k1";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, aggregator_compare("k1", &carg_cmp));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, aggregator_compare("k2", &carg_cmp) > 0);

    /* del_key_gen -> del_key -> set remove_from_hash without transport-side free paths. */
    alligator_ht *saved = ac->aggregators;
    ac->aggregators = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->aggregators);

    context_arg *c = calloc(1, sizeof(*c));
    c->transport = -1; /* avoid smart_aggregator_del transport branches */
    c->key = strdup("tcp:http:127.0.0.1:8080/metrics");
    alligator_ht_insert(ac->aggregators, &c->node, c, tommy_strhash_u32(0, c->key));

    smart_aggregator_del_key_gen("tcp", "http", "127.0.0.1", "8080", "/metrics");
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, c->remove_from_hash);

    context_arg *r = alligator_ht_remove(ac->aggregators, aggregator_compare, c->key, tommy_strhash_u32(0, c->key));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, r);
    free(r->key);
    free(r);
    alligator_ht_done(ac->aggregators);
    free(ac->aggregators);
    ac->aggregators = saved;

    /* direct free-foreach helper path */
    aggregate_context *af = calloc(1, sizeof(*af));
    af->handler = calloc(1, sizeof(*af->handler));
    af->key = strdup("af");
    af->data = calloc(1, 8);
    aggregate_free_foreach(NULL, af);

    /* smart_aggregator: safe success/duplicate paths with unsupported transport */
    saved = ac->aggregators;
    ac->aggregators = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->aggregators);

    context_arg *sa = calloc(1, sizeof(*sa));
    sa->transport = -1;
    sa->transport_string = "x";
    sa->parser_name = "p";
    snprintf(sa->host, sizeof(sa->host), "%s", "h");
    snprintf(sa->port, sizeof(sa->port), "%s", "1");
    sa->query_url = "/q";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, smart_aggregator(sa));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sa->key);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "x:p:h:1/q", sa->key);

    context_arg *sa_dup = calloc(1, sizeof(*sa_dup));
    sa_dup->transport = -1;
    sa_dup->key = strdup(sa->key);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, smart_aggregator(sa_dup));

    context_arg *sa_rm = alligator_ht_remove(ac->aggregators, aggregator_compare, sa->key, tommy_strhash_u32(0, sa->key));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sa_rm);
    free(sa_rm->key);
    free(sa_rm);
    free(sa_dup->key);
    free(sa_dup);

    /* delete-by-generated-key path when query has no leading slash */
    context_arg *sa2 = calloc(1, sizeof(*sa2));
    sa2->transport = -1;
    sa2->key = strdup("x:p:h:1/q2");
    alligator_ht_insert(ac->aggregators, &sa2->node, sa2, tommy_strhash_u32(0, sa2->key));
    smart_aggregator_del_key_gen("x", "p", "h", "1", "q2");
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, sa2->remove_from_hash);
    context_arg *sa2_rm = alligator_ht_remove(ac->aggregators, aggregator_compare, sa2->key, tommy_strhash_u32(0, sa2->key));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sa2_rm);
    free(sa2_rm->key);
    free(sa2_rm);

    /* explicit no-op normalization */
    char clean_key[] = "abc:xyz:1/ok";
    smart_aggregator_key_normalize(clean_key);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "abc:xyz:1/ok", clean_key);

    alligator_ht_done(ac->aggregators);
    free(ac->aggregators);
    ac->aggregators = saved;
    free(key);
}

void test_config_global_get_extended()
{
    ac->log_level = L_DEBUG;
    ac->aggregator_repeat = 3210;
    ac->tls_fs_repeat = 2222;
    ac->system_aggregator_repeat = 3333;
    ac->query_repeat = 4444;
    ac->cluster_repeat = 5555;
    ac->ttl = 77;
    ac->persistence_dir = "/tmp/alligator-ut2";

    setenv("UV_THREADPOOL_SIZE", "9", 1);
    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    json_t *jlog = json_object_get(root, "log_level");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, jlog);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "debug", json_string_value(jlog));

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3210, json_integer_value(json_object_get(root, "aggregate_period")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2222, json_integer_value(json_object_get(root, "tls_collect_period")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3333, json_integer_value(json_object_get(root, "system_collect_period")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4444, json_integer_value(json_object_get(root, "query_period")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 5555, json_integer_value(json_object_get(root, "synchronization_period")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 77, json_integer_value(json_object_get(root, "ttl")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9, json_integer_value(json_object_get(root, "workers")));

    json_t *persist = json_object_get(root, "persistence");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, persist);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/alligator-ut2", json_string_value(json_object_get(persist, "directory")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3210, json_integer_value(json_object_get(persist, "period")));

    json_decref(root);
    unsetenv("UV_THREADPOOL_SIZE");
}

void test_mask_password_and_parse_url()
{
    char *masked = mask_password("https://user:secret@example.org:443/path");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, masked);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "https://user:*******@example.org:443/path", masked);
    free(masked);

    masked = mask_password("http://example.org/no-auth");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, masked);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "http://example.org/no-auth", masked);
    free(masked);

    char url1[] = "https://user:pass@example.org:8443/path?q=1";
    host_aggregator_info *hi = parse_url(url1, strlen(url1));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, APROTO_HTTPS, hi->proto);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, APROTO_TLS, hi->transport);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, hi->tls);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "example.org", hi->host);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "8443", hi->port);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/path?q=1", hi->query);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "user", hi->user);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "pass", hi->pass);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi->auth);
    url_free(hi);

    char url2[] = "http://example.org";
    hi = parse_url(url2, strlen(url2));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "80", hi->port);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/", hi->query);
    url_free(hi);

    char url3[] = "file:///var/log/messages";
    hi = parse_url(url3, strlen(url3));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, APROTO_FILE, hi->proto);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/var/log/messages", hi->host);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "", hi->query);
    url_free(hi);
}

void test_selector_helpers_core()
{
    char data[] = "alpha 11\nbeta 22\ngamma 33\n";
    char *line = malloc(64);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, line);
    char *ln = selector_getline(data, strlen(data), line, 64, 1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ln);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "beta 22", line);

    char *field = selector_get_field_by_str(data, strlen(data), "beta", 2, " ");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, field);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "22", field);
    free(field);

    uint64_t cursor = 0;
    char nums[] = "a,-7,15,42";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -7, int_get_next(nums, strlen(nums), ',', &cursor));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 15, int_get_next(nums, strlen(nums), ',', &cursor));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 42, int_get_next(nums, strlen(nums), ',', &cursor));

    cursor = 0;
    char unums[] = "x,7,15";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 7, uint_get_next(unums, strlen(unums), ',', &cursor));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 15, uint_get_next(unums, strlen(unums), ',', &cursor));

    cursor = 0;
    char *token = malloc(16);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, token);
    str_get_next("abc:def", token, 16, ":", &cursor);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "abc", token);
    free(token);

    string_tokens *st = string_tokens_char_split_any("a,b;c", strlen("a,b;c"), ",;");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, st);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, st->l);
    string *joined = string_tokens_join(st, "|", 1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, joined);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a|b|c", joined->s);
    string_free(joined);
    string_tokens_free(st);
    free(line);
}

void test_selector_string_and_file_helpers()
{
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, uint_min(2, 9));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, strcspn_n("abc:def", ":", 10));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, count_nl("a\nb\n", 4));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, selector_count_field("a,b,c", ",", strlen("a,b,c")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, sisdigit("123456"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, sisdigit("12a"));

    char mixed[] = "AbC12";
    str_tolower(mixed, sizeof(mixed));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "abc12", mixed);

    char lower_before[] = "ABC:DEF";
    to_lower_before(lower_before, ":");
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "abc:def", lower_before);

    char ws[] = "  xx yy  ";
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "xx yy", trim_whitespaces(ws));

    char tbuf[] = " \t ping \n";
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "ping", trim(tbuf));

    string *s = string_init(4);
    string_cat(s, "ab", 2);
    string_cat(s, "cd", 2);
    string_cat(s, "ef", 2);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "abcdef", s->s);

    string_sprintf(s, "-%d", 77);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(s->s, "-77") != NULL);

    string *d = string_new();
    string_copy(d, "hello", 5);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "hello", d->s);
    string_string_cat(d, s);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(d->s, "helloabcdef") != NULL);

    string_break(d, 5, 11);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "abcdef", d->s);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, string_compare(d, "abcdef", 6));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, string_compare(NULL, "abcdef", 6));

    string *n = string_new();
    string_uint(n, 42);
    string_cat(n, ",", 1);
    string_int(n, -9);
    string_cat(n, ",", 1);
    double pi = 3.5;
    string_number(n, &pi, DATATYPE_DOUBLE);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(n->s, "42,-9,3.500000") != NULL);

    string_free(n);
    string_free(d);
    string_free(s);

    char path[] = "/tmp/alligator_ut_file_XXXXXX";
    int fd = mkstemp(path);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, fd >= 0);
    char payload[] = "10\n20\nname\n";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)sizeof(payload) - 1, (int)write(fd, payload, sizeof(payload) - 1));
    close(fd);

    struct stat st;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, stat(path, &st));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, st.st_size, get_file_size(path));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, st.st_size + 1, get_any_file_size(path));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, count_file_lines(path));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10, getkvfile(path));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10, getkvfile_uint(path));

    uint8_t err = 0;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10, getkvfile_ext(path, &err));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, err);

    char *out = malloc(32);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, out);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, getkvfile_str(path, out, 32));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "10", out);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, get_file_atime(path) > 0);

    string *fc = get_file_content(path, 1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, fc);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(fc->s, "name") != NULL);
    string_free(fc);

    unlink(path);

    char missing[] = "/tmp/alligator_missing_file_zz";
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, getkvfile(missing));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, getkvfile_uint(missing));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, getkvfile_str(missing, out, 32));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, get_file_content(missing, 0) != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, get_any_file_size(missing));

    err = 0;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, getkvfile_ext(missing, &err));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, err);
    free(out);
}

void test_base64_and_auth_helpers()
{
    size_t outlen = 0;
    char *enc = base64_encode("root:qwerty", strlen("root:qwerty"), &outlen);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, enc);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "cm9vdDpxd2VydHk=", enc);

    size_t declen = 0;
    char *dec = base64_decode(enc, outlen, &declen);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dec);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "root:qwerty", dec);
    free(dec);
    free(enc);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        base64_decode("abc", strlen("abc"), &declen) != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        base64_decode("%%%%", 4, &declen) != NULL);

    alligator_ht *h = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, h);
    http_auth_push(h, "token1");
    http_auth_push(h, "token2");

    alligator_ht *copied = http_auth_copy(h);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, copied);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_auth_delete(copied, "token1"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, http_auth_delete(copied, "token1"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, http_auth_delete(copied, "token2"));

    alligator_ht_foreach_arg(copied, http_auth_foreach_free, NULL);
    alligator_ht_done(copied);
    free(copied);

    alligator_ht_foreach_arg(h, http_auth_foreach_free, NULL);
    alligator_ht_done(h);
    free(h);
}

void test_selector_binary_converters_and_config_string()
{
    char b16[] = {0x34, 0x12};
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x1234, to_uint16(b16));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x3412, to_uint16_swap(b16));

    char b32[] = {0x78, 0x56, 0x34, 0x12};
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x12345678U, to_uint32(b32));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x78563412U, to_uint32_swap(b32));

    char b64[] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 0x0102030405060708ULL, to_uint64(b64));
    assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 0x0807060504030201ULL, to_uint64_swap(b64));

    char *b128 = calloc(16, 1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, b128);
    b128[0] = 1;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, to_uint128(b128) > 0);
    free(b128);

    string *cfg = config_get_string();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cfg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, cfg->l > 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(cfg->s, "{") != NULL);
    string_free(cfg);
}

void test_url_parse_more_edges()
{
    char u1[] = "http://example.org?x=1";
    host_aggregator_info *hi = parse_url(u1, strlen(u1));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "example.org", hi->host);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "?x=1", hi->query);
    url_free(hi);

    char u2[] = "tcp://user@example.com";
    hi = parse_url(u2, strlen(u2));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, APROTO_TCP, hi->proto);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "user@example.com", hi->user);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "example.com", hi->host);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "", hi->query);
    url_free(hi);

    char u3[] = "http://unix:/var/run/alligator.sock:localhost/metrics";
    hi = parse_url(u3, strlen(u3));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, APROTO_UNIX, hi->transport);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/var/run/alligator.sock", hi->host);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "localhost", hi->host_header);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/metrics", hi->query);
    url_free(hi);

    char u4[] = "https://example.org:9443";
    hi = parse_url(u4, strlen(u4));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "9443", hi->port);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/", hi->query);
    url_free(hi);
}

void test_match_rules_hash_paths()
{
    match_rules *mr = calloc(1, sizeof(*mr));
    mr->hash = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mr->hash);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, match_mapper(mr, "any", strlen("any"), "n"));

    match_push(mr, "postgres", strlen("postgres"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(mr->hash));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, match_mapper(mr, "postgres", strlen("postgres"), "n"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, match_mapper(mr, "mysql", strlen("mysql"), "n"));

    match_del(mr, "postgres", strlen("postgres"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_count(mr->hash));

    match_free(mr);
}

void test_http_common_helpers()
{
    char *encbuf = malloc(128);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, encbuf);
    uint64_t enclen = urlencode(encbuf, "a b+c/=", strlen("a b+c/="));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 15, enclen);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a%20b%2bc%2f%3d", encbuf);
    free(encbuf);

    alligator_ht *args = http_get_args("/x?foo=bar&x=10%2f20&plus=a+b", strlen("/x?foo=bar&x=10%2f20&plus=a+b"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, args);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "bar", http_get_param(args, "foo"));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "10/20", http_get_param(args, "x"));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a b", http_get_param(args, "plus"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, http_get_param(args, "none") != NULL);
    http_args_free(args);

    args = http_get_args(NULL, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, args);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_count(args));
    http_args_free(args);

    args = http_get_args("/x?foo=1&foo=2&bar=3", strlen("/x?foo=1&foo=2&bar=3"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, args);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "1", http_get_param(args, "foo"));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "3", http_get_param(args, "bar"));
    http_args_free(args);

    string *body = string_init(32);
    string_cat(body, "{\"ok\":1}", strlen("{\"ok\":1}"));
    char *req = gen_http_query(
        HTTP_POST,
        "/api",
        "v1",
        "example.org",
        "ut-agent",
        "YWJj",
        "1.1",
        NULL,
        NULL,
        body
    );
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, req);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "POST /apiv1 HTTP/1.1\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "User-Agent: ut-agent\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "Host: example.org\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "Authorization: Basic YWJj\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "{\"ok\":1}") != NULL);
    free(req);
    string_free(body);

    req = gen_http_query(HTTP_GET, "/", "metrics", "/tmp/socket", "ua", NULL, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, req);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "GET /metrics HTTP/1.0\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "Host: localhost\r\n") != NULL);
    free(req);

    alligator_ht *env = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, env);
    env_struct_push_alloc(env, "X-Test", "yes");
    req = gen_http_query(HTTP_GET, "/", "p", "example.org", "ua2", NULL, "1.1", env, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, req);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "X-Test: yes\r\n") != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(req, "HTTP/1.1\r\n") != NULL);
    free(req);
    env_free(env);
}

void test_config_hashfunc_serialization()
{
    ac->metrictree_hashfunc = murmurhash;
    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "murmur", json_string_value(json_object_get(root, "metrictree_hashfunc")));
    json_decref(root);

    ac->metrictree_hashfunc = crc32;
    root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "crc32", json_string_value(json_object_get(root, "metrictree_hashfunc")));
    json_decref(root);

    ac->metrictree_hashfunc = xxh3_run;
    root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "XXH3", json_string_value(json_object_get(root, "metrictree_hashfunc")));
    json_decref(root);

    ac->metrictree_hashfunc = alligator_ht_strhash;
}

void test_config_system_serialization_paths()
{
    ac->system_base = 1;
    ac->system_disk = 1;
    ac->system_network = 1;
    ac->system_firewall = 1;
    ac->system_ipset_entries = 1;
    ac->system_packages = 1;
    ac->system_services = 1;
    ac->system_process = 1;
    ac->system_cpuavg = 1;
    ac->system_sysfs = "/sys-ut";
    ac->system_procfs = "/proc-ut";
    ac->system_rundir = "/run-ut";
    ac->system_usrdir = "/usr-ut";
    ac->system_etcdir = "/etc-ut";

    unsetenv("UV_THREADPOOL_SIZE");
    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    json_t *system = json_object_get(root, "system");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, system);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "base"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "disk"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "network"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "packages"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "services"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "process"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "cpuavg"));

    json_t *fw = json_object_get(system, "firewall");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, fw);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "entries", json_string_value(json_object_get(fw, "ipset")));

    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/sys-ut", json_string_value(json_object_get(system, "sysfs")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/proc-ut", json_string_value(json_object_get(system, "procfs")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/run-ut", json_string_value(json_object_get(system, "rundir")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/usr-ut", json_string_value(json_object_get(system, "usrdir")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/etc-ut", json_string_value(json_object_get(system, "etcdir")));

    /* default worker count path when env is not set */
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, json_integer_value(json_object_get(root, "workers")));
    json_decref(root);

    ac->system_base = 0;
    ac->system_disk = 0;
    ac->system_network = 0;
    ac->system_firewall = 0;
    ac->system_ipset_entries = 0;
    ac->system_packages = 0;
    ac->system_services = 0;
    ac->system_process = 0;
    ac->system_cpuavg = 0;
    ac->system_sysfs = NULL;
    ac->system_procfs = NULL;
    ac->system_rundir = NULL;
    ac->system_usrdir = NULL;
    ac->system_etcdir = NULL;
}

void test_env_struct_helper_paths()
{
    alligator_ht *env = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, env);

    env_struct_push_alloc(env, "K1", "V1");
    env_struct_push_alloc(env, "K1", "V2"); /* duplicate key ignored */
    env_struct_push_alloc(env, "K2", "V2");
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(env));

    json_t *dump = env_struct_dump(env);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dump);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "V1", json_string_value(json_object_get(dump, "K1")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "V2", json_string_value(json_object_get(dump, "K2")));
    json_decref(dump);

    alligator_ht *dup = env_struct_duplicate(env);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, dup);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(dup));
    env_free(dup);

    json_t *root = json_object();
    json_t *jenv = json_object();
    json_array_object_insert(jenv, "A", json_string("1"));
    json_array_object_insert(jenv, "B", json_string("2"));
    json_array_object_insert(root, "env", jenv);
    alligator_ht *parsed = env_struct_parser(root);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, parsed);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(parsed));
    env_free(parsed);
    json_decref(root);

    env_free(env);
}

void test_hash_and_rtime_helpers()
{
    const char *s = "alligator";
    uint64_t m1 = murmurhash(s, strlen(s), 0);
    uint64_t m2 = murmurhash_get(s);
    assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, m1, m2);

    uint64_t c1 = crc32(s, strlen(s), 0);
    uint64_t c2 = crc32_get(s);
    assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, c1, c2);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, c1 != 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, crc32("", 0, 0));

    /* keep deltas within signed 32-bit math used in implementation */
    r_time t1 = {1, 100};
    r_time t2 = {2, 200};
    assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 1000000100ULL, getrtime_ns(t1, t2));
    assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 1000000ULL, getrtime_mcs(t1, t2, 0));
    assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 1000ULL, getrtime_ms(t1, t2));
    assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 2000ULL, getrtime_now_ms(t2));

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, getrtime_sec_float(t1, t2) > 0.9);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, getrtime_msec_float(t2, t1) >= 1000.0);
}

void test_config_serializer_edge_paths()
{
    ac->log_level = 9999;
    ac->metrictree_hashfunc = alligator_ht_strhash;
    unsetenv("UV_THREADPOOL_SIZE");

    json_t *root = json_object();
    config_global_get(root);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9999, json_integer_value(json_object_get(root, "log_level")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "lookup3", json_string_value(json_object_get(root, "metrictree_hashfunc")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, json_integer_value(json_object_get(root, "workers")));
    json_decref(root);

    setenv("UV_THREADPOOL_SIZE", "0", 1);
    root = json_object();
    config_global_get(root);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, json_integer_value(json_object_get(root, "workers")));
    json_decref(root);
    unsetenv("UV_THREADPOOL_SIZE");

    ac->system_firewall = 1;
    ac->system_ipset_entries = 0;
    ac->system_ipset = 1;
    root = json_object();
    system_config_get(root);
    json_t *system = json_object_get(root, "system");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, system);
    json_t *fw = json_object_get(system, "firewall");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, fw);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "on", json_string_value(json_object_get(fw, "ipset")));
    json_decref(root);
    ac->system_firewall = 0;
    ac->system_ipset = 0;

    struct {
        void *arg;
        json_t *dst;
    } cgarg = {0};
    cgarg.arg = "process";
    cgarg.dst = json_object();
    match_string ms1 = {.s = "nginx", .count = 1};
    match_string ms2 = {.s = "redis", .count = 1};
    system_mapper_generate_conf(&cgarg, &ms1);
    system_mapper_generate_conf(&cgarg, &ms2);
    system = json_object_get(cgarg.dst, "system");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, system);
    json_t *process = json_object_get(system, "process");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, process);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(process));
    json_decref(cgarg.dst);

    pidfile_node n2 = {.pidfile = "/tmp/cgroup.slice", .type = 1, .next = NULL};
    pidfile_node n1 = {.pidfile = "/tmp/alligator.pid", .type = 0, .next = &n2};
    pidfile_list plist = {.head = &n1, .tail = &n2};
    pidfile_list *old_plist = ac->system_pidfile;
    ac->system_pidfile = &plist;

    root = json_object();
    system_pidfile_generate_conf(root);
    system = json_object_get(root, "system");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, system);
    json_t *pidfile = json_object_get(system, "pidfile");
    json_t *cgroup = json_object_get(system, "cgroup");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, pidfile);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cgroup);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/alligator.pid", json_string_value(json_array_get(pidfile, 0)));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/cgroup.slice", json_string_value(json_array_get(cgroup, 0)));
    json_decref(root);

    ac->system_pidfile = old_plist;
}

void test_config_generators_batch()
{
    json_t *dst = json_object();

    lang_options lo1 = {0};
    lo1.key = "lua_test";
    lo1.lang = "lua";
    lo1.method = "run";
    lo1.arg = "safe-arg";
    lo1.file = "/tmp/script.lua";
    lo1.module = "mod1";
    lo1.query = "sum(metric)";
    lo1.log_level = L_INFO;
    lo1.serializer = METRIC_SERIALIZER_DSV;
    lo1.conf = 1;
    lang_generate_conf(dst, &lo1);

    lang_options lo2 = {0};
    lo2.key = "lua_hidden";
    lo2.lang = "lua";
    lo2.hidden_arg = 1;
    lo2.arg = "should-not-be-exported";
    lo2.serializer = 999;
    lang_generate_conf(dst, &lo2);

    json_t *lang = json_object_get(dst, "lang");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, lang);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(lang));
    json_t *lang0 = json_array_get(lang, 0);
    json_t *lang1 = json_array_get(lang, 1);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "lua_test", json_string_value(json_object_get(lang0, "key")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "dsv", json_string_value(json_object_get(lang0, "serializer")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_integer_value(json_object_get(lang0, "conf")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_is_true(json_object_get(lang1, "hidden_arg")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, json_object_get(lang1, "arg") != NULL);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "none", json_string_value(json_object_get(lang1, "serializer")));

    action_node an = {0};
    an.expr = "up == 0";
    an.name = "alert_web";
    an.ns = "prod";
    an.follow_redirects = 1;
    an.dry_run = 1;
    an.serializer = METRIC_SERIALIZER_INFLUXDB;
    action_generate_conf(dst, &an);

    json_t *action = json_object_get(dst, "action");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, action);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(action));
    json_t *action0 = json_array_get(action, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "alert_web", json_string_value(json_object_get(action0, "name")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "influxdb", json_string_value(json_object_get(action0, "serializer")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_is_true(json_object_get(action0, "dry_run")));

    probe_node pn = {0};
    char *statuses[] = {"200", "302"};
    pn.name = "http_probe";
    pn.source_ip_address = "127.0.0.2";
    pn.body = "ok";
    pn.prober_str = "http";
    pn.http_proxy_url = "http://proxy.local:3128";
    pn.ca_file = "/tmp/ca.pem";
    pn.cert_file = "/tmp/cert.pem";
    pn.key_file = "/tmp/key.pem";
    pn.server_name = "example.org";
    pn.follow_redirects = 1;
    pn.tls_verify = 1;
    pn.timeout = 2500;
    pn.tls = 1;
    pn.loop = 3;
    pn.percent_success = 99.5;
    pn.method = HTTP_POST;
    pn.valid_status_codes = statuses;
    pn.valid_status_codes_size = 2;
    probe_generate_conf(dst, &pn);

    json_t *probe = json_object_get(dst, "probe");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, probe);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(probe));
    json_t *probe0 = json_array_get(probe, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "POST", json_string_value(json_object_get(probe0, "method")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "on", json_string_value(json_object_get(probe0, "tls")));
    json_t *valid_status_codes = json_object_get(probe0, "valid_status_codes");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, valid_status_codes);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(valid_status_codes));

    module_t mod = {0};
    mod.key = "luaext";
    mod.path = "/tmp/luaext.so";
    modules_generate_conf(dst, &mod);
    json_t *modules = json_object_get(dst, "modules");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, modules);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/luaext.so", json_string_value(json_object_get(modules, "luaext")));

    userprocess_node up = {0};
    up.name = "nginx";
    userprocess_generate_conf(dst, &up);
    groupprocess_generate_conf(dst, &up);
    json_t *system = json_object_get(dst, "system");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, system);
    json_t *userprocess = json_object_get(system, "userprocess");
    json_t *groupprocess = json_object_get(system, "groupprocess");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, userprocess);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, groupprocess);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "nginx", json_string_value(json_array_get(userprocess, 0)));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "nginx", json_string_value(json_array_get(groupprocess, 0)));

    sysctl_node sc = {0};
    sc.name = "vm.max_map_count";
    sysctl_generate_conf(dst, &sc);
    json_t *sysctl = json_object_get(system, "sysctl");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sysctl);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "vm.max_map_count", json_string_value(json_array_get(sysctl, 0)));

    query_node qn = {0};
    qn.expr = "sum(rate(http_requests_total[5m]))";
    qn.make = "http_qps";
    qn.action = "store";
    qn.ns = "prod";
    qn.datasource = "prometheus";
    query_node_generate_conf(dst, &qn);
    json_t *query = json_object_get(dst, "query");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, query);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(query));
    json_t *query0 = json_array_get(query, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "http_qps", json_string_value(json_object_get(query0, "make")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "prometheus", json_string_value(json_object_get(query0, "datasource")));

    json_decref(dst);
}

void test_config_generators_batch2()
{
    json_t *dst = json_object();

    labels_container lc = {0};
    lc.name = "service";
    lc.key = "api";
    labels_kv_deserialize(dst, &lc);
    json_t *add_label = json_object_get(dst, "add_label");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, add_label);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "api", json_string_value(json_object_get(add_label, "service")));

    env_struct es = {0};
    es.k = "K_ENV";
    es.v = "V_ENV";
    env_struct_conf_deserialize(dst, &es);
    json_t *env = json_object_get(dst, "env");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, env);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "V_ENV", json_string_value(json_object_get(env, "K_ENV")));
    json_decref(dst);

    dst = json_object();
    context_arg carg = {0};
    char *pquery_arr[] = {"$.a", "$.b"};
    carg.url = "https://user:pass@example.org/metrics";
    carg.parser_name = "json";
    carg.follow_redirects = 1;
    carg.period = 7;
    carg.pquery = pquery_arr;
    carg.pquery_size = 2;
    aggregator_generate_conf(dst, &carg);
    json_t *aggregate = json_object_get(dst, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(aggregate));
    json_t *agg0 = json_array_get(aggregate, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "https://user:*******@example.org/metrics", json_string_value(json_object_get(agg0, "url")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_integer_value(json_object_get(agg0, "follow_redirects")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 7, json_integer_value(json_object_get(agg0, "period")));
    json_t *pquery = json_object_get(agg0, "pquery");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, pquery);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(pquery));
    json_decref(dst);

    dst = json_object();
    x509_fs_t xfs = {0};
    xfs.path = "/etc/ssl/certs";
    xfs.name = "certscan";
    xfs.match = string_tokens_new();
    string_tokens_push_dupn(xfs.match, "crt", 3);
    string_tokens_push_dupn(xfs.match, "pem", 3);
    fs_x509_generate_conf(dst, &xfs);
    json_t *x509 = json_object_get(dst, "x509");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, x509);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(x509));
    json_t *x0 = json_array_get(x509, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/etc/ssl/certs", json_string_value(json_object_get(x0, "path")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "certscan", json_string_value(json_object_get(x0, "name")));
    string_tokens_free(xfs.match);
    json_decref(dst);

    dst = json_object();
    cluster_server_oplog servers[2] = {{0}};
    servers[0].name = "srv-a";
    servers[0].is_me = 1;
    servers[1].name = "srv-b";
    servers[1].is_me = 0;
    cluster_node cn = {0};
    cn.name = "main";
    cn.servers = servers;
    cn.servers_size = 2;
    cn.replica_factor = 2;
    cluster_generate_conf(dst, &cn);
    json_t *cluster = json_object_get(dst, "cluster");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cluster);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "main", json_string_value(json_object_get(cluster, "name")));
    json_t *srv = json_object_get(cluster, "servers");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, srv);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(srv));
    json_decref(dst);

    dst = json_object();
    context_arg ep = {0};
    ep.ttl = 15;
    ep.pingloop = 9;
    ep.threads = 3;
    ep.api_enable = 1;
    ep.key = "ep1";
    ep.cluster = "main";
    ep.instance = "node-a";
    ep.lang = "en";
    ep.metric_aggregation = ENTRYPOINT_AGGREGATION_COUNT;
    entrypoints_generate_conf(dst, &ep);
    json_t *entrypoint = json_object_get(dst, "entrypoint");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, entrypoint);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(entrypoint));
    json_t *ep0 = json_array_get(entrypoint, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "count", json_string_value(json_object_get(ep0, "metric_aggregation")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 15, json_integer_value(json_object_get(ep0, "ttl")));
    json_decref(dst);

    resolver_data rdata = {0};
    host_aggregator_info *hi = calloc(1, sizeof(*hi));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    hi->host = strdup("8.8.8.8");
    hi->url = strdup("udp://8.8.8.8:53");
    hi->user = strdup("");
    hi->transport_string = strdup("udp");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi->host);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi->url);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi->user);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi->transport_string);
    snprintf(hi->port, sizeof(hi->port), "%s", "53");
    rdata.hi = hi;

    resolver_data *old_srv_resolver[255] = {0};
    uint8_t old_resolver_size = ac->resolver_size;
    for (uint16_t i = 0; i < 255; ++i) {
        old_srv_resolver[i] = ac->srv_resolver[i];
    }
    ac->srv_resolver[0] = &rdata;
    ac->resolver_size = 1;

    dst = json_object();
    resolver_generate_conf(dst);
    json_t *resolver = json_object_get(dst, "resolver");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, resolver);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(resolver));
    json_t *rs0 = json_array_get(resolver, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "8.8.8.8", json_string_value(json_object_get(rs0, "host")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "udp", json_string_value(json_object_get(rs0, "transport")));
    json_decref(dst);

    for (uint16_t i = 0; i < 255; ++i) {
        ac->srv_resolver[i] = old_srv_resolver[i];
    }
    ac->resolver_size = old_resolver_size;
    free(hi->host);
    free(hi->url);
    free(hi->user);
    free(hi->transport_string);
    free(hi);
}

void test_config_generators_serializer_matrix()
{
    json_t *dst = json_object();

    /* lang serializer matrix */
    lang_options lo = {0};
    lo.key = "lang-json";
    lo.serializer = METRIC_SERIALIZER_JSON;
    lang_generate_conf(dst, &lo);
    lo.key = "lang-openmetrics";
    lo.serializer = METRIC_SERIALIZER_OPENMETRICS;
    lang_generate_conf(dst, &lo);
    lo.key = "lang-unknown";
    lo.serializer = 12345;
    lang_generate_conf(dst, &lo);
    json_t *lang = json_object_get(dst, "lang");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, lang);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "json", json_string_value(json_object_get(json_array_get(lang, 0), "serializer")));
    /* serializer=0 (OPENMETRICS) does not enter `if (lo->serializer)` branch */
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, json_object_get(json_array_get(lang, 1), "serializer") != NULL);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "none", json_string_value(json_object_get(json_array_get(lang, 2), "serializer")));

    /* action serializer matrix */
    const int serializers[] = {
        METRIC_SERIALIZER_JSON, METRIC_SERIALIZER_OTLP, METRIC_SERIALIZER_OTLP_PROTOBUF,
        METRIC_SERIALIZER_OPENMETRICS, METRIC_SERIALIZER_CLICKHOUSE, METRIC_SERIALIZER_ELASTICSEARCH,
        METRIC_SERIALIZER_DSV, METRIC_SERIALIZER_CARBON2, METRIC_SERIALIZER_GRAPHITE,
        METRIC_SERIALIZER_STATSD, METRIC_SERIALIZER_DOGSTATSD, METRIC_SERIALIZER_DYNATRACE,
        METRIC_SERIALIZER_INFLUXDB, METRIC_SERIALIZER_PG, METRIC_SERIALIZER_CASSANDRA, 9999
    };
    const char *expected[] = {
        "json", "otlp", "otlp_protobuf",
        "openmetrics", "clickhouse", "elasticsearch",
        "dsv", "carbon2", "graphite",
        "statsd", "dogstatsd", "dynatrace",
        "influxdb", "postgresql", "cassandra", "none"
    };

    for (size_t i = 0; i < sizeof(serializers) / sizeof(serializers[0]); ++i) {
        action_node an = {0};
        an.name = "matrix";
        an.serializer = serializers[i];
        action_generate_conf(dst, &an);
    }

    json_t *action = json_object_get(dst, "action");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, action);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)(sizeof(serializers) / sizeof(serializers[0])), json_array_size(action));
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        json_t *a = json_array_get(action, i);
        if (serializers[i] == METRIC_SERIALIZER_OPENMETRICS) {
            /* serializer=0 is skipped by current implementation */
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, json_object_get(a, "serializer") != NULL);
        } else {
            assert_equal_string(__FILE__, __FUNCTION__, __LINE__, (char*)expected[i], json_string_value(json_object_get(a, "serializer")));
        }
    }

    /* probe method fallback branch */
    probe_node pn = {0};
    pn.name = "probe-none";
    pn.method = 77;
    probe_generate_conf(dst, &pn);
    json_t *probe = json_object_get(dst, "probe");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, probe);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "none", json_string_value(json_object_get(json_array_get(probe, 0), "method")));

    /* no-op branches: query_generate_conf(NULL hash) and resolver_generate_conf(size=0) */
    query_ds qds = {0};
    qds.hash = NULL;
    query_generate_conf(dst, &qds);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, json_object_get(dst, "query") != NULL);

    resolver_data *old_srv_resolver[255] = {0};
    uint8_t old_resolver_size = ac->resolver_size;
    for (uint16_t i = 0; i < 255; ++i) {
        old_srv_resolver[i] = ac->srv_resolver[i];
    }
    ac->resolver_size = 0;
    resolver_generate_conf(dst);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, json_object_get(dst, "resolver") != NULL);
    for (uint16_t i = 0; i < 255; ++i) {
        ac->srv_resolver[i] = old_srv_resolver[i];
    }
    ac->resolver_size = old_resolver_size;

    json_decref(dst);
}

void test_config_generators_more_edges()
{
    json_t *dst = json_object();

    /* puppeteer: valid JSON value and invalid JSON fallback */
    puppeteer_node pn = {0};
    pn.url = string_init_dupn("https://example.org", strlen("https://example.org"));
    pn.value = "{\"headers\":{\"X\":1}}";
    puppeteer_generate_conf(dst, &pn);
    json_t *puppeteer = json_object_get(dst, "puppeteer");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, puppeteer);
    json_t *u1 = json_object_get(puppeteer, "https://example.org");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, u1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(u1, "headers"));

    puppeteer_node pn2 = {0};
    pn2.url = string_init_dupn("https://fallback.local", strlen("https://fallback.local"));
    pn2.value = "{broken-json";
    puppeteer_generate_conf(dst, &pn2);
    json_t *u2 = json_object_get(puppeteer, "https://fallback.local");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, u2);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, json_object_size(u2));
    string_free(pn.url);
    string_free(pn2.url);

    /* modules: ignore entries without full key/path */
    module_t m1 = {0};
    m1.key = "ok";
    m1.path = "/tmp/ok.so";
    modules_generate_conf(dst, &m1);
    module_t m2 = {0};
    m2.key = "missing-path";
    modules_generate_conf(dst, &m2);
    module_t m3 = {0};
    m3.path = "/tmp/missing-key.so";
    modules_generate_conf(dst, &m3);
    json_t *modules = json_object_get(dst, "modules");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, modules);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_object_size(modules));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/ok.so", json_string_value(json_object_get(modules, "ok")));

    /* query field serializer helper */
    query_field qf = {0};
    qf.field = "latency_p99";
    json_t *field = json_array();
    query_node_generate_field_conf(field, &qf);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(field));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "latency_p99", json_string_value(json_array_get(field, 0)));
    json_decref(field);

    json_decref(dst);
}

void test_config_query_and_x509_branches()
{
    json_t *dst = json_object();

    /* qf_hash branch in query_node_generate_conf */
    alligator_ht *qh = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, qh);
    query_field *qf = calloc(1, sizeof(*qf));
    qf->field = "status";
    alligator_ht_insert(qh, &qf->node, qf, alligator_ht_strhash(qf->field, strlen(qf->field), 0));

    query_node qn = {0};
    qn.make = "with_fields";
    qn.qf_hash = qh;
    query_node_generate_conf(dst, &qn);

    json_t *query = json_object_get(dst, "query");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, query);
    json_t *q0 = json_array_get(query, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, q0);
    json_t *fields = json_object_get(q0, "field");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, fields);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(fields));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "status", json_string_value(json_array_get(fields, 0)));

    alligator_ht_done(qh);
    free(qh);
    free(qf);
    json_decref(dst);

    /* x509 match branch behavior */
    dst = json_object();
    x509_fs_t xfs = {0};
    xfs.path = "/x509/path";
    xfs.match = string_tokens_new();
    string_tokens_push_dupn(xfs.match, "crt", 3);
    fs_x509_generate_conf(dst, &xfs);
    json_t *x509 = json_object_get(dst, "x509");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, x509);
    json_t *x0 = json_array_get(x509, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, x0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(x0, "match"));
    string_tokens_free(xfs.match);
    json_decref(dst);
}

void test_config_generators_labels_env_branches()
{
    labels_container *lc = calloc(1, sizeof(*lc));
    lc->name = "role";
    lc->key = "backend";
    alligator_ht *labels = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, labels);
    alligator_ht_insert(labels, &lc->node, lc, alligator_ht_strhash(lc->name, strlen(lc->name), 0));

    alligator_ht *env = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, env);
    env_struct_push_alloc(env, "X-Env", "prod");

    json_t *dst = json_object();

    context_arg carg = {0};
    carg.url = "http://example.org/metrics";
    carg.labels = labels;
    carg.env = env;
    aggregator_generate_conf(dst, &carg);
    json_t *aggregate = json_object_get(dst, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    json_t *agg0 = json_array_get(aggregate, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, agg0);
    json_t *add_label = json_object_get(agg0, "add_label");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, add_label);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "backend", json_string_value(json_object_get(add_label, "role")));
    json_t *jenv = json_object_get(agg0, "env");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, jenv);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "prod", json_string_value(json_object_get(jenv, "X-Env")));

    action_node an = {0};
    an.name = "act";
    an.labels = labels;
    action_generate_conf(dst, &an);
    json_t *action = json_object_get(dst, "action");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, action);
    json_t *a0 = json_array_get(action, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, a0);
    add_label = json_object_get(a0, "add_label");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, add_label);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "backend", json_string_value(json_object_get(add_label, "role")));

    probe_node pn = {0};
    pn.name = "p";
    pn.method = HTTP_GET;
    pn.labels = labels;
    pn.env = env;
    probe_generate_conf(dst, &pn);
    json_t *probe = json_object_get(dst, "probe");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, probe);
    json_t *p0 = json_array_get(probe, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p0);
    add_label = json_object_get(p0, "add_label");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, add_label);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "backend", json_string_value(json_object_get(add_label, "role")));
    jenv = json_object_get(p0, "env");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, jenv);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "prod", json_string_value(json_object_get(jenv, "X-Env")));

    context_arg ep = {0};
    ep.key = "ep";
    ep.labels = labels;
    ep.env = env;
    entrypoints_generate_conf(dst, &ep);
    json_t *entrypoint = json_object_get(dst, "entrypoint");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, entrypoint);
    json_t *e0 = json_array_get(entrypoint, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, e0);
    add_label = json_object_get(e0, "add_label");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, add_label);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "backend", json_string_value(json_object_get(add_label, "role")));
    jenv = json_object_get(e0, "env");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, jenv);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "prod", json_string_value(json_object_get(jenv, "X-Env")));

    json_decref(dst);
    env_free(env);
    alligator_ht_done(labels);
    free(labels);
    free(lc);
}

void test_config_generators_iteration_paths()
{
    json_t *dst = json_object();

    /* query_generate_conf with populated hash -> iterates and serializes nodes */
    alligator_ht *qhash = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, qhash);
    query_node *qn1 = calloc(1, sizeof(*qn1));
    query_node *qn2 = calloc(1, sizeof(*qn2));
    qn1->make = "m1";
    qn1->datasource = "ds1";
    qn2->make = "m2";
    qn2->datasource = "ds2";
    alligator_ht_insert(qhash, &qn1->node, qn1, alligator_ht_strhash("m1", 2, 0));
    alligator_ht_insert(qhash, &qn2->node, qn2, alligator_ht_strhash("m2", 2, 0));

    query_ds qds = {0};
    qds.hash = qhash;
    query_generate_conf(dst, &qds);

    json_t *query = json_object_get(dst, "query");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, query);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(query));

    alligator_ht_done(qhash);
    free(qhash);
    free(qn1);
    free(qn2);
    json_decref(dst);

    /* resolver_generate_conf with two entries */
    resolver_data rd1 = {0}, rd2 = {0};
    host_aggregator_info *hi1 = calloc(1, sizeof(*hi1));
    host_aggregator_info *hi2 = calloc(1, sizeof(*hi2));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi1);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi2);
    hi1->host = strdup("1.1.1.1");
    hi1->url = strdup("udp://1.1.1.1:53");
    hi1->user = strdup("");
    hi1->transport_string = strdup("udp");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi1->host);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi1->url);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi1->user);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi1->transport_string);
    snprintf(hi1->port, sizeof(hi1->port), "%s", "53");
    rd1.hi = hi1;

    hi2->host = strdup("8.8.4.4");
    hi2->url = strdup("tcp://8.8.4.4:53");
    hi2->user = strdup("");
    hi2->transport_string = strdup("tcp");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi2->host);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi2->url);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi2->user);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi2->transport_string);
    snprintf(hi2->port, sizeof(hi2->port), "%s", "53");
    rd2.hi = hi2;

    resolver_data *old_srv_resolver[255] = {0};
    uint8_t old_resolver_size = ac->resolver_size;
    for (uint16_t i = 0; i < 255; ++i) {
        old_srv_resolver[i] = ac->srv_resolver[i];
    }
    ac->srv_resolver[0] = &rd1;
    ac->srv_resolver[1] = &rd2;
    ac->resolver_size = 2;

    dst = json_object();
    resolver_generate_conf(dst);
    json_t *resolver = json_object_get(dst, "resolver");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, resolver);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(resolver));
    json_decref(dst);

    for (uint16_t i = 0; i < 255; ++i) {
        ac->srv_resolver[i] = old_srv_resolver[i];
    }
    ac->resolver_size = old_resolver_size;
    free(hi1->host);
    free(hi1->url);
    free(hi1->user);
    free(hi1->transport_string);
    free(hi1);
    free(hi2->host);
    free(hi2->url);
    free(hi2->user);
    free(hi2->transport_string);
    free(hi2);

    /* puppeteer branch when value is NULL */
    dst = json_object();
    puppeteer_node pn = {0};
    pn.url = string_init_dupn("https://null-value.local", strlen("https://null-value.local"));
    pn.value = NULL;
    puppeteer_generate_conf(dst, &pn);
    json_t *puppeteer = json_object_get(dst, "puppeteer");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, puppeteer);
    json_t *node = json_object_get(puppeteer, "https://null-value.local");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, node);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, json_object_size(node));
    string_free(pn.url);
    json_decref(dst);
}

void test_config_aggregator_and_lang_extra_paths()
{
    json_t *dst = json_object();

    context_arg carg = {0};
    char dns_query[] = "example.org";
    carg.url = "https://user:pass@example.org/path";
    carg.tls_ca_file = "/tmp/ca.crt";
    carg.tls_cert_file = "/tmp/client.crt";
    carg.tls_key_file = "/tmp/client.key";
    carg.tls_server_name = "example.org";
    carg.parser_name = "dns";
    carg.parser_handler = dns_handler;
    carg.data = dns_query;
    carg.key = "agg-key";
    carg.threaded_loop_name = "loop-a";
    carg.log_level = 9999;
    aggregator_generate_conf(dst, &carg);

    json_t *aggregate = json_object_get(dst, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(aggregate));
    json_t *a0 = json_array_get(aggregate, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, a0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/ca.crt", json_string_value(json_object_get(a0, "tls_ca")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/client.crt", json_string_value(json_object_get(a0, "tls_certificate")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/client.key", json_string_value(json_object_get(a0, "tls_key")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "example.org", json_string_value(json_object_get(a0, "tls_server_name")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "dns", json_string_value(json_object_get(a0, "handler")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "example.org", json_string_value(json_object_get(a0, "resolve")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "agg-key", json_string_value(json_object_get(a0, "key")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "loop-a", json_string_value(json_object_get(a0, "threaded_loop_name")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9999, json_integer_value(json_object_get(a0, "log_level")));

    lang_options lo = {0};
    lo.key = "lang-log-level-int";
    lo.log_level = 9999;
    lo.serializer = METRIC_SERIALIZER_JSON;
    lang_generate_conf(dst, &lo);
    json_t *lang = json_object_get(dst, "lang");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, lang);
    json_t *l0 = json_array_get(lang, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, l0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9999, json_integer_value(json_object_get(l0, "log_level")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "json", json_string_value(json_object_get(l0, "serializer")));

    json_decref(dst);
}

void test_config_generators_additional_scalars()
{
    json_t *dst = json_object();

    cluster_server_oplog servers[2] = {{0}};
    servers[0].name = "node-1";
    servers[0].is_me = 1;
    servers[1].name = "node-2";
    servers[1].is_me = 0;
    cluster_node cn = {0};
    cn.name = "cluster-a";
    cn.servers = servers;
    cn.servers_size = 2;
    cn.replica_factor = 3;
    cluster_generate_conf(dst, &cn);
    json_t *cluster = json_object_get(dst, "cluster");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cluster);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, json_integer_value(json_object_get(cluster, "replica_factor")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_integer_value(json_object_get(cluster, "size")));
    json_t *srv = json_object_get(cluster, "servers");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, srv);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_integer_value(json_object_get(json_array_get(srv, 0), "is_me")));

    context_arg ep = {0};
    ep.ttl = 120;
    ep.pingloop = 2;
    ep.threads = 4;
    ep.api_enable = 1;
    ep.key = "entry-a";
    ep.cluster = "cluster-a";
    ep.instance = "inst-a";
    ep.lang = "ru";
    entrypoints_generate_conf(dst, &ep);
    json_t *entrypoint = json_object_get(dst, "entrypoint");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, entrypoint);
    json_t *e0 = json_array_get(entrypoint, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, e0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_integer_value(json_object_get(e0, "pingloop")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, json_integer_value(json_object_get(e0, "threads")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_integer_value(json_object_get(e0, "api")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "inst-a", json_string_value(json_object_get(e0, "instance")));

    probe_node pn = {0};
    char *statuses[] = {"200", "204", "301"};
    pn.name = "probe-scalar";
    pn.method = HTTP_GET;
    pn.follow_redirects = 1;
    pn.tls_verify = 1;
    pn.timeout = 9000;
    pn.loop = 5;
    pn.percent_success = 97.5;
    pn.valid_status_codes = statuses;
    pn.valid_status_codes_size = 3;
    probe_generate_conf(dst, &pn);
    json_t *probe = json_object_get(dst, "probe");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, probe);
    json_t *p0 = json_array_get(probe, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "GET", json_string_value(json_object_get(p0, "method")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_integer_value(json_object_get(p0, "follow_redirects")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_integer_value(json_object_get(p0, "tls_verify")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9000, json_integer_value(json_object_get(p0, "timeout")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 5, json_integer_value(json_object_get(p0, "loop")));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, json_array_size(json_object_get(p0, "valid_status_codes")));

    resolver_data rd = {0};
    host_aggregator_info *hi = calloc(1, sizeof(*hi));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi);
    hi->host = strdup("9.9.9.9");
    hi->url = strdup("udp://9.9.9.9:5353");
    hi->user = strdup("u");
    hi->transport_string = strdup("udp");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi->host);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi->url);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi->user);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, hi->transport_string);
    snprintf(hi->port, sizeof(hi->port), "%s", "5353");
    rd.hi = hi;

    resolver_data *old_srv_resolver[255] = {0};
    uint8_t old_resolver_size = ac->resolver_size;
    for (uint16_t i = 0; i < 255; ++i) {
        old_srv_resolver[i] = ac->srv_resolver[i];
    }
    ac->srv_resolver[0] = &rd;
    ac->resolver_size = 1;

    resolver_generate_conf(dst);
    json_t *resolver = json_object_get(dst, "resolver");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, resolver);
    json_t *r0 = json_array_get(resolver, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, r0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "5353", json_string_value(json_object_get(r0, "port")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "udp://9.9.9.9:5353", json_string_value(json_object_get(r0, "url")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "u", json_string_value(json_object_get(r0, "user")));

    for (uint16_t i = 0; i < 255; ++i) {
        ac->srv_resolver[i] = old_srv_resolver[i];
    }
    ac->resolver_size = old_resolver_size;
    free(hi->host);
    free(hi->url);
    free(hi->user);
    free(hi->transport_string);
    free(hi);
    json_decref(dst);
}

void test_match_rules_regex_paths()
{
    match_rules *mr = calloc(1, sizeof(*mr));
    mr->hash = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mr->hash);

    match_push(mr, "/foo.*/", strlen("/foo.*/"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, mr->head != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, match_mapper(mr, "foobar", strlen("foobar"), "n"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, match_mapper(mr, "bar", strlen("bar"), "n"));

    /* broken regex should not crash and should not replace existing regex chain */
    match_push(mr, "/[/", strlen("/[/"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, mr->head != NULL);

    match_free(mr);
}
