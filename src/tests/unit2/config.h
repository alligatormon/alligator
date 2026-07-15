#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include "config/get.h"
#include "config/plain.h"
#include "common/url.h"
#include "common/selector.h"
#include "common/base64.h"
#include "common/auth.h"
#include "common/http.h"
#include "common/logs.h"
#include "parsers/multiparser.h"
#include "common/log_tcp.h"
#include "common/log_http.h"
#include "common/log_kafka.h"
#include "common/log_elastic.h"
#include "common/log_json.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/entrypoint.h"
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
#include "scheduler/type.h"
#include "metric/namespace.h"
#include "x509/type.h"
#include "cluster/type.h"
#include "puppeteer/puppeteer.h"
#include "chromecdp/chromecdp.h"
#include "grok/type.h"
#include "amtail/type.h"
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
void chromecdp_root_generate_conf(json_t *dst);
void chromecdp_generate_conf(void *funcarg, void* arg);
void amtail_generate_conf(void *funcarg, void* arg);
void grok_generate_conf(void *funcarg, void* arg);
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

    log_channel *agg = log_channel_upsert("aggregate", "file:///tmp/alligator-ut2-aggregate.log", -1, -1, NULL, -1, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, agg);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "aggregate", agg->name);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, agg->socket >= 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, log_channel_get("aggregate"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_channel_get("missing") == log_channel_default());

    log_channel *tcp_ch = log_channel_upsert("tcp-test", "tcp://127.0.0.1:59999", -1, -1, NULL, -1, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, tcp_ch);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_channel_is_tcp(tcp_ch));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1,
        log_tcp_sink_write(tcp_ch, "dropped\n", 8));
    log_tcp_sink_close(tcp_ch);

    log_channel *elastic_ch = log_channel_upsert("elastic-tcp", "tcp://127.0.0.1:59999", -1, -1, NULL,
        LOG_FORMAT_ELASTIC, "alligator-test");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, elastic_ch);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_channel_format_elastic(elastic_ch));
    {
        char *doc;
        size_t doclen = 0;
        doc = log_elastic_format_doc_msg(elastic_ch, NULL, L_INFO, "hello\n", 6, &doclen);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, doc);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, doclen > 0);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(doc, "\"message\":\"hello\\n\"") != NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(doc, "\"@timestamp\"") != NULL);
        free(doc);
    }
    log_tcp_sink_close(elastic_ch);

    log_channel *http_ch = log_channel_upsert("elastic-http", "http://127.0.0.1:9200/alligator/_bulk", -1, -1, NULL, -1, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, http_ch);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_channel_is_http(http_ch));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_channel_format_elastic(http_ch));
    log_http_sink_close(http_ch);

    log_channel *kafka_ch = log_channel_upsert("kafka-test", "kafka://127.0.0.1:9092/alligator-logs?key=test-host", -1, -1, NULL,
        LOG_FORMAT_JSON, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, kafka_ch);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_channel_is_kafka(kafka_ch));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_channel_format_json(kafka_ch));
    {
        json_t *kopts = json_object();
        json_object_set_new(kopts, "linger.ms", json_integer(10));
        json_object_set_new(kopts, "compression.type", json_string("lz4"));
        log_channel_set_kafka(kafka_ch, "test-host-override", kopts);
        json_decref(kopts);
        log_channel_open(kafka_ch);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_channel_is_kafka(kafka_ch));
    }
    {
        size_t i;
        char msg[64];
        for (i = 0; i < 20000; i++) {
            snprintf(msg, sizeof(msg), "kafka flood %zu\n", i);
            log_kafka_sink_write(kafka_ch, msg, strlen(msg));
        }
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_kafka_sink_dropped(kafka_ch) > 0);
    }
    log_kafka_sink_close(kafka_ch);

    log_channel *json_ch = log_channel_upsert("json-tcp", "tcp://127.0.0.1:59999", -1, 1, NULL,
        LOG_FORMAT_JSON, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_ch);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_channel_format_json(json_ch));
    {
        context_arg json_carg = {0};
        char *doc;
        size_t doclen = 0;
        strlcpy(json_carg.host, "127.0.0.1", sizeof(json_carg.host));
        json_carg.key = "test-key";
        doc = log_json_format_doc_msg(json_ch, &json_carg, "hello\n", 6, &doclen);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, doc);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, doclen > 0);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(doc, "\"message\":\"hello\"") != NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(doc, "\"key\":\"test-key\"") != NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(doc, "\"date\":") != NULL);
        free(doc);
    }
    log_tcp_sink_close(json_ch);

    {
        const char *raw_plain_path = "/tmp/alligator-ut2-raw-plain.log";
        const char *raw_json_path = "/tmp/alligator-ut2-raw-json.log";
        unlink(raw_plain_path);
        unlink(raw_json_path);

        log_channel *raw_plain = log_channel_upsert("raw-plain", "file:///tmp/alligator-ut2-raw-plain.log",
            -1, 0, NULL, LOG_FORMAT_PLAIN, NULL);
        log_channel *raw_json = log_channel_upsert("raw-json", "file:///tmp/alligator-ut2-raw-json.log",
            -1, 0, NULL, LOG_FORMAT_JSON, NULL);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, raw_plain);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, raw_json);

        context_arg file_carg = {0};
        file_carg.transport = APROTO_FILE;
        file_carg.key = "file://test";
        file_carg.log_ch_raw = raw_plain;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, context_allows_raw_log(&file_carg));
        log_channel_write_raw(raw_plain, &file_carg, "raw line\n", 9);

        file_carg.log_ch_raw = raw_json;
        log_channel_write_raw(raw_json, &file_carg, "json line\n", 10);

        context_arg http_carg = {0};
        http_carg.transport = APROTO_HTTP;
        http_carg.log_ch_raw = raw_plain;
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, context_allows_raw_log(&http_carg));
        carglog_raw(&http_carg, "skipped\n", 8);

        if (raw_plain->socket > 2)
            close(raw_plain->socket);
        if (raw_json->socket > 2)
            close(raw_json->socket);

        FILE *rf = fopen(raw_plain_path, "r");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, rf);
        char rbuf[32] = {0};
        size_t rn = fread(rbuf, 1, sizeof(rbuf) - 1, rf);
        fclose(rf);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9, (int)rn);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "raw line\n", rbuf);

        rf = fopen(raw_json_path, "r");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, rf);
        char jbuf[256] = {0};
        rn = fread(jbuf, 1, sizeof(jbuf) - 1, rf);
        fclose(rf);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, rn > 0);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(jbuf, "\"message\":\"json line\"") != NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(jbuf, "\"key\":\"file://test\"") != NULL);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(jbuf, "\"date\":") != NULL);

        {
            const char *raw_lines_path = "/tmp/alligator-ut2-raw-lines.log";
            log_channel *raw_lines;

            unlink(raw_lines_path);
            raw_lines = log_channel_upsert("raw-lines", "file:///tmp/alligator-ut2-raw-lines.log",
                -1, 0, NULL, LOG_FORMAT_JSON, NULL);
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, raw_lines);

            memset(&file_carg, 0, sizeof(file_carg));
            file_carg.transport = APROTO_FILE;
            file_carg.key = "file://lines";
            file_carg.log_ch_raw = raw_lines;

            carglog_raw(&file_carg, "line1\nline2\npar", 15);
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, file_carg.log_ch_raw_tail);
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, (int)file_carg.log_ch_raw_tail->l);

            carglog_raw(&file_carg, "tial\n", 5);
            assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, file_carg.log_ch_raw_tail);

            if (raw_lines->socket > 2)
                close(raw_lines->socket);

            rf = fopen(raw_lines_path, "r");
            assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, rf);
            memset(jbuf, 0, sizeof(jbuf));
            rn = fread(jbuf, 1, sizeof(jbuf) - 1, rf);
            fclose(rf);
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, rn > 0);
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(jbuf, "\"message\":\"line1\"") != NULL);
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(jbuf, "\"message\":\"line2\"") != NULL);
            assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(jbuf, "\"message\":\"partial\"") != NULL);
            unlink(raw_lines_path);
        }

        unlink(raw_plain_path);
        unlink(raw_json_path);
    }

    {
        const char *log_handler_path = "/tmp/alligator-ut2-log-handler.log";
        unlink(log_handler_path);
        log_channel *log_ch = log_channel_upsert("log-handler", "file:///tmp/alligator-ut2-log-handler.log",
            -1, 0, NULL, LOG_FORMAT_PLAIN, NULL);
        context_arg log_carg = {0};
        log_carg.transport = APROTO_FILE;
        log_carg.log_ch_raw = log_ch;
        alligator_multiparser("syslog line\n", 12, log_handler, NULL, &log_carg);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, log_carg.parser_status);
        if (log_ch && log_ch->socket > 2)
            close(log_ch->socket);
        FILE *lf = fopen(log_handler_path, "r");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, lf);
        char lbuf[32] = {0};
        size_t ln = fread(lbuf, 1, sizeof(lbuf) - 1, lf);
        fclose(lf);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 11, (int)ln);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "syslog line", lbuf);
        unlink(log_handler_path);
    }

    context_arg carg = {0};
    strlcpy(carg.host, "127.0.0.1", sizeof(carg.host));
    carg.key = "test-key";
    carg.log_ch = agg;
    carg.log_level = L_INFO;
    ac->log_level = L_INFO;
    carglog(&carg, L_INFO, "ut2 channel log\n");
    if (agg->socket > 2) {
        close(agg->socket);
        agg->socket = fileno(stdout);
    }
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
    ac->log_level = L_OFF;
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
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, jlog == NULL);

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
    uint64_t enclen = urlencode(encbuf, 128, "a b+c/=", strlen("a b+c/="));
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

