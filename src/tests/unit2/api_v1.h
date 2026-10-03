#include "main.h"
#include "api/api.h"
#include "query/type.h"
#include "scheduler/type.h"
#include "lang/type.h"
#include "cluster/type.h"
#include "common/logs.h"
#include "metric/namespace.h"

void api_test_query_1() {
	ac->query = alligator_ht_init(NULL);
    query_ds* qds;
    query_node *qn;
	char *config = "{\"query\": [ \
			{ \"make\": \"socket_match\", \"expr\": \"count by (src_port, process) (socket_stat{process=\\\"nginx\\\", src_port=\\\"80\\\"})\", \"datasource\": \"internal\", \"action\": \"run-test\", \"ns\": \"default\"}, \
			{ \"make\": \"external-sql\", \"expr\": \"SELECT now() - pg_last_xact_replay_timestamp() AS replication_delay\", \"datasource\": \"pg\", \"fields\": [\"replication_delay\"], \"ns\": \"postgres\"}, \
			{ \"make\": \"external-kv\", \"expr\": \"MGET veigieMu ohThozoo ahPhouca\", \"datasource\": \"redis\", \"ns\": \"0\"} \
		] \
	}\
	";

    http_api_v1(NULL, NULL, config);

    qds = query_get("internal");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, qds);
    qn = query_get_node(qds, "socket_match");
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "socket_match", qn->make);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "run-test", qn->action);

    qds = query_get("pg");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, qds);
    qn = query_get_node(qds, "external-sql");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, qn);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "external-sql", qn->make);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "postgres", qn->ns);

    qds = query_get("redis");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, qds);
    qn = query_get_node(qds, "external-kv");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, qn);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "external-kv", qn->make);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "0", qn->ns);
}

