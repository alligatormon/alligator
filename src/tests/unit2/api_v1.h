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
			{ \"name\": \"to-elastic\", \"expr\": \"http://localhost:9200/_bulk\", \"datasource\": \"internal\", \"serializer\": \"elasticsearch\", \"index_template\": \"alligator-%Y-%m-%d\", \"follow_redirects\": 12 } \
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
			{ \"key\": \"l1\", \"lang\": \"mruby\", \"script\": \"run-something\", \"method\": \"main\", \"hidden_arg\": true, \"log_level\": \"debug\", \"arg\": \"hello world\", \"serializer\": \"json\"}, \
			{ \"key\": \"l2\", \"lang\": \"so\", \"module\": \"module_1\", \"file\": \"path\", \"method\": \"_main\", \"arg\": \"goodbye world\", \"serializer\": \"dsv\"} \
		] \
	}\
	";

    http_api_v1(NULL, NULL, config);

    lo = lang_get("l1");
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, lo);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "run-something", lo->script);
    assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "mruby", lo->lang);
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