void test_entrypoint_key_and_parse_add_label()
{
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        entrypoint_key_match_transport("tcp:0:0.0.0.0:19101", "tcp", "0.0.0.0", 19101));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        entrypoint_key_match_transport("tcp:3:127.0.0.1:8080", "tcp", "127.0.0.1", 8080));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        entrypoint_key_match_transport("tcp:0:0.0.0.0:19101", "tcp", "127.0.0.1", 19101));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        entrypoint_key_match_transport("udp:2:10.0.0.1:53", "udp", "10.0.0.1", 53));

    context_arg *carg = calloc(1, sizeof(*carg));
    json_t *root = json_object();
    parse_add_label(carg, root);
    assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, carg->labels);
    json_decref(root);
    free(carg);
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

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, getrtime_sec_float(t2, t1) > 0.9);
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

void test_metricstransform_plain_and_ingest()
{
    /* One plain-to-JSON run per top-level block: a single multi-block string can leave the
     * tokenizer index misaligned between contexts, so metricstransform would be missing on later blocks. */
    const char *frag_ep =
        "entrypoint { tcp 19101; metricstransform { include ut_metric_transform_total match_type strict label source regex '^https?://(example\\.org).*$' replacement '$1'; }; }\n";
    const char *frag_agg =
        "aggregate { json http://127.0.0.1:18080/metrics metricstransform { include ut_metric_transform_total match_type strict label source regex '^https?://(example\\.org).*$' replacement '$1'; }; }\n";
    const char *frag_act =
        "action { name t1 expr exec://true metricstransform { include ut_metric_transform_total match_type strict label source regex '^https?://(example\\.org).*$' replacement '$1'; }; }\n";
    const char *frag_pup =
        "puppeteer { https://example.org metricstransform { include puppeteer_eventSourceResponseStatus match_type strict label source regex '^https?://(example\\.org).*$' replacement '$1'; }; }\n";
    const char *fragments[4] = { frag_ep, frag_agg, frag_act, frag_pup };
    const char *keys[4] = { "entrypoint", "aggregate", "action", "puppeteer" };

    json_error_t error;
    json_t *root = json_object();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    for (int f = 0; f < 4; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, keys[f]);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        json_object_set(root, keys[f], child);
        json_decref(part);
    }

    json_t *entrypoint = json_object_get(root, "entrypoint");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, entrypoint);
    json_t *ep0 = json_array_get(entrypoint, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(ep0, "metricstransform"));

    json_t *aggregate = json_object_get(root, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    json_t *agg0 = json_array_get(aggregate, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(agg0, "metricstransform"));

    json_t *action = json_object_get(root, "action");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, action);
    json_t *act0 = json_array_get(action, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(act0, "metricstransform"));

    json_t *puppeteer = json_object_get(root, "puppeteer");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, puppeteer);
    json_t *pup0 = json_object_get(puppeteer, "https://example.org");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(pup0, "metricstransform"));

    json_t *mtx_ctxs[4] = { ep0, agg0, act0, pup0 };
    for (int c = 0; c < 4; c++) {
        json_t *mtx = json_object_get(mtx_ctxs[c], "metricstransform");
        json_t *txf0 = json_array_get(json_object_get(mtx, "transforms"), 0);
        json_t *op0 = json_array_get(json_object_get(txf0, "operations"), 0);
        json_t *va0 = json_array_get(json_object_get(op0, "value_actions"), 0);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(va0, "replacement"));
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "$1", json_string_value(json_object_get(va0, "replacement")));
    }

    {
        const char *frag_repl_active =
            "action { name regexpr expr exec://true metricstransform { include ci12312312.memory_usage_cgroup match_type regexp label type regex '^total$' replacement 'active'; }; }\n";
        string *sx = string_new();
        string_cat(sx, (char *)frag_repl_active, strlen(frag_repl_active));
        char *jsx = config_plain_to_json(sx);
        string_free(sx);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, jsx);
        json_t *partx = json_loads(jsx, 0, &error);
        free(jsx);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, partx);
        json_t *ax = json_object_get(partx, "action");
        json_t *ax0 = json_array_get(ax, 0);
        json_t *mtxx = json_object_get(ax0, "metricstransform");
        json_t *tx0 = json_array_get(json_object_get(mtxx, "transforms"), 0);
        json_t *opx = json_array_get(json_object_get(tx0, "operations"), 0);
        json_t *vax = json_array_get(json_object_get(opx, "value_actions"), 0);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "active", json_string_value(json_object_get(vax, "replacement")));
        json_decref(partx);
    }

    {
        const char *frag_nl =
            "action { name nl1 expr exec://true metricstransform { include ut_metric_transform_total match_type strict label source new_label src_norm; }; }\n";
        string *sn = string_new();
        string_cat(sn, (char *)frag_nl, strlen(frag_nl));
        char *jsn = config_plain_to_json(sn);
        string_free(sn);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, jsn);
        json_t *partn = json_loads(jsn, 0, &error);
        free(jsn);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, partn);
        json_t *an0 = json_array_get(json_object_get(partn, "action"), 0);
        json_t *mtn = json_object_get(an0, "metricstransform");
        json_t *txn = json_array_get(json_object_get(mtn, "transforms"), 0);
        json_t *opn = json_array_get(json_object_get(txn, "operations"), 0);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "src_norm", json_string_value(json_object_get(opn, "new_label")));
        char *nk = metric_transform_label_key(
            "ut_metric_transform_total", NULL, "source", "v1", mtn, NULL, NULL);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, nk);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "src_norm", nk);
        free(nk);
        json_decref(partn);
    }

    context_arg carg = {0};
    carg.metricstransform = json_incref(json_object_get(ep0, "metricstransform"));
    carg.namespace = strdup("ut_metricstransform_plain_ns");
    carg.namespace_allocated = 1;
    insert_namespace(carg.namespace, 0);

    int64_t val = 1;
    metric_add_labels("ut_metric_transform_total", &val, DATATYPE_INT, &carg, "source", "https://example.org/path?id=11");

    metric_query_context *mqc = promql_parser(NULL, "ut_metric_transform_total", strlen("ut_metric_transform_total"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mqc);
    string *body = metric_query_deserialize(1024, mqc, METRIC_SERIALIZER_JSON, 0, carg.namespace, NULL, NULL, NULL, NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, body);

    json_t *metrics = json_loads(body->s, 0, &error);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, metrics);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(metrics) > 0);
    json_t *m0 = json_array_get(metrics, 0);
    json_t *labels = json_object_get(m0, "labels");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, labels);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "example.org", json_string_value(json_object_get(labels, "source")));

    json_decref(metrics);
    string_free(body);
    query_context_free(mqc);

    free(carg.namespace);
    json_decref(carg.metricstransform);
    json_decref(root);
}