void api_test_action_1() {
	ac->action = alligator_ht_init(NULL);
    action_node* an;
	char *config = "{\"action\": [ \
			{ \"name\": \"run-local\", \"expr\": \"exec://systemctl restart sshd\", \"ns\": \"default\", \"work_dir\": \"/root\"}, \
			{ \"name\": \"to-pushgateway\", \"expr\": \"tcp://localhost:9091/metrics\", \"datasource\": \"internal\", \"serializer\": \"openmetrics\"}, \
			{ \"name\": \"to-clickhouse\", \"expr\": \"http://localhost:8123/\", \"datasource\": \"internal\", \"serializer\": \"clickhouse\", \"engine\": \"ENGINE=MergeTree ORDER BY timestamp\"}, \
			{ \"name\": \"to-elastic\", \"expr\": \"http://localhost:9200/_bulk\", \"datasource\": \"internal\", \"serializer\": \"elasticsearch\", \"index_template\": \"alligator-%Y-%m-%d\", \"follow_redirects\": 12 }, \
			{ \"name\": \"to-otlp-mtls\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_certificate\": \"/secrets/tls.crt\", \"tls_key\": \"/secrets/tls.key\", \"tls_ca\": \"/secrets/ca.crt\", \"tls_verify\": \"on\" }, \
			{ \"name\": \"to-otlp-ca\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_certificate\": \"/secrets/tls.crt\", \"tls_key\": \"/secrets/tls.key\", \"tls_ca\": \"/secrets/ca.crt\" }, \
			{ \"name\": \"to-otlp-insecure\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_certificate\": \"/secrets/tls.crt\", \"tls_key\": \"/secrets/tls.key\", \"tls_ca\": \"/secrets/ca.crt\", \"tls_verify\": \"off\" }, \
			{ \"name\": \"to-otlp-client\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_certificate\": \"/secrets/tls.crt\", \"tls_key\": \"/secrets/tls.key\" }, \
			{ \"name\": \"to-otlp-cert-only\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_certificate\": \"/secrets/tls.crt\" }, \
			{ \"name\": \"to-otlp-key-only\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_key\": \"/secrets/tls.key\" } \
		] \
	}\
	";

    http_api_v1(NULL, NULL, config);

    an =  action_get("run-local");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    //assert_equal_int(__FILE__, __FUNCTION__, __LINE__, ACTION_TYPE_SHELL, an->type);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "exec://systemctl restart sshd", an->expr);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an->work_dir);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/root", an->work_dir->s);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "default", an->ns);

    an =  action_get("to-pushgateway");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "tcp://localhost:9091/metrics", an->expr);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, METRIC_SERIALIZER_OPENMETRICS, an->serializer);

    an = action_get("to-clickhouse");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, METRIC_SERIALIZER_CLICKHOUSE, an->serializer);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an->engine);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "http://localhost:8123/", an->expr);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "ENGINE=MergeTree ORDER BY timestamp", an->engine->s);

    an = action_get("to-elastic");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, METRIC_SERIALIZER_ELASTICSEARCH, an->serializer);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an->index_template);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "alligator-%Y-%m-%d", an->index_template->s);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "http://localhost:9200/_bulk", an->expr);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 12, an->follow_redirects);

    an = action_get("to-otlp-mtls");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, METRIC_SERIALIZER_OTLP_PROTOBUF, an->serializer);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "https://collector.example:4318/v1/metrics", an->expr);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.crt", an->tls_cert_file);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.key", an->tls_key_file);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/ca.crt", an->tls_ca_file);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify_defined);

    an = action_get("to-otlp-ca");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/ca.crt", an->tls_ca_file);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify_defined);

    an = action_get("to-otlp-insecure");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/ca.crt", an->tls_ca_file);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, an->tls_verify);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify_defined);

    an = action_get("to-otlp-client");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.crt", an->tls_cert_file);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.key", an->tls_key_file);
    assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an->tls_ca_file);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, an->tls_verify);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, an->tls_verify_defined);

    an = action_get("to-otlp-cert-only");
    assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an);
    an = action_get("to-otlp-key-only");
    assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an);

    /* A rejected pair must not replace an action that was already installed. */
    {
        extern void action_generate_conf(void *funcarg, void *arg);
        json_t *dst;
        json_t *arr;
        json_t *row;
        char *bad = "{\"action\": [ \
            { \"name\": \"to-otlp-mtls\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_certificate\": \"/secrets/only.crt\" } \
        ]}";

        http_api_v1(NULL, NULL, bad);
        an = action_get("to-otlp-mtls");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.crt", an->tls_cert_file);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.key", an->tls_key_file);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify);

        dst = json_object();
        action_generate_conf(dst, an);
        arr = json_object_get(dst, "action");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, arr);
        row = json_array_get(arr, 0);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.crt", json_string_value(json_object_get(row, "tls_certificate")));
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.key", json_string_value(json_object_get(row, "tls_key")));
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/ca.crt", json_string_value(json_object_get(row, "tls_ca")));
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "on", json_string_value(json_object_get(row, "tls_verify")));
        json_decref(dst);

        an = action_get("to-otlp-insecure");
        dst = json_object();
        action_generate_conf(dst, an);
        row = json_array_get(json_object_get(dst, "action"), 0);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "off", json_string_value(json_object_get(row, "tls_verify")));
        json_decref(dst);
    }

    /* Empty cert and/or key is the same rejection. A CA with no client cert stays valid. */
    {
        char *empty_both = "{\"action\": [ \
            { \"name\": \"to-otlp-mtls\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_certificate\": \"\", \"tls_key\": \"\" } \
        ]}";
        char *empty_cert = "{\"action\": [ \
            { \"name\": \"to-otlp-mtls\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_certificate\": \"\", \"tls_key\": \"/secrets/tls.key\" } \
        ]}";
        char *empty_key = "{\"action\": [ \
            { \"name\": \"to-otlp-mtls\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_certificate\": \"/secrets/only.crt\", \"tls_key\": \"\" } \
        ]}";
        char *ca_only = "{\"action\": [ \
            { \"name\": \"to-otlp-ca-only\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_ca\": \"/secrets/ca.crt\" } \
        ]}";

        http_api_v1(NULL, NULL, empty_both);
        http_api_v1(NULL, NULL, empty_cert);
        http_api_v1(NULL, NULL, empty_key);
        an = action_get("to-otlp-mtls");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.crt", an->tls_cert_file);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/tls.key", an->tls_key_file);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify);

        http_api_v1(NULL, NULL, ca_only);
        an = action_get("to-otlp-ca-only");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
        assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an->tls_cert_file);
        assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an->tls_key_file);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/secrets/ca.crt", an->tls_ca_file);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify_defined);
    }

    /* Empty tls_ca installs as no CA. Explicit verify on still uses the system store. */
    {
        char *empty_ca = "{\"action\": [ \
            { \"name\": \"to-otlp-empty-ca\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_ca\": \"\" } \
        ]}";
        char *empty_ca_verify = "{\"action\": [ \
            { \"name\": \"to-otlp-empty-ca-verify\", \"expr\": \"https://collector.example:4318/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_ca\": \"\", \"tls_verify\": \"on\" } \
        ]}";
        char *replace_empty = "{\"action\": [ \
            { \"name\": \"to-otlp-ca-only\", \"expr\": \"https://replaced.example/v1/metrics\", \"serializer\": \"otlp_protobuf\", \"tls_ca\": \"\" } \
        ]}";

        http_api_v1(NULL, NULL, empty_ca);
        an = action_get("to-otlp-empty-ca");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
        assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an->tls_cert_file);
        assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an->tls_key_file);
        assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an->tls_ca_file);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, an->tls_verify);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, an->tls_verify_defined);

        http_api_v1(NULL, NULL, empty_ca_verify);
        an = action_get("to-otlp-empty-ca-verify");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
        assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an->tls_ca_file);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, an->tls_verify_defined);

        http_api_v1(NULL, NULL, replace_empty);
        an = action_get("to-otlp-ca-only");
        assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, an);
        assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "https://replaced.example/v1/metrics", an->expr);
        assert_ptr_null(__FILE__, __FUNCTION__, __LINE__, an->tls_ca_file);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, an->tls_verify);
        assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, an->tls_verify_defined);
    }
}

    char *name;
    char *action;
    char *lang;
    string *expr;
    char *datasource;
    uint8_t datasource_int;
    uv_timer_t *timer;
    uint64_t period;