void test_aggregate_multi_block_plain_parse()
{
    const char *conf =
        "aggregate {\n"
        "  dns udp://9.9.9.9:5353 resolve=yahoo.com type=a add_label=check:dns bind_address=1112;\n"
        "  dns udp://9.9.9.9:5353 resolve=yahoo.com type=a add_label=check:dns bind_address=1113;\n"
        "  dns udp://9.9.9.9:5353 resolve=yahoo.com type=a add_label=check:dns bind_address=1114;\n"
        "  wazuh file:///var/ossec/var/run/wazuh-agentd.state;\n"
        "}\n"
        "aggregate {\n"
        "  rabbitmq http://user:pass@127.0.0.1:8080;\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    json_t *aggregate = json_object_get(root, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 5, json_array_size(aggregate));

    json_t *a0 = json_array_get(aggregate, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "dns", json_string_value(json_object_get(a0, "handler")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "udp://9.9.9.9:5353", json_string_value(json_object_get(a0, "url")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "yahoo.com", json_string_value(json_object_get(a0, "resolve")));

    json_t *a4 = json_array_get(aggregate, 4);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "rabbitmq", json_string_value(json_object_get(a4, "handler")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "http://user:pass@127.0.0.1:8080", json_string_value(json_object_get(a4, "url")));

    json_decref(root);
}

void test_entrypoint_log_channel_plain_parse()
{
    const char *conf =
        "log_channel {\n"
        "  name entrypoint-tcp;\n"
        "  dest file:///var/log/alligator-entrypoint.log;\n"
        "}\n"
        "entrypoint {\n"
        "  log_level trace;\n"
        "  log_channel entrypoint-tcp;\n"
        "  handler prometheus;\n"
        "  allow 127.0.0.1;\n"
        "  allow 10.99.10.0/24;\n"
        "  tcp 1111;\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    json_t *entrypoint = json_object_get(root, "entrypoint");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, entrypoint);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(entrypoint));

    json_t *ep0 = json_array_get(entrypoint, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ep0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "trace", json_string_value(json_object_get(ep0, "log_level")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "entrypoint-tcp", json_string_value(json_object_get(ep0, "log_channel")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "prometheus", json_string_value(json_object_get(ep0, "handler")));

    json_t *allow = json_object_get(ep0, "allow");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, allow);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(allow));

    json_t *tcp = json_object_get(ep0, "tcp");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, tcp);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(tcp));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "1111", json_string_value(json_array_get(tcp, 0)));

    json_t *log_channels = json_object_get(root, "log_channel");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, log_channels);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(log_channels));
    json_t *lc0 = json_array_get(log_channels, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "entrypoint-tcp", json_string_value(json_object_get(lc0, "name")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "file:///var/log/alligator-entrypoint.log", json_string_value(json_object_get(lc0, "dest")));

    json_decref(root);
}

void test_log_channel_raw_plain_parse()
{
    const char *conf =
        "log_channel {\n"
        "  name kafka-raw;\n"
        "  dest kafka://127.0.0.1:9092/alligator-raw;\n"
        "}\n"
        "entrypoint {\n"
        "  handler log;\n"
        "  tcp 1514;\n"
        "  log_channel_raw kafka-raw;\n"
        "}\n"
        "aggregate {\n"
        "  log \"file:///var/log/app.log\" log_channel_raw=kafka-raw;\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    json_t *entrypoint = json_object_get(root, "entrypoint");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, entrypoint);
    json_t *ep0 = json_array_get(entrypoint, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "kafka-raw",
        json_string_value(json_object_get(ep0, "log_channel_raw")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "log",
        json_string_value(json_object_get(ep0, "handler")));

    json_t *aggregate = json_object_get(root, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    json_t *ag0 = json_array_get(aggregate, 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "kafka-raw",
        json_string_value(json_object_get(ag0, "log_channel_raw")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "log",
        json_string_value(json_object_get(ag0, "handler")));

    json_decref(root);
}

void test_puppeteer_option_kv_plain_parse()
{
    const char *conf =
        "puppeteer {\n"
        "  https://example.org;\n"
        "  headers=Authorization:Bearer abc;\n"
        "  env=NODE_ENV:prod;\n"
        "  add_label=team:qa;\n"
        "  screenshot=fullPage:true;\n"
        "  screenshot=minimum_code:207;\n"
        "  metricstransform={\"include\":\"puppeteer_eventSourceResponseStatus\",\"match_type\":\"strict\",\"transforms\":[{\"label\":\"source\",\"operations\":[{\"type\":\"replace_all\",\"value_actions\":[{\"regex\":\"^https?://(example\\\\.org).*$\",\"replacement\":\"$1\"}]}]}]};\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    json_t *puppeteer = json_object_get(root, "puppeteer");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, puppeteer);
    json_t *p0 = json_object_get(puppeteer, "https://example.org");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, p0);

    json_t *headers = json_object_get(p0, "headers");
    json_t *env = json_object_get(p0, "env");
    json_t *add_label = json_object_get(p0, "add_label");
    json_t *screenshot = json_object_get(p0, "screenshot");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, headers);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, env);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, add_label);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, screenshot);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_object_size(headers) > 0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "prod",
        json_string_value(json_object_get(env, "NODE_ENV")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "qa",
        json_string_value(json_object_get(add_label, "team")));
    json_t *full_page = json_object_get(screenshot, "fullPage");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, full_page);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        json_is_true(full_page) || (json_is_string(full_page) && !strcmp("true", json_string_value(full_page))));

    json_t *min_code = json_object_get(screenshot, "minimum_code");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, min_code);
    if (json_is_integer(min_code))
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 207, json_integer_value(min_code));
    else
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "207", json_string_value(min_code));

    json_decref(root);
}

void test_entrypoint_plain_rich_parse()
{
    const char *conf =
        "entrypoint {\n"
        "  log_level debug;\n"
        "  handler prometheus;\n"
        "  deny 192.168.1.0/24;\n"
        "  allow 10.0.0.0/8;\n"
        "  allow 127.0.0.1;\n"
        "  tcp 8080;\n"
        "  udp 514;\n"
        "  tls 8443;\n"
        "  unix /tmp/alligator.sock;\n"
        "  unixgram /tmp/alligator.gram;\n"
        "  namespace ut_ep_rich;\n"
        "  ttl 30s;\n"
        "  read_metric_interval 5s;\n"
        "  mtail_full_export_interval 60s;\n"
        "  cluster ut-cluster;\n"
        "  instance host1;\n"
        "  threads 2;\n"
        "  metric_aggregation true;\n"
        "  key ep-main;\n"
        "  auth_header Authorization;\n"
        "  return 403;\n"
        "  mapping path1;\n"
        "  api v1;\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    json_t *entrypoint = json_object_get(root, "entrypoint");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, entrypoint);
    json_t *ep0 = json_array_get(entrypoint, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ep0);

    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "debug",
        json_string_value(json_object_get(ep0, "log_level")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "prometheus",
        json_string_value(json_object_get(ep0, "handler")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "ut_ep_rich",
        json_string_value(json_object_get(ep0, "namespace")));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "ep-main",
        json_string_value(json_object_get(ep0, "key")));

    json_t *allow = json_object_get(ep0, "allow");
    json_t *deny = json_object_get(ep0, "deny");
    json_t *tcp = json_object_get(ep0, "tcp");
    json_t *udp = json_object_get(ep0, "udp");
    json_t *tls = json_object_get(ep0, "tls");
    json_t *unix_ep = json_object_get(ep0, "unix");
    json_t *unixgram = json_object_get(ep0, "unixgram");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, allow);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, deny);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, tcp);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, udp);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, tls);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, unix_ep);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, unixgram);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(allow));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(deny));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "8080",
        json_string_value(json_array_get(tcp, 0)));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "514",
        json_string_value(json_array_get(udp, 0)));
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "8443",
        json_string_value(json_array_get(tls, 0)));

    json_decref(root);
}

void test_config_plain_top_level_blocks()
{
    const char *fragments[] = {
        "resolver { udp://8.8.8.8:53; udp://9.9.9.9:53; }\n",
        "scheduler { name ut_tick action ut_action expr count(up) period 30s datasource internal; }\n",
        "action { name ut_plain_action expr http://127.0.0.1:18080 serializer json dry_run; }\n",
        "lang { key ut_lang expr http://127.0.0.1:18080 lang duktape method main; }\n",
        "query { make socket_match expr process_match datasource internal action ut_action; }\n",
        "system { sysctl vm.overcommit_memory firewall; }\n",
        "x509 { name ut-x509 path /tmp/certs match .pem password secret type pem; }\n",
        "modules { jvm /usr/lib/libjvm.so; }\n"
    };
    const char *keys[] = { "resolver", "scheduler", "action", "lang", "query", "system", "x509", "modules" };

    json_error_t error;
    for (int f = 0; f < 8; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, keys[f]);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
            json_is_array(child) ? json_array_size(child) > 0 : json_object_size(child) > 0);
        json_decref(part);
    }
}