void api_test_scheduler_1() {
	ac->scheduler = alligator_ht_init(NULL);
    scheduler_node* sn;
	char *config = "{\"scheduler\": [ \
			{ \"name\": \"timer-action\", \"action\": \"run-something\", \"datasource\": \"internal\", \"expr\": \"count(cpu_usage_time)\", \"period\": \"156s\"}, \
			{ \"name\": \"timer-lang\", \"lang\": \"call-something\", \"datasource\": \"internal\", \"expr\": \"count(cores_num)\", \"period\": \"1d\"} \
		] \
	}\
	";

    http_api_v1(NULL, NULL, config);

    sn = scheduler_get("timer-action");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sn);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "count(cpu_usage_time)", sn->expr->s);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "run-something", sn->action);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 156000, sn->period);

    sn = scheduler_get("timer-lang");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, sn);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "count(cores_num)", sn->expr->s);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "call-something", sn->lang);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 86400000, sn->period);
}

void api_test_lang_1() {
	ac->lang_aggregator = alligator_ht_init(NULL);
    lang_options *lo;
	char *config = "{\"lang\": [ \
			{ \"key\": \"l1\", \"lang\": \"so\", \"module\": \"module_0\", \"script\": \"run-something\", \"method\": \"main\", \"hidden_arg\": true, \"log_level\": \"debug\", \"arg\": \"hello world\", \"serializer\": \"json\"}, \
			{ \"key\": \"l2\", \"lang\": \"so\", \"module\": \"module_1\", \"file\": \"path\", \"method\": \"_main\", \"arg\": \"goodbye world\", \"serializer\": \"dsv\"} \
		] \
	}\
	";

    http_api_v1(NULL, NULL, config);

    lo = lang_get("l1");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, lo);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "run-something", lo->script);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "so", lo->lang);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "main", lo->method);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_DEBUG, lo->log_level);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "hello world", lo->arg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, METRIC_SERIALIZER_JSON, lo->serializer);

    lo = lang_get("l2");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, lo);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "path", lo->file);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "so", lo->lang);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "_main", lo->method);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_OFF, lo->log_level);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "goodbye world", lo->arg);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, METRIC_SERIALIZER_DSV, lo->serializer);
}