void test_config_plain_globals_and_channels()
{
    const char *conf =
        "log_level 4;\n"
        "grok_patterns /patterns/a.grok /patterns/b.grok;\n"
        "log_channel {\n"
        "  name global-ch;\n"
        "  dest file:///var/log/alligator-global.log;\n"
        "}\n"
        "system {\n"
        "  base;\n"
        "  network;\n"
        "  log_level info;\n"
        "  log_channel global-ch;\n"
        "  firewall;\n"
        "  cadvisor add_label=team:infra;\n"
        "}\n"
        "aggregate {\n"
        "  json http://127.0.0.1:9100/metrics log_level=debug log_channel=global-ch;\n"
        "  redis tcp://127.0.0.1:6379 log_level=info;\n"
        "  blackbox tcp://127.0.0.1:80;\n"
        "  log \"/var/log/messages\";\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, json_integer_value(json_object_get(root, "log_level")));
    json_t *grok = json_object_get(root, "grok_patterns");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, grok);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(grok));

    json_t *system = json_object_get(root, "system");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, system);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "global-ch",
        json_string_value(json_object_get(system, "log_channel")));

    json_t *aggregate = json_object_get(root, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, json_array_size(aggregate));

    json_decref(root);
}

void test_config_plain_persistence_block()
{
    const char *conf = "persistence { directory /tmp/alligator-ut; }\n";
    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    json_t *persistence = json_object_get(root, "persistence");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, persistence);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/alligator-ut",
        json_string_value(json_object_get(persistence, "directory")));
    json_decref(root);
}

void test_config_plain_more_top_level_blocks()
{
    const char *fragments[] = {
        "cluster { name ut-cluster size 1000 replica_factor 2 sharding_key __name__ servers localhost:1111 localhost:1112; }\n",
        "probe { name ut-icmp prober icmp timeout 5000 loop 10 percent 0.5; }\n",
        "namespace { name ut-ns-plain max_emit 3; }\n",
        "threaded_loop { name ut-loop threads 4; }\n",
        "instance { name ut-instance-host; }\n"
    };
    const char *keys[] = { "cluster", "probe", "namespace", "threaded_loop", "instance" };

    json_error_t error;
    for (int f = 0; f < 5; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, keys[f]);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
            json_is_array(child) ? json_array_size(child) > 0 : json_object_size(child) > 0);
        json_decref(part);
    }
}

void test_config_plain_grok_mtail_chromecdp_blocks()
{
    const char *fragments[] = {
        "grok { key dmesg; name dmesg_event; match '%{TIMESTAMP}] %{WORD:process}'; "
        "counter dmesg_counter process; quantiles dmesg_q response_time 0.99 0.5; "
        "splited_tags \", \" upstream_status upstream_addr; splited_inherit_tag server_name; }\n",
        "mtail { name nginx_mtail; script /etc/alligator/mtail/nginx.mtail; log_level_vm debug; }\n",
        "chromecdp { executable /usr/bin/chromium-browser; port 9222; https://example.com; "
        "log_level debug; timeout 30s; }\n",
        "aggregate { grok file:///var/log/nginx-access.log name=nginx log_level=off; "
        "blackbox https://example.com threaded_loop_name=small; }\n"
    };
    const char *keys[] = { "grok", "mtail", "chromecdp", "aggregate" };

    json_error_t error;
    for (int f = 0; f < 4; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, keys[f]);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
            json_is_array(child) ? json_array_size(child) > 0 : json_object_size(child) > 0);
        json_decref(part);
    }
}

void test_config_plain_aggregate_rich_variants()
{
    const char *conf =
        "aggregate {\n"
        "  snmp udp://public@192.0.2.1:161/1.3.6.1.2.1.1.3.0;\n"
        "  beanstalkd tcp://127.0.0.1:11300;\n"
        "  consul-configuration http://127.0.0.1:8500;\n"
        "  consul-discovery http://127.0.0.1:8500;\n"
        "  parser http://127.0.0.1:9100/metrics;\n"
        "  redis tcp://127.0.0.1:6379 add_label=team:core tls_certificate=/tmp/c.crt tls_key=/tmp/c.key tls_ca=/tmp/ca.crt;\n"
        "  https https://example.com follow_redirects=5;\n"
        "  mongodb mongodb://127.0.0.1:27017;\n"
        "  mtail file:///var/log/syslog name=syslog_mtail;\n"
        "  blackbox https://example.org period=30s;\n"
        "  json http://127.0.0.1:9200/_nodes/stats;\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    json_t *aggregate = json_object_get(root, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(aggregate) >= 8);
    json_decref(root);
}

void test_config_plain_mega_document()
{
    const char *conf =
        "log_level 3;\n"
        "grok_patterns /patterns/a.grok /patterns/b.grok;\n"
        "log_channel { name mega-ch; dest file:///var/log/mega.log; }\n"
        "persistence { directory /var/lib/alligator; }\n"
        "ttl 120;\n"
        "cluster { name mega-cluster size 10 replica_factor 2 sharding_key __name__ servers h1:1111 h2:1112; }\n"
        "instance { name mega-inst; }\n"
        "namespace { name mega-ns max_emit 0; }\n"
        "threaded_loop { name mega-loop threads 6; }\n"
        "resolver { udp://8.8.8.8:53; tcp://9.9.9.9:53; }\n"
        "scheduler { name mega_sched action mega_act expr count(x) period 60s datasource internal; }\n"
        "action { name mega_act expr http://127.0.0.1:8080 serializer openmetrics dry_run add_label=team:mega; }\n"
        "lang { key mega_lang expr http://127.0.0.1:8080/lang lang duktape method run; }\n"
        "query { make mega_q expr process_match datasource internal action mega_act; }\n"
        "probe { name mega_probe prober http valid_status_codes 2xx tls on; }\n"
        "system { base network firewall cadvisor add_label=host:mega; }\n"
        "x509 { name mega-x509 path /tmp/certs match .pem password x type pem; }\n"
        "modules { jvm /usr/lib/libjvm.so; }\n"
        "grok { key mega_grok; name mega_grok_name; match '%{WORD:w}'; counter mega_c w; }\n"
        "mtail { name mega_mtail script /tmp/mega.mtail log_level_vm debug; }\n"
        "chromecdp { executable /usr/bin/chromium port 9223 https://example.com timeout 30s; }\n"
        "puppeteer { https://example.org headers=Auth:tok env=K:V add_label=l:v; }\n"
        "entrypoint {\n"
        "  handler prometheus;\n"
        "  tcp 8080 udp 514 tls 8443;\n"
        "  unix /tmp/mega.sock unixgram /tmp/mega.g;\n"
        "  namespace mega-ns cluster mega-cluster instance mega-inst;\n"
        "  allow 10.0.0.0/8 deny 192.168.0.0/16;\n"
        "  mapping path1 api v1;\n"
        "  log_level debug log_channel mega-ch;\n"
        "  ttl 30s threads 4 metric_aggregation true key mega-ep;\n"
        "}\n"
        "aggregate {\n"
        "  json http://127.0.0.1:9100/metrics log_level=info log_channel=mega-ch;\n"
        "  redis tcp://127.0.0.1:6379 add_label=db:redis;\n"
        "  snmp udp://public@127.0.0.1:161/1.3.6.1.2.1.1.1.0;\n"
        "  parser http://127.0.0.1:8080/status;\n"
        "  https https://example.com follow_redirects=3;\n"
        "  blackbox https://google.com period=60s;\n"
        "  log \"/var/log/syslog\" log_channel_raw=mega-ch;\n"
        "  grok file:///var/log/nginx.log name=nginx;\n"
        "  mtail file:///var/log/app.log name=mega_mtail;\n"
        "  beanstalkd tcp://127.0.0.1:11300;\n"
        "  mongodb mongodb://127.0.0.1:27017;\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, json_integer_value(json_object_get(root, "log_level")));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "entrypoint"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "aggregate"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "grok"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "mtail"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "chromecdp"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "puppeteer"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "cluster"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        json_array_size(json_object_get(root, "aggregate")) >= 8);
    json_decref(root);
}

void test_config_plain_probe_scheduler_variants()
{
    const char *fragments[] = {
        "probe { name ut_probe_http prober http valid_status_codes 2xx tls on timeout 5s; }\n",
        "probe { name ut_probe_tcp prober tcp valid_status_codes 200,404; }\n",
        "scheduler { name ut_sched_probe action ut_action expr count(up) period 15s datasource internal; }\n",
        "query { make ut_q_match expr process_match datasource internal action ut_action; }\n"
    };
    const char *keys[] = { "probe", "probe", "scheduler", "query" };

    json_error_t error;
    for (int f = 0; f < 4; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, keys[f]);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(child) > 0);
        json_decref(part);
    }
}

void test_config_plain_log_scalars_block()
{
    const char *conf =
        "log_level debug;\n"
        "log_dest stderr;\n"
        "log_form json;\n"
        "log_time 1;\n"
        "log_time_format rfc3339;\n"
        "aggregate_period 45s;\n"
        "process_shell /bin/sh;\n"
        "metrictree_hashfunc crc32;\n";
    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "log_level"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "log_dest"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "metrictree_hashfunc"));
    json_decref(root);
}

void test_config_plain_extra_blocks_batch()
{
    const char *fragments[] = {
        "instance { name ut-inst-plain; }\n",
        "namespace { name ut-ns-a max_emit 1; }\nnamespace { name ut-ns-b max_emit 0; }\n",
        "probe { name ut-probe-http prober http valid_status_codes 2xx timeout 3s; }\n",
        "probe { name ut-probe-icmp prober icmp timeout 2000; }\n",
        "cluster { name ut-cl-plain size 500 replica_factor 1 servers s1:1111 s2:1112; }\n",
        "x509 { name ut-x509-b path /tmp/pem match .crt password p type pem; }\n",
        "modules { mod_a /lib/a.so; mod_b /lib/b.so; }\n",
        "puppeteer { https://example.com env=K:V; }\n",
        "chromecdp { executable /usr/bin/chromium port 9222 https://page.example timeout 10s; }\n"
    };
    const char *keys[] = {
        "instance", "namespace", "probe", "probe",
        "cluster", "x509", "modules", "puppeteer", "chromecdp"
    };

    json_error_t error;
    for (int f = 0; f < 9; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, keys[f]);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
            json_is_array(child) ? json_array_size(child) > 0 : json_object_size(child) > 0);
        json_decref(part);
    }
}

void test_config_plain_action_query_lang_batch()
{
    const char *fragments[] = {
        "action { name ut_pq_a1 expr http://127.0.0.1:1 serializer json dry_run content_type_json; }\n",
        "action { name ut_pq_a2 expr udp://127.0.0.1:2 serializer openmetrics dry_run add_label=team:core; }\n",
        "action { name ut_pq_a3 expr http://127.0.0.1:3 serializer influxdb dry_run follow_redirects 2; }\n",
        "action { name ut_pq_a4 expr http://127.0.0.1:4 serializer graphite dry_run namespace ut_pq_ns; }\n",
        "action { name ut_pq_a5 expr cassandra://127.0.0.1:9042 serializer cassandra dry_run; }\n",
        "action { name ut_pq_a6 expr http://127.0.0.1:6 serializer clickhouse dry_run; }\n",
        "action { name ut_pq_a7 expr http://127.0.0.1:7 serializer postgresql dry_run; }\n",
        "action { name ut_pq_a8 expr http://127.0.0.1:8 serializer elasticsearch dry_run; }\n",
        "query { make ut_pq_q1 expr up datasource internal action ut_pq_a1; }\n",
        "query { make ut_pq_q2 expr process_match datasource internal action ut_pq_a2; }\n",
        "query { make ut_pq_q3 expr count(x) datasource internal action ut_pq_a3; }\n",
        "scheduler { name ut_pq_sched action ut_pq_a1 expr sum(up) period 30s datasource internal; }\n",
        "lang { key ut_pq_lang expr http://127.0.0.1/lang lang lua method run file /tmp/ut.lua; }\n",
        "resolver { udp://8.8.4.4:53; tcp://1.1.1.1:53; }\n"
    };
    const char *keys[] = {
        "action", "action", "action", "action", "action", "action", "action", "action",
        "query", "query", "query", "scheduler", "lang", "resolver"
    };

    json_error_t error;
    for (int f = 0; f < 14; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, keys[f]);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
            json_is_array(child) ? json_array_size(child) > 0 : json_object_size(child) > 0);
        json_decref(part);
    }
}

void test_config_plain_entrypoint_batch2()
{
    const char *fragments[] = {
        "entrypoint { handler prometheus tcp 8080 namespace ut_ep_ns; }\n",
        "entrypoint { handler json udp 514 allow 10.0.0.0/8 deny 192.168.0.0/16; }\n",
        "entrypoint { handler openmetrics format prometheus tls 8443 unix /tmp/ut.sock key ut_ep_key; }\n",
        "entrypoint { handler graphite unixgram /tmp/ut.g mapping path1 api v1 threads 2; }\n",
        "entrypoint { handler influxdb tcp 8086 log_level debug metric_aggregation true ttl 60s; }\n"
    };

    json_error_t error;
    for (int f = 0; f < 5; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, "entrypoint");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(child) > 0);
        json_decref(part);
    }
}

void test_config_plain_timing_repeat_globals()
{
    const char *conf =
        "synchronization_period 120s;\n"
        "tls_collect_period 300s;\n"
        "query_repeat 5;\n"
        "cluster_repeat 2;\n"
        "aggregator_repeat 3;\n"
        "workers auto;\n"
        "ttl 900;\n"
        "system_collect_period 75;\n"
        "query_period 20s;\n"
        "grok_patterns /tmp/a.grok /tmp/b.grok;\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "synchronization_period"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "query_repeat"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "grok_patterns"));
    json_decref(root);
}

void test_config_plain_aggregate_more_subtypes()
{
    const char *conf =
        "aggregate {\n"
        "  log \"/var/log/messages\" log_channel_raw=raw-ch;\n"
        "  consul-configuration http://127.0.0.1:8500/v1/kv/;\n"
        "  consul-discovery http://127.0.0.1:8500/v1/catalog/services;\n"
        "  parser http://127.0.0.1:9100/metrics period=30s;\n"
        "  https https://example.com follow_redirects=2 tls_verify=off;\n"
        "  mongodb mongodb://127.0.0.1:27017/?authSource=admin;\n"
        "  mtail file:///var/log/nginx-access.log name=nginx_mtail;\n"
        "  beanstalkd tcp://127.0.0.1:11300;\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    json_t *aggregate = json_object_get(root, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(aggregate) >= 6);
    json_decref(root);
}

static void test_config_plain_mega_document_v2()
{
    const char *conf =
        "log_level info;\n"
        "log_dest syslog;\n"
        "log_form plain;\n"
        "workers 8;\n"
        "ttl 240;\n"
        "synchronization_period 90s;\n"
        "tls_collect_period 600s;\n"
        "query_repeat 4;\n"
        "cluster_repeat 1;\n"
        "aggregator_repeat 2;\n"
        "grok_patterns /opt/grok/a.grok;\n"
        "log_channel { name v2-ch; dest file:///var/log/v2.log; }\n"
        "persistence { directory /var/lib/alligator-v2; }\n"
        "cluster { name v2-cluster size 20 replica_factor 3 servers n1:1111 n2:1112 n3:1113; }\n"
        "instance { name v2-inst; }\n"
        "namespace { name v2-ns max_emit 1; }\n"
        "threaded_loop { name v2-loop threads 8; }\n"
        "resolver { udp://1.0.0.1:53; }\n"
        "scheduler { name v2_sched action v2_act expr avg(cpu) period 45s datasource internal; }\n"
        "action { name v2_act expr http://127.0.0.1:9090 serializer prometheus dry_run; }\n"
        "lang { key v2_lang expr http://127.0.0.1:9090/lang lang duktape method run; }\n"
        "query { make v2_q expr present(up) datasource internal action v2_act; }\n"
        "probe { name v2_probe prober tcp valid_status_codes 200 timeout 1s; }\n"
        "system { base disk smart interrupts load add_label=site:v2; }\n"
        "x509 { name v2-x509 path /etc/ssl match .key password z type pem; }\n"
        "modules { custom /opt/libcustom.so; }\n"
        "reject { label dc value us; }\n"
        "metricstransform { include v2_.* match_type regexp label dc regex '^(.+)$' replacement 'dc-$1'; }\n"
        "grok { key v2_grok; name v2_grok_name; match '%{NUMBER:n}'; counter v2_c n; }\n"
        "mtail { name v2_mtail script /tmp/v2.mtail; }\n"
        "chromecdp { executable /usr/bin/chrome port 9224 https://v2.example timeout 20s; }\n"
        "puppeteer { https://v2.example headers=H:V env=E:1; }\n"
        "entrypoint {\n"
        "  handler influxdb;\n"
        "  tcp 9091 udp 5514;\n"
        "  namespace v2-ns cluster v2-cluster instance v2-inst;\n"
        "  allow 172.16.0.0/12;\n"
        "  mapping metrics prom v2;\n"
        "  log_channel v2-ch;\n"
        "}\n"
        "aggregate {\n"
        "  json http://127.0.0.1:9200/_cluster/health;\n"
        "  redis tcp://127.0.0.1:6380;\n"
        "  snmp udp://public@127.0.0.1:161/1.3.6.1.2.1.1.5.0;\n"
        "  parser http://127.0.0.1:8080/status;\n"
        "  https https://v2.example follow_redirects=1;\n"
        "  blackbox https://example.net period=45s;\n"
        "  log \"/var/log/v2.log\";\n"
        "  grok file:///var/log/v2-nginx.log name=v2nginx;\n"
        "  mtail file:///var/log/v2-app.log name=v2_mtail;\n"
        "  beanstalkd tcp://127.0.0.1:11301;\n"
        "  mongodb mongodb://127.0.0.1:27018;\n"
        "  consul-configuration http://127.0.0.1:8500;\n"
        "  consul-discovery http://127.0.0.1:8500;\n"
        "}\n";

    string *s = string_new();
    string_cat(s, (char *)conf, strlen(conf));
    char *json_s = config_plain_to_json(s);
    string_free(s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);

    json_error_t error;
    json_t *root = json_loads(json_s, 0, &error);
    free(json_s);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "entrypoint"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "reject"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "metricstransform"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
        json_array_size(json_object_get(root, "aggregate")) >= 10);
    json_decref(root);
}