void api_test_cluster_1() {
    cluster_node *cn;
    ac->cluster = alligator_ht_init(NULL);
	char *config = "{\"cluster\": [ \
			{ \"name\": \"replication-receive\", \"size\": 14133, \"sharding_key\": [\"type\", \"project\"], \"replica_factor\": 12, \"type\": \"oplog\", \"servers\": [\"srv1.example.com:1111\", \"srv2.example.com:1111\"]}, \
			{ \"name\": \"replication-crawl\", \"replica_factor\": 1, \"type\": \"sharedlock\", \"servers\": [\"srv10.example.com:1111\", \"srv12.example.com:1111\"]} \
		] \
	}\
	";

    http_api_v1(NULL, NULL, config);

    cn = cluster_get("replication-receive");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cn);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 12, cn->replica_factor);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, CLUSER_TYPE_OPLOG, cn->type);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 14133, cn->size);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "srv1.example.com:1111", cn->servers[0].name);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "srv2.example.com:1111", cn->servers[1].name);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "type", cn->sharding_key[0]);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "project", cn->sharding_key[1]);

    cn = cluster_get("replication-crawl");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, cn);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, cn->replica_factor);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, CLUSER_TYPE_SHAREDLOCK, cn->type);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "srv10.example.com:1111", cn->servers[0].name);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "srv12.example.com:1111", cn->servers[1].name);
}

void api_test_global_options_and_errors() {
    if (!ac->system_carg)
        ac->system_carg = calloc(1, sizeof(*ac->system_carg));

    string *resp = string_init(4096);

    /* Same hash as tests.c startup: switching to e.g. XXH3 here would desync existing metric hashes. */
    char *cfg = "{\
      \"log_level\": \"debug\",\
      \"log_form\": \"syslog\",\
      \"aggregate_period\": \"3s\",\
      \"system_collect_period\": 7000,\
      \"tls_collect_period\": \"2s\",\
      \"query_period\": \"4s\",\
      \"synchronization_period\": 9000,\
      \"ttl\": \"11s\",\
      \"workers\": \"auto\",\
      \"metrictree_hashfunc\": \"lookup3\",\
      \"persistence\": {\"directory\": \"/tmp/alligator-ut\", \"period\": 7}\
    }";
    http_api_v1(resp, NULL, cfg);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, L_DEBUG, ac->log_level);
    ac->log_level = L_OFF;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, FORM_SYSLOG, ac->log_form);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3000, ac->aggregator_repeat);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 7000, ac->system_aggregator_repeat);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2000, ac->tls_fs_repeat);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4000, ac->query_repeat);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 9000, ac->cluster_repeat);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 11, ac->ttl);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->workers > 0);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, ac->persistence_dir);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/tmp/alligator-ut", ac->persistence_dir);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 7000, ac->persistence_period);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(resp->s, "HTTP/1.1 200 OK") != NULL);
    string_free(resp);

    resp = string_init(2048);
    http_api_v1(resp, NULL, "{\"system\": []}");
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(resp->s, "HTTP/1.1 400 Bad Request") != NULL);
    string_free(resp);

    resp = string_init(2048);
    http_api_v1(resp, NULL, "{");
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, strstr(resp->s, "HTTP/1.1 400 Bad Request") != NULL);
    string_free(resp);
}