void test_config_plain_reject_transform_batch()
{
    const char *fragments[] = {
        "reject { label env value prod; }\nreject { label role; }\n",
        "metricstransform { include ut_mt_.* match_type regexp label host regex '^(.+)$' replacement 'h-$1'; }\n",
        "query { make ut_rej_q expr up datasource internal action ut_rej_a; }\n"
        "action { name ut_rej_a expr http://127.0.0.1:9 serializer json dry_run; }\n"
    };
    const char *keys[] = { "reject", "metricstransform", "query" };

    json_error_t error;
    for (int f = 0; f < 3; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, keys[f]);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        json_decref(part);
    }
}

void test_config_plain_action_serializer_plain_batch()
{
    const char *serializers[] = {
        "json", "openmetrics", "graphite", "influxdb", "statsd",
        "dogstatsd", "dynatrace", "carbon2", "dsv", "elasticsearch",
        "cassandra", "clickhouse", "postgresql", "mongodb", "otlp"
    };
    json_error_t error;
    for (int i = 0; i < 15; i++) {
        char conf[256];
        snprintf(conf, sizeof(conf),
            "action { name ut_ser_plain_%d expr http://127.0.0.1:%d serializer %s dry_run; }\n",
            i, 20000 + i, serializers[i]);
        string *s = string_new();
        string_cat(s, conf, strlen(conf));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *action = json_object_get(part, "action");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, action);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(action) > 0);
        json_decref(part);
    }
}

void test_config_plain_fragment_library()
{
    const char *fragments[] = {
        "log_channel { name frag-ch1; dest stderr; }\n",
        "log_channel { name frag-ch2; dest file:///tmp/f.log; }\n",
        "persistence { directory /tmp/frag-persist; }\n",
        "resolver { udp://8.8.8.8:53; }\n",
        "resolver { tcp://9.9.9.9:53; }\n",
        "instance { name frag-inst-1; }\n",
        "instance { name frag-inst-2; }\n",
        "namespace { name frag-ns-1 max_emit 0; }\n",
        "namespace { name frag-ns-2 max_emit 5; }\n",
        "threaded_loop { name frag-loop-1 threads 2; }\n",
        "threaded_loop { name frag-loop-2 threads 6; }\n",
        "cluster { name frag-cl size 100 replica_factor 1 servers a:1 b:2; }\n",
        "probe { name frag-probe-http prober http timeout 3s; }\n",
        "probe { name frag-probe-icmp prober icmp timeout 1000; }\n",
        "scheduler { name frag-sched action frag-act expr count(x) period 10s datasource internal; }\n",
        "action { name frag-act expr http://127.0.0.1:1 serializer json dry_run; }\n",
        "query { make frag-q expr up datasource internal action frag-act; }\n",
        "lang { key frag-lang expr http://127.0.0.1/l lang lua method run; }\n",
        "x509 { name frag-x509 path /tmp/p match .pem type pem password p; }\n",
        "modules { m1 /lib/m1.so; m2 /lib/m2.so; }\n",
        "puppeteer { https://frag.example env=K:V; }\n",
        "chromecdp { executable /usr/bin/chromium port 9222 https://frag.example timeout 5s; }\n",
        "grok { key frag-grok; name frag_grok_n; match '%{WORD:w}'; counter frag_c w; }\n",
        "mtail { name frag-mtail script /tmp/frag.mtail; }\n",
        "system { base network firewall cadvisor; }\n",
        "reject { label team value ops; }\n",
        "metricstransform { include frag_.* match_type strict label k regex '^(.+)$' replacement '$1'; }\n",
        "entrypoint { handler prometheus tcp 19999; }\n",
        "aggregate { json http://127.0.0.1:9100/metrics; }\n",
        "aggregate { snmp udp://public@127.0.0.1:161/1.3.6.1.2.1.1.1.0; }\n"
    };
    const char *keys[] = {
        "log_channel", "log_channel", "persistence", "resolver", "resolver",
        "instance", "instance", "namespace", "namespace", "threaded_loop",
        "threaded_loop", "cluster", "probe", "probe", "scheduler",
        "action", "query", "lang", "x509", "modules",
        "puppeteer", "chromecdp", "grok", "mtail", "system",
        "reject", "metricstransform", "entrypoint", "aggregate", "aggregate"
    };

    json_error_t error;
    for (int f = 0; f < 30; f++) {
        string *s = string_new();
        string_cat(s, (char *)fragments[f], strlen(fragments[f]));
        char *json_s = config_plain_to_json(s);
        string_free(s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_s);
        json_t *part = json_loads(json_s, 0, &error);
        free(json_s);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, part);
        json_t *child = json_object_get(part, keys[f]);
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, child);
        json_decref(part);
    }
}

void test_config_generators_amtail_grok_paths()
{
    json_t *dst = json_object();

    amtail_node an = {0};
    an.name = "ut_mtail_gen";
    an.script = "/tmp/ut.mtail";
    an.key = "ut_mtail_key";
    amtail_generate_conf(dst, &an);

    json_t *mtail = json_object_get(dst, "mtail");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mtail);
    json_t *m0 = json_array_get(mtail, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, m0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "ut_mtail_gen",
        json_string_value(json_object_get(m0, "name")));

    grok_node gn = {0};
    gn.name = "ut_grok_name";
    gn.value = "bytes";
    gn.match = string_init_dup("%{WORD:word}");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, gn.match);

    grok_ds gps = {0};
    gps.key = "ut_grok_key";
    gps.hash = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, gps.hash);
    alligator_ht_insert(gps.hash, &gn.node, &gn, tommy_strhash_u32(0, gn.name));
    grok_generate_conf(dst, &gps);

    json_t *grok = json_object_get(dst, "grok");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, grok);
    json_t *g0 = json_array_get(grok, 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, g0);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "ut_grok_key",
        json_string_value(json_object_get(g0, "key")));

    string_free(gn.match);
    alligator_ht_done(gps.hash);
    free(gps.hash);
    json_decref(dst);
}

void test_config_get_amtail_grok_runtime_paths()
{
    alligator_ht *saved_amtail = ac->amtail;
    alligator_ht *saved_grok = ac->grok;

    ac->amtail = alligator_ht_init(NULL);
    ac->grok = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->amtail);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->grok);

    amtail_node *an = calloc(1, sizeof(*an));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    an->name = strdup("ut_runtime_mtail");
    an->script = strdup("/tmp/runtime.mtail");
    alligator_ht_insert(ac->amtail, &an->node, an, tommy_strhash_u32(0, an->name));

    grok_node *gn = calloc(1, sizeof(*gn));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, gn);
    gn->name = strdup("ut_runtime_grok");
    gn->value = strdup("value");
    gn->match = string_init_dup("%{NUMBER:n}");
    grok_ds *gps = calloc(1, sizeof(*gps));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, gps);
    gps->key = strdup("runtime_grok_key");
    gps->hash = alligator_ht_init(NULL);
    alligator_ht_insert(gps->hash, &gn->node, gn, tommy_strhash_u32(0, gn->name));
    alligator_ht_insert(ac->grok, &gps->node, gps, tommy_strhash_u32(0, gps->key));

    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "mtail"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "grok"));
    json_decref(root);

    alligator_ht_done(ac->amtail);
    alligator_ht_done(ac->grok);
    free(an->name);
    free(an->script);
    free(an);
    string_free(gn->match);
    free(gn->name);
    free(gn->value);
    free(gn);
    alligator_ht_done(gps->hash);
    free(gps->hash);
    free(gps->key);
    free(gps);
    free(ac->amtail);
    free(ac->grok);
    ac->amtail = saved_amtail;
    ac->grok = saved_grok;
}

void test_config_get_cluster_chromecdp_runtime_paths()
{
    alligator_ht *saved_cluster = ac->cluster;
    alligator_ht *saved_puppeteer = ac->puppeteer;
    alligator_ht *saved_chromecdp = ac->chromecdp;
    char *saved_exec = ac->chromecdp_exec;

    ac->cluster = alligator_ht_init(NULL);
    ac->puppeteer = alligator_ht_init(NULL);
    ac->chromecdp = alligator_ht_init(NULL);
    ac->chromecdp_exec = "/usr/bin/chromium";
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->cluster);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->puppeteer);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->chromecdp);

    cluster_node *cn = calloc(1, sizeof(*cn));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cn);
    cn->name = "ut-runtime-cluster";
    cn->servers_size = 2;
    cn->replica_factor = 2;
    cn->servers = calloc(2, sizeof(*cn->servers));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cn->servers);
    cn->servers[0].name = "h1:1111";
    cn->servers[1].name = "h2:1112";
    alligator_ht_insert(ac->cluster, &cn->node, cn, tommy_strhash_u32(0, cn->name));

    puppeteer_node *pn = calloc(1, sizeof(*pn));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, pn);
    pn->url = string_init_dup("https://ut.example");
    pn->value = "{\"timeout\":30}";
    alligator_ht_insert(ac->puppeteer, &pn->node, pn, tommy_strhash_u32(0, pn->url->s));

    cdp_node *cdp = calloc(1, sizeof(*cdp));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cdp);
    cdp->url = string_init_dup("https://page.example");
    cdp->value = "{\"wait\":1}";
    alligator_ht_insert(ac->chromecdp, &cdp->node, cdp, tommy_strhash_u32(0, cdp->url->s));

    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "cluster"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "puppeteer"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "chromecdp"));
    json_decref(root);

    string_free(pn->url);
    free(pn);
    string_free(cdp->url);
    free(cdp);
    free(cn->servers);
    free(cn);

    alligator_ht_done(ac->cluster);
    alligator_ht_done(ac->puppeteer);
    alligator_ht_done(ac->chromecdp);
    free(ac->cluster);
    free(ac->puppeteer);
    free(ac->chromecdp);
    ac->cluster = saved_cluster;
    ac->puppeteer = saved_puppeteer;
    ac->chromecdp = saved_chromecdp;
    ac->chromecdp_exec = saved_exec;
}

void test_config_get_aggregator_rich_runtime_paths()
{
    alligator_ht *saved_aggr = ac->aggregators;
    ac->aggregators = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->aggregators);

    context_arg *carg = calloc(1, sizeof(*carg));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);
    carg->key = strdup("json:https://user:secret@127.0.0.1:9100/metrics");
    carg->url = "https://user:secret@127.0.0.1:9100/metrics";
    carg->tls_ca_file = "/tmp/ca.pem";
    carg->tls_cert_file = "/tmp/cert.pem";
    carg->tls_key_file = "/tmp/key.pem";
    carg->tls_server_name = "metrics.local";
    carg->parser_name = "json";
    carg->follow_redirects = 3;
    carg->period = 60;
    carg->threaded_loop_name = "mega-loop";
    carg->log_level = L_DEBUG;
    carg->state = FILESTAT_STATE_SAVE;
    carg->notify = 1;
    carg->script = "/tmp/script.sh";
    carg->bind_address = "10.0.0.1";
    carg->bind_port = 8080;
    carg->buffer_request_size = 4096;
    carg->buffer_response_size = 8192;
    carg->labels = alligator_ht_init(NULL);
    labels_hash_insert_nocache(carg->labels, "team", "core");
    carg->log_ch = log_channel_upsert("agg-ch", "file:///tmp/agg.log", -1, -1, NULL, -1, NULL);
    carg->pquery = calloc(1, sizeof(char *));
    carg->pquery[0] = strdup("sum(up)");
    carg->pquery_size = 1;
    carg->metricstransform = json_loads(
        "{\"transforms\":[{\"include\":\"up\",\"match_type\":\"strict\",\"operations\":"
        "[{\"action\":\"update_label\",\"label\":\"instance\",\"value_actions\":"
        "[{\"regex\":\".*\",\"replacement\":\"local\",\"replace_all\":true}]}]}]}",
        0, NULL);

    alligator_ht_insert(ac->aggregators, &(carg->context_node), carg, tommy_strhash_u32(0, carg->key));

    context_arg *carg2 = calloc(1, sizeof(*carg2));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg2);
    carg2->key = strdup("grok:file:///var/log/app.log");
    carg2->url = "file:///var/log/app.log";
    carg2->parser_name = "grok";
    carg2->state = FILESTAT_STATE_STREAM;
    carg2->notify = 2;
    carg2->period = 120;
    alligator_ht_insert(ac->aggregators, &(carg2->context_node), carg2, tommy_strhash_u32(0, carg2->key));

    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    json_t *aggregate = json_object_get(root, "aggregate");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, aggregate);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(aggregate));
    json_decref(root);

    if (carg->metricstransform)
        json_decref(carg->metricstransform);
    free(carg->pquery[0]);
    free(carg->pquery);
    labels_hash_free(carg->labels);
    free(carg->key);
    free(carg);
    free(carg2->key);
    free(carg2);

    alligator_ht_done(ac->aggregators);
    free(ac->aggregators);
    ac->aggregators = saved_aggr;
}

void test_config_get_scheduler_x509_lang_runtime_paths()
{
    alligator_ht *saved_sched = ac->scheduler;
    alligator_ht *saved_x509 = ac->fs_x509;
    alligator_ht *saved_lang = ac->lang_aggregator;

    ac->scheduler = alligator_ht_init(NULL);
    ac->fs_x509 = alligator_ht_init(NULL);
    ac->lang_aggregator = alligator_ht_init(NULL);

    scheduler_node *sn = calloc(1, sizeof(*sn));
    sn->name = "ut_sched_rt";
    sn->action = "ut_action";
    sn->lang = "ut_lang";
    sn->datasource = "internal";
    sn->period = 30;
    sn->expr = string_init_dup("count(up)");
    alligator_ht_insert(ac->scheduler, &sn->node, sn, tommy_strhash_u32(0, sn->name));

    x509_fs_t *xf = calloc(1, sizeof(*xf));
    xf->name = "ut-x509-rt";
    xf->path = "/tmp/certs";
    xf->password = "secret";
    xf->type = X509_TYPE_PEM;
    xf->period = 3600;
    xf->match = string_tokens_new();
    string_tokens_push_dupn(xf->match, ".pem", 4);
    alligator_ht_insert(ac->fs_x509, &xf->node, xf, tommy_strhash_u32(0, xf->name));

    lang_options *lo = calloc(1, sizeof(*lo));
    lo->key = "ut_lang_rt";
    lo->lang = "duktape";
    lo->method = "main";
    lo->arg = "safe";
    lo->file = "/tmp/lang.js";
    lo->serializer = METRIC_SERIALIZER_JSON;
    alligator_ht_insert(ac->lang_aggregator, &lo->node, lo, tommy_strhash_u32(0, lo->key));

    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "scheduler"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "x509"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "lang"));
    json_decref(root);

    string_free(sn->expr);
    free(sn);
    if (xf->match)
        string_tokens_free(xf->match);
    free(xf);
    free(lo);

    alligator_ht_done(ac->scheduler);
    alligator_ht_done(ac->fs_x509);
    alligator_ht_done(ac->lang_aggregator);
    free(ac->scheduler);
    free(ac->fs_x509);
    free(ac->lang_aggregator);
    ac->scheduler = saved_sched;
    ac->fs_x509 = saved_x509;
    ac->lang_aggregator = saved_lang;
}

void test_config_get_system_modules_runtime_paths()
{
    alligator_ht *saved_up = ac->system_userprocess;
    alligator_ht *saved_gp = ac->system_groupprocess;
    alligator_ht *saved_sc = ac->system_sysctl;
    alligator_ht *saved_mod = ac->modules;

    ac->system_userprocess = alligator_ht_init(NULL);
    ac->system_groupprocess = alligator_ht_init(NULL);
    ac->system_sysctl = alligator_ht_init(NULL);
    ac->modules = alligator_ht_init(NULL);

    userprocess_node *up = calloc(1, sizeof(*up));
    up->name = "nginx";
    alligator_ht_insert(ac->system_userprocess, &up->node, up, tommy_strhash_u32(0, up->name));

    userprocess_node *gp = calloc(1, sizeof(*gp));
    gp->name = "www-data";
    alligator_ht_insert(ac->system_groupprocess, &gp->node, gp, tommy_strhash_u32(0, gp->name));

    sysctl_node *sc = calloc(1, sizeof(*sc));
    sc->name = "vm.swappiness";
    alligator_ht_insert(ac->system_sysctl, &sc->node, sc, tommy_strhash_u32(0, sc->name));

    module_t *mod = calloc(1, sizeof(*mod));
    mod->key = "utmod";
    mod->path = "/tmp/utmod.so";
    alligator_ht_insert(ac->modules, &mod->node, mod, tommy_strhash_u32(0, mod->key));

    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    json_t *system = json_object_get(root, "system");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, system);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "userprocess"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "sysctl"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "modules"));
    json_decref(root);

    free(up);
    free(gp);
    free(sc);
    free(mod);
    alligator_ht_done(ac->system_userprocess);
    alligator_ht_done(ac->system_groupprocess);
    alligator_ht_done(ac->system_sysctl);
    alligator_ht_done(ac->modules);
    free(ac->system_userprocess);
    free(ac->system_groupprocess);
    free(ac->system_sysctl);
    free(ac->modules);
    ac->system_userprocess = saved_up;
    ac->system_groupprocess = saved_gp;
    ac->system_sysctl = saved_sc;
    ac->modules = saved_mod;
}

void test_config_get_action_probe_query_runtime_paths()
{
    alligator_ht *saved_action = ac->action;
    alligator_ht *saved_probe = ac->probe;

    ac->action = alligator_ht_init(NULL);
    ac->probe = alligator_ht_init(NULL);

    action_node *an = calloc(1, sizeof(*an));
    an->name = strdup("ut_action_rt");
    an->expr = strdup("up == 0");
    an->expr_len = strlen(an->expr);
    an->serializer = METRIC_SERIALIZER_JSON;
    an->dry_run = 1;
    alligator_ht_insert(ac->action, &an->node, an, tommy_strhash_u32(0, an->name));

    probe_node *pn = calloc(1, sizeof(*pn));
    pn->name = strdup("ut_probe_rt");
    pn->prober_str = "http";
    pn->timeout = 1000;
    pn->method = HTTP_GET;
    alligator_ht_insert(ac->probe, &pn->node, pn, tommy_strhash_u32(0, pn->name));

    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "action"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(root, "probe"));
    json_decref(root);

    free(an->name);
    free(an->expr);
    free(an);
    free(pn->name);
    free(pn);
    alligator_ht_done(ac->action);
    alligator_ht_done(ac->probe);
    free(ac->action);
    free(ac->probe);
    ac->action = saved_action;
    ac->probe = saved_probe;
}

void test_config_get_system_flags_runtime_paths()
{
    uint8_t saved_base = ac->system_base;
    uint8_t saved_disk = ac->system_disk;
    uint8_t saved_network = ac->system_network;
    uint8_t saved_smart = ac->system_smart;
    uint8_t saved_ipmi = ac->system_ipmi;
    uint8_t saved_interrupts = ac->system_interrupts;
    uint8_t saved_firewall = ac->system_firewall;
    uint8_t saved_ipset = ac->system_ipset;
    uint8_t saved_ipset_entries = ac->system_ipset_entries;
    uint8_t saved_cadvisor = ac->system_cadvisor;
    uint8_t saved_packages = ac->system_packages;
    uint8_t saved_services = ac->system_services;
    uint8_t saved_services_process = ac->system_services_process;
    uint8_t saved_process = ac->system_process;
    uint8_t saved_cpuavg = ac->system_cpuavg;
    char *saved_sysfs = ac->system_sysfs;
    char *saved_procfs = ac->system_procfs;
    char *saved_rundir = ac->system_rundir;

    ac->system_base = 1;
    ac->system_disk = 1;
    ac->system_network = 1;
    ac->system_smart = 1;
    ac->system_ipmi = 1;
    ac->system_interrupts = 1;
    ac->system_firewall = 1;
    ac->system_ipset = 1;
    ac->system_cadvisor = 1;
    ac->system_packages = 1;
    ac->system_services = 1;
    ac->system_services_process = 1;
    ac->system_process = 1;
    ac->system_cpuavg = 1;
    ac->system_sysfs = "/sys";
    ac->system_procfs = "/proc";
    ac->system_rundir = "/run";

    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    json_t *system = json_object_get(root, "system");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, system);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "base"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "network"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "firewall"));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, json_object_get(system, "cadvisor"));
    json_decref(root);

    ac->system_ipset_entries = 1;
    root = config_get();
    system = json_object_get(root, "system");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, system);
    json_decref(root);

    ac->system_base = saved_base;
    ac->system_disk = saved_disk;
    ac->system_network = saved_network;
    ac->system_smart = saved_smart;
    ac->system_ipmi = saved_ipmi;
    ac->system_interrupts = saved_interrupts;
    ac->system_firewall = saved_firewall;
    ac->system_ipset = saved_ipset;
    ac->system_ipset_entries = saved_ipset_entries;
    ac->system_cadvisor = saved_cadvisor;
    ac->system_packages = saved_packages;
    ac->system_services = saved_services;
    ac->system_services_process = saved_services_process;
    ac->system_process = saved_process;
    ac->system_cpuavg = saved_cpuavg;
    ac->system_sysfs = saved_sysfs;
    ac->system_procfs = saved_procfs;
    ac->system_rundir = saved_rundir;
}

void test_config_get_entrypoint_rich_runtime_paths()
{
    alligator_ht *saved_ep = ac->entrypoints;
    ac->entrypoints = alligator_ht_init(NULL);

    context_arg *ep = calloc(1, sizeof(*ep));
    ep->key = strdup("ep-rich-rt");
    ep->ttl = 60;
    ep->threads = 8;
    ep->pingloop = 5;
    ep->api_enable = 1;
    ep->cluster = "ut-cluster";
    ep->instance = "ut-inst";
    ep->lang = "ut_lang";
    ep->name = "prometheus";
    ep->metric_aggregation = ENTRYPOINT_AGGREGATION_COUNT;
    ep->buffer_request_size = 2048;
    ep->buffer_response_size = 4096;
    ep->labels = alligator_ht_init(NULL);
    labels_hash_insert_nocache(ep->labels, "role", "front");
    alligator_ht_insert(ac->entrypoints, &(ep->context_node), ep, tommy_strhash_u32(0, ep->key));

    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    json_t *entrypoint = json_object_get(root, "entrypoint");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, entrypoint);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, json_array_size(entrypoint) > 0);
    json_decref(root);

    labels_hash_free(ep->labels);
    free(ep->key);
    free(ep);
    alligator_ht_done(ac->entrypoints);
    free(ac->entrypoints);
    ac->entrypoints = saved_ep;
}

void test_config_get_resolver_runtime_paths()
{
    resolver_data rd1 = {0}, rd2 = {0};
    host_aggregator_info *hi1 = calloc(1, sizeof(*hi1));
    host_aggregator_info *hi2 = calloc(1, sizeof(*hi2));
    hi1->host = strdup("1.1.1.1");
    hi1->url = strdup("udp://1.1.1.1:53");
    hi1->user = strdup("");
    hi1->transport_string = strdup("udp");
    snprintf(hi1->port, sizeof(hi1->port), "%s", "53");
    rd1.hi = hi1;
    hi2->host = strdup("8.8.8.8");
    hi2->url = strdup("tcp://8.8.8.8:53");
    hi2->user = strdup("");
    hi2->transport_string = strdup("tcp");
    snprintf(hi2->port, sizeof(hi2->port), "%s", "53");
    rd2.hi = hi2;

    resolver_data *old_srv_resolver[255] = {0};
    uint8_t old_resolver_size = ac->resolver_size;
    for (uint16_t i = 0; i < 255; ++i)
        old_srv_resolver[i] = ac->srv_resolver[i];
    ac->srv_resolver[0] = &rd1;
    ac->srv_resolver[1] = &rd2;
    ac->resolver_size = 2;

    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
    json_t *resolver = json_object_get(root, "resolver");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, resolver);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, json_array_size(resolver));
    json_decref(root);

    ac->resolver_size = old_resolver_size;
    for (uint16_t i = 0; i < 255; ++i)
        ac->srv_resolver[i] = old_srv_resolver[i];
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
}

void test_match_rules_regex_paths()
{
    match_rules *mr = calloc(1, sizeof(*mr));
    mr->hash = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, mr->hash);

    /* exact-string hash path */
    match_push(mr, "postgres", strlen("postgres"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, match_mapper(mr, "postgres", strlen("postgres"), "n"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, match_mapper(mr, "postgre", strlen("postgre"), "n"));

    match_push(mr, "/foo.*/", strlen("/foo.*/"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, mr->head != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, match_mapper(mr, "foobar", strlen("foobar"), "n"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, match_mapper(mr, "bar", strlen("bar"), "n"));

    /* broken regex should not crash and should not replace existing regex chain */
    match_push(mr, "/[/", strlen("/[/"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, mr->head != NULL);

    /* hash deletion path */
    match_del(mr, "postgres", strlen("postgres"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, match_mapper(mr, "postgres", strlen("postgres"), "n"));

    match_free(mr);
}
