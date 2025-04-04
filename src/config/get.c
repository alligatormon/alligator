#include "common/selector.h"
#include "config/get.h"
#include "lang/type.h"
#include "x509/type.h"
#include "common/http.h"
#include "query/type.h"
#include "probe/probe.h"
#include "puppeteer/puppeteer.h"
#include "resolver/resolver.h"
#include "system/linux/sysctl.h"
#include "common/json_query.h"
#include "common/logs.h"
#include "common/crc32.h"
#include "common/xxh.h"
#include "common/murmurhash.h"
#include "main.h"
extern aconf *ac;

typedef struct config_get_arg
{
	void *arg;
	json_t *dst;
} config_get_arg;

void labels_kv_deserialize(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	labels_container *labelscont = arg;

	json_t *add_label = json_object_get(dst, "add_label");
	if (!add_label)
	{
		add_label = json_object();
		json_array_object_insert(dst, "add_label", add_label);
	}

	if (labelscont->name && labelscont->key)
	{
		json_t *name = json_string(labelscont->key);
		json_array_object_insert(add_label, labelscont->name, name);
	}
}

void env_struct_conf_deserialize(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	env_struct *es = arg;

	json_t *env = json_object_get(dst, "env");
	if (!env)
	{
		env = json_object();
		json_array_object_insert(dst, "env", env);
	}

	if (es->k && es->v)
	{
		json_t *value = json_string(es->v);
		json_array_object_insert(env, es->k, value);
	}
}

void config_global_get(json_t *dst)
{
	if (ac->log_level)
	{
		char *log = get_log_level_by_id(ac->log_level);
		json_t *log_level;
		if (!log)
			log_level =  json_integer(ac->log_level);
		else
			log_level = json_string(log);
		json_array_object_insert(dst, "log_level", log_level);
	}

	if (ac->aggregator_repeat)
	{
		json_t *aggregate_period = json_integer(ac->aggregator_repeat);
		json_array_object_insert(dst, "aggregate_period", aggregate_period);
	}
	if (ac->tls_fs_repeat)
	{
		json_t *aggregate_period = json_integer(ac->tls_fs_repeat);
		json_array_object_insert(dst, "tls_collect_period", aggregate_period);
	}
	if (ac->system_aggregator_repeat)
	{
		json_t *aggregate_period = json_integer(ac->system_aggregator_repeat);
		json_array_object_insert(dst, "system_collect_period", aggregate_period);
	}
	if (ac->query_repeat)
	{
		json_t *aggregate_period = json_integer(ac->query_repeat);
		json_array_object_insert(dst, "query_period", aggregate_period);
	}
	if (ac->cluster_repeat)
	{
		json_t *aggregate_period = json_integer(ac->cluster_repeat);
		json_array_object_insert(dst, "synchronization_period", aggregate_period);
	}

	if (ac->ttl)
	{
		json_t *ttl = json_integer(ac->ttl);
		json_array_object_insert(dst, "ttl", ttl);
	}

	if (ac->metrictree_hashfunc == alligator_ht_strhash) {
		json_t *metrictree_hashfunc = json_string("lookup3");
		json_array_object_insert(dst, "metrictree_hashfunc", metrictree_hashfunc);
	}
	else if (ac->metrictree_hashfunc == murmurhash) {
		json_t *metrictree_hashfunc = json_string("murmur");
		json_array_object_insert(dst, "metrictree_hashfunc", metrictree_hashfunc);
	}
	else if (ac->metrictree_hashfunc == crc32) {
		json_t *metrictree_hashfunc = json_string("crc32");
		json_array_object_insert(dst, "metrictree_hashfunc", metrictree_hashfunc);
	}
	else if (ac->metrictree_hashfunc == xxh3_run) {
		json_t *metrictree_hashfunc = json_string("XXH3");
		json_array_object_insert(dst, "metrictree_hashfunc", metrictree_hashfunc);
	}

	char *workers = getenv("UV_THREADPOOL_SIZE");
	uint64_t iworkers = 4;
	if (workers) {
		iworkers = strtoull(workers, NULL, 10);
	}
	json_t *cores = json_integer(iworkers);
	json_array_object_insert(dst, "workers", cores);

	if (ac->persistence_dir)
	{
		json_t *ctx = json_object();

		json_t *persistence_dir = json_string(ac->persistence_dir);
		json_t *persistence_period = json_integer(ac->aggregator_repeat);

		json_array_object_insert(dst, "persistence", ctx);

		json_array_object_insert(ctx, "directory", persistence_dir);
		json_array_object_insert(ctx, "period", persistence_period);
	}
}

void aggregator_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	context_arg *carg = arg;

	json_t *aggregate = json_object_get(dst, "aggregate");
	if (!aggregate)
	{
		aggregate = json_array();
		json_array_object_insert(dst, "aggregate", aggregate);
	}

	json_t *ctx = json_object();
	json_array_object_insert(aggregate, NULL, ctx);

	if (carg->url)
	{
		json_t *url = json_string(carg->url);
		json_array_object_insert(ctx, "url", url);
	}

	if (carg->tls_ca_file)
	{
		json_t *ca_certificate = json_string(carg->tls_ca_file);
		json_array_object_insert(ctx, "tls_ca", ca_certificate);
	}

	if (carg->tls_cert_file)
	{
		json_t *tls_certificate = json_string(carg->tls_cert_file);
		json_array_object_insert(ctx, "tls_certificate", tls_certificate);
	}

	if (carg->tls_key_file)
	{
		json_t *tls_key = json_string(carg->tls_key_file);
		json_array_object_insert(ctx, "tls_key", tls_key);
	}

	if (carg->tls_server_name)
	{
		json_t *tls_server_name = json_string(carg->tls_server_name);
		json_array_object_insert(ctx, "tls_server_name", tls_server_name);
	}

	if (carg->parser_name)
	{
		json_t *parser_name = json_string(carg->parser_name);
		json_array_object_insert(ctx, "handler", parser_name);
	}

	if (carg->follow_redirects)
	{
		json_t *follow_redirects = json_integer(carg->follow_redirects);
		json_array_object_insert(ctx, "follow_redirects", follow_redirects);
	}

	if (carg->period)
	{
		json_t *period = json_integer(carg->period);
		json_array_object_insert(ctx, "follow_redirects", period);
	}

	if (carg->parser_handler == dns_handler)
	{
		json_t *resolve = json_string(carg->data);
		json_array_object_insert(ctx, "resolve", resolve);
	}

	if (carg->key)
	{
		json_t *key = json_string(carg->key);
		json_array_object_insert(ctx, "key", key);
	}

	if (carg->threaded_loop_name)
	{
		json_t *threaded_loop_name = json_string(carg->threaded_loop_name);
		json_array_object_insert(ctx, "threaded_loop_name", threaded_loop_name);
	}

	if (carg->log_level)
	{
		char *log = get_log_level_by_id(carg->log_level);
		json_t *log_level;
		if (!log)
			log_level =  json_integer(carg->log_level);
		else
			log_level = json_string(log);
		json_array_object_insert(ctx, "log_level", log_level);
	}

	if (carg->labels)
		alligator_ht_foreach_arg(carg->labels, labels_kv_deserialize, ctx);


	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_struct_conf_deserialize, ctx);

	if (carg->pquery) {
		json_t *pquery_arr = json_array();
		json_array_object_insert(ctx, "pquery", pquery_arr);
		for (uint8_t i = 0; i < carg->pquery_size; ++i) {
			json_t *pquery = json_string(carg->pquery[i]);
			json_array_object_insert(pquery_arr, "", pquery);
		}
	}
}

void lang_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	lang_options *lo = arg;

	json_t *lang = json_object_get(dst, "lang");
	if (!lang)
	{
		lang = json_array();
		json_array_object_insert(dst, "lang", lang);
	}

	json_t *ctx = json_object();
	json_array_object_insert(lang, NULL, ctx);
	if (lo->key)
	{
		json_t *key = json_string(lo->key);
		json_array_object_insert(ctx, "key", key);
	}

	if (lo->lang)
	{
		json_t *lang = json_string(lo->lang);
		json_array_object_insert(ctx, "lang", lang);
	}

	if (lo->method)
	{
		json_t *method = json_string(lo->method);
		json_array_object_insert(ctx, "method", method);
	}

	if (!(lo->hidden_arg) && (lo->arg))
	{
		json_t *arg = json_string(lo->arg);
		json_array_object_insert(ctx, "arg", arg);
		json_array_object_insert(ctx, "hidden_arg", json_false());
	}

	if (lo->hidden_arg)
	{
		json_array_object_insert(ctx, "hidden_arg", json_true());
	}

	if (lo->file)
	{
		json_t *file = json_string(lo->file);
		json_array_object_insert(ctx, "file", file);
	}

	if (lo->module)
	{
		json_t *module = json_string(lo->module);
		json_array_object_insert(ctx, "module", module);
	}

	if (lo->query)
	{
		json_t *query = json_string(lo->query);
		json_array_object_insert(ctx, "query", query);
	}

	if (lo->log_level)
	{
		char *log = get_log_level_by_id(lo->log_level);
		json_t *log_level;
		if (!log)
			log_level =  json_integer(lo->log_level);
		else
			log_level = json_string(log);
		json_array_object_insert(ctx, "log_level", log_level);
	}

	if (lo->conf)
	{
		json_t *conf = json_integer(lo->conf);
		json_array_object_insert(ctx, "conf", conf);
	}

	if (lo->serializer)
	{
		json_t *sertype = NULL;
		if (lo->serializer == METRIC_SERIALIZER_JSON)
			sertype = json_string("json");
		else if (lo->serializer == METRIC_SERIALIZER_OPENMETRICS)
			sertype = json_string("openmetrics");
		else if (lo->serializer == METRIC_SERIALIZER_DSV)
			sertype = json_string("dsv");
		else
			sertype = json_string("none");
		json_array_object_insert(ctx, "serializer", sertype);
	}
}

void fs_x509_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	x509_fs_t *tls_fs = arg;

	json_t *x509 = json_object_get(dst, "x509");
	if (!x509)
	{
		x509 = json_array();
		json_array_object_insert(dst, "x509", x509);
	}

	json_t *ctx = json_object();
	json_array_object_insert(x509, NULL, ctx);

	if (tls_fs->path)
	{
		json_t *path = json_string(tls_fs->path);
		json_array_object_insert(ctx, "path", path);
	}

	if (tls_fs->name)
	{
		json_t *name = json_string(tls_fs->name);
		json_array_object_insert(ctx, "name", name);
	}

	if (tls_fs->match)
	{
		json_t *match = json_array();
		json_array_object_insert(ctx, "match", match);

		for (uint64_t i = 0; i < tls_fs->match->l; ++i) {
			json_t *token = json_string(tls_fs->match->str[i]->s);
			json_array_object_insert(ctx, "", token);
		}
	}
}

void query_node_generate_field_conf(void *funcarg, void* arg)
{
	json_t *field = funcarg;
	query_field *qf = arg;

	json_t *sfield = json_string(qf->field);
	json_array_object_insert(field, NULL, sfield);
}

void query_node_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	query_node *qn = arg;

	json_t *query = json_object_get(dst, "query");
	if (!query)
	{
		query = json_array();
		json_array_object_insert(dst, "query", query);
	}

	json_t *ctx = json_object();
	json_array_object_insert(query, NULL, ctx);

	if (qn->expr)
	{
		json_t *expr = json_string(qn->expr);
		json_array_object_insert(ctx, "expr", expr);
	}

	if (qn->make)
	{
		json_t *make = json_string(qn->make);
		json_array_object_insert(ctx, "make", make);
	}

	if (qn->action)
	{
		json_t *action = json_string(qn->action);
		json_array_object_insert(ctx, "action", action);
	}

	if (qn->ns)
	{
		json_t *ns = json_string(qn->ns);
		json_array_object_insert(ctx, "ns", ns);
	}

	if (qn->datasource)
	{
		json_t *datasource = json_string(qn->datasource);
		json_array_object_insert(ctx, "datasource", datasource);
	}

	if (qn->qf_hash)
	{
		json_t *qf_hash = json_array();
		alligator_ht_foreach_arg(qn->qf_hash, query_node_generate_field_conf, qf_hash);
		json_array_object_insert(ctx, "field", qf_hash);
	}
}

void query_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	query_ds *qds = arg;

	if (!qds)
		return;

	if (!qds->hash)
		return;

	alligator_ht_foreach_arg(qds->hash, query_node_generate_conf, dst);
}

void action_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	action_node *an = arg;

	json_t *action = json_object_get(dst, "action");
	if (!action)
	{
		action = json_array();
		json_array_object_insert(dst, "action", action);
	}

	json_t *ctx = json_object();
	json_array_object_insert(action, NULL, ctx);

	if (an->expr)
	{
		json_t *expr = json_string(an->expr);
		json_array_object_insert(ctx, "expr", expr);
	}

	if (an->name)
	{
		json_t *name = json_string(an->name);
		json_array_object_insert(ctx, "name", name);
	}

	if (an->ns)
	{
		json_t *ns = json_string(an->ns);
		json_array_object_insert(ctx, "ns", ns);
	}

	//if (an->datasource)
	//{
	//	json_t *datasource = json_string(an->datasource);
	//	json_array_object_insert(ctx, "datasource", datasource);
	//}

	if (an->follow_redirects)
	{
		json_t *follow_redirects = json_integer(an->follow_redirects);
		json_array_object_insert(ctx, "follow_redirects", follow_redirects);
	}

	if (an->serializer)
	{
		json_t *sertype = NULL;
		if (an->serializer == METRIC_SERIALIZER_JSON)
			sertype = json_string("json");
		else if (an->serializer == METRIC_SERIALIZER_OPENMETRICS)
			sertype = json_string("openmetrics");
		else if (an->serializer == METRIC_SERIALIZER_CLICKHOUSE)
			sertype = json_string("clickhouse");
		else if (an->serializer == METRIC_SERIALIZER_ELASTICSEARCH)
			sertype = json_string("elasticsearch");
		else if (an->serializer == METRIC_SERIALIZER_DSV)
			sertype = json_string("dsv");
		else if (an->serializer == METRIC_SERIALIZER_CARBON2)
			sertype = json_string("carbon2");
		else if (an->serializer == METRIC_SERIALIZER_GRAPHITE)
			sertype = json_string("graphite");
		else if (an->serializer == METRIC_SERIALIZER_INFLUXDB)
			sertype = json_string("influxdb");
		else if (an->serializer == METRIC_SERIALIZER_PG)
			sertype = json_string("postgresql");
		else if (an->serializer == METRIC_SERIALIZER_CASSANDRA)
			sertype = json_string("cassandra");
		else
			sertype = json_string("none");
		json_array_object_insert(ctx, "serializer", sertype);
	}

	//if (an->type == ACTION_TYPE_SHELL)
	//	json_array_object_insert(ctx, "type", json_string("shell"));

	if (an->labels)
		alligator_ht_foreach_arg(an->labels, labels_kv_deserialize, ctx);
}

void probe_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	probe_node *pn = arg;

	json_t *probe = json_object_get(dst, "probe");
	if (!probe)
	{
		probe = json_array();
		json_array_object_insert(dst, "probe", probe);
	}

	json_t *ctx = json_object();
	json_array_object_insert(probe, NULL, ctx);

	if (pn->source_ip_address)
	{
		json_t *source_ip_address = json_string(pn->source_ip_address);
		json_array_object_insert(ctx, "source_ip_address", source_ip_address);
	}

	if (pn->body)
	{
		json_t *body = json_string(pn->body);
		json_array_object_insert(ctx, "body", body);
	}

	if (pn->prober_str)
	{
		json_t *prober_str = json_string(pn->prober_str);
		json_array_object_insert(ctx, "prober", prober_str);
	}

	if (pn->http_proxy_url)
	{
		json_t *http_proxy_url = json_string(pn->http_proxy_url);
		json_array_object_insert(ctx, "http_proxy_url", http_proxy_url);
	}

	if (pn->name)
	{
		json_t *name = json_string(pn->name);
		json_array_object_insert(ctx, "name", name);
	}

	if (pn->ca_file)
	{
		json_t *ca_file = json_string(pn->ca_file);
		json_array_object_insert(ctx, "ca_file", ca_file);
	}

	if (pn->cert_file)
	{
		json_t *cert_file = json_string(pn->cert_file);
		json_array_object_insert(ctx, "cert_file", cert_file);
	}

	if (pn->key_file)
	{
		json_t *key_file = json_string(pn->key_file);
		json_array_object_insert(ctx, "key_file", key_file);
	}

	if (pn->server_name)
	{
		json_t *server_name = json_string(pn->server_name);
		json_array_object_insert(ctx, "server_name", server_name);
	}

	if (pn->follow_redirects)
	{
		json_t *follow_redirects = json_integer(pn->follow_redirects);
		json_array_object_insert(ctx, "follow_redirects", follow_redirects);
	}

	if (pn->tls_verify)
	{
		json_t *tls_verify = json_integer(pn->tls_verify);
		json_array_object_insert(ctx, "tls_verify", tls_verify);
	}

	if (pn->timeout)
	{
		json_t *timeout = json_integer(pn->timeout);
		json_array_object_insert(ctx, "timeout", timeout);
	}

	if (pn->tls)
	{
		json_t *tls;
		if (pn->tls)
			tls = json_string("on");
		else
			tls = json_string("off");
		json_array_object_insert(ctx, "tls", tls);
	}

	if (pn->loop)
	{
		json_t *loop = json_integer(pn->loop);
		json_array_object_insert(ctx, "loop", loop);
	}

	if (pn->percent_success)
	{
		json_t *percent_success = json_real(pn->percent_success);
		json_array_object_insert(ctx, "percent", percent_success);
	}

	json_t *jmethod = NULL;
	if (pn->method == HTTP_GET)
		jmethod = json_string("GET");
	else if (pn->method == HTTP_POST)
		jmethod = json_string("POST");
	else
		jmethod = json_string("none");
	json_array_object_insert(ctx, "method", jmethod);

	if (pn->valid_status_codes_size)
	{
		json_t *valid_status_codes = json_array();
		json_array_object_insert(ctx, "valid_status_codes", valid_status_codes);
		for (uint64_t i = 0; i < pn->valid_status_codes_size; i++)
		{
			json_t *vss = json_string(pn->valid_status_codes[i]);
			json_array_object_insert(valid_status_codes, NULL, vss);
		}
	}

	if (pn->labels)
		alligator_ht_foreach_arg(pn->labels, labels_kv_deserialize, ctx);

	if (pn->env)
		alligator_ht_foreach_arg(pn->env, env_struct_conf_deserialize, ctx);
}

void puppeteer_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	puppeteer_node *pn = arg;

	json_t *puppeteer = json_object_get(dst, "puppeteer");
	if (!puppeteer)
	{
		puppeteer = json_object();
		json_array_object_insert(dst, "puppeteer", puppeteer);
	}

	if (pn->url)
	{
		json_t *url_ctx = NULL;

		if (pn->value)
		{
			json_error_t error;
			url_ctx = json_loads(pn->value, 0, &error);
		}

		if (!url_ctx)
		{
			url_ctx = json_object();
		}

		json_array_object_insert(puppeteer, pn->url->s, url_ctx);
	}
}


void cluster_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	cluster_node *cn = arg;

	json_t *cluster = json_object_get(dst, "cluster");
	if (!cluster)
	{
		cluster = json_object();
		json_array_object_insert(dst, "cluster", cluster);
	}

	json_t *name_ctx = json_string(cn->name);
	json_array_object_insert(cluster, "name", name_ctx);

	json_t *size_ctx = json_integer(cn->servers_size);
	json_array_object_insert(cluster, "size", size_ctx);

	json_t *replica_factor_ctx = json_integer(cn->replica_factor);
	json_array_object_insert(cluster, "replica_factor", replica_factor_ctx);

	json_t *servers_ctx = json_array();
	json_array_object_insert(cluster, "servers", servers_ctx);

	for (uint64_t i = 0; i < cn->servers_size; i++)
	{
		json_t *srv_ctx = json_object();
		json_t *server_name = json_string(cn->servers[i].name);
		json_t *is_me = json_integer(cn->servers[i].is_me);

		json_array_object_insert(srv_ctx, "name", server_name);
		json_array_object_insert(srv_ctx, "is_me", is_me);
		json_array_object_insert(servers_ctx, "", srv_ctx);
	}
}

void entrypoints_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	context_arg *carg = arg;

	json_t *entrypoint = json_object_get(dst, "entrypoint");
	if (!entrypoint)
	{
		entrypoint = json_array();
		json_array_object_insert(dst, "entrypoint", entrypoint);
	}

	json_t *ctx = json_object();
	json_array_object_insert(entrypoint, NULL, ctx);

	if (carg->ttl)
	{
		json_t *ttl = json_integer(carg->ttl);
		json_array_object_insert(ctx, "ttl", ttl);
	}

	if (carg->pingloop)
	{
		json_t *pingloop = json_integer(carg->pingloop);
		json_array_object_insert(ctx, "pingloop", pingloop);
	}

	if (carg->threads)
	{
		json_t *threads = json_integer(carg->threads);
		json_array_object_insert(ctx, "threads", threads);
	}

	if (carg->api_enable)
	{
		json_t *api_enable = json_integer(carg->api_enable);
		json_array_object_insert(ctx, "api", api_enable);
	}

	if (carg->key)
	{
		json_t *key = json_string(carg->key);
		json_array_object_insert(ctx, "key", key);
	}

	if (carg->cluster)
	{
		json_t *cluster = json_string(carg->cluster);
		json_array_object_insert(ctx, "cluster", cluster);
	}

	if (carg->instance)
	{
		json_t *instance = json_string(carg->instance);
		json_array_object_insert(ctx, "instance", instance);
	}

	if (carg->lang)
	{
		json_t *lang = json_string(carg->lang);
		json_array_object_insert(ctx, "lang", lang);
	}

	if (carg->metric_aggregation)
	{
		if (carg->metric_aggregation == ENTRYPOINT_AGGREGATION_COUNT)
		{
			json_t *metric_aggregation = json_string("count");
			json_array_object_insert(ctx, "metric_aggregation", metric_aggregation);
		}
	}

	if (carg->labels)
		alligator_ht_foreach_arg(carg->labels, labels_kv_deserialize, ctx);


	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_struct_conf_deserialize, ctx);
}

void system_config_get(json_t *dst)
{
	json_t *system = json_object_get(dst, "system");
	if (!system)
	{
		system = json_object();
		json_array_object_insert(dst, "system", system);
	}

	if (ac->system_base) {
		json_t *ctxsys = json_object();
		json_array_object_insert(system, "base", ctxsys);
	}

	if (ac->system_disk) {
		json_t *ctxsys = json_object();
		json_array_object_insert(system, "disk", ctxsys);
	}

	if (ac->system_network) {
		json_t *ctxsys = json_object();
		json_array_object_insert(system, "network", ctxsys);
	}

	if (ac->system_smart) {
		json_t *ctxsys = json_object();
		json_array_object_insert(system, "smart", ctxsys);
	}

	if (ac->system_interrupts) {
		json_t *ctxsys = json_object();
		json_array_object_insert(system, "interrupts", ctxsys);
	}

	if (ac->system_firewall) {
		json_t *ctxsys = json_object();
		if (ac->system_ipset_entries)
		{
			json_t *entries = json_string("entries");
			json_array_object_insert(ctxsys, "ipset", entries);
		}
		else if (ac->system_ipset)
		{
			json_t *entries = json_string("on");
			json_array_object_insert(ctxsys, "ipset", entries);
		}
		json_array_object_insert(system, "firewall", ctxsys);
	}

	if (ac->system_cadvisor) {
		json_t *ctxsys = json_object();
		json_array_object_insert(system, "cadvisor", ctxsys);
	}

	if (ac->system_packages) {
		json_t *ctxsys = json_array();
		json_array_object_insert(system, "packages", ctxsys);
	}

	if (ac->system_services) {
		json_t *ctxsys = json_array();
		json_array_object_insert(system, "services", ctxsys);
	}

	if (ac->system_process) {
		json_t *ctxsys = json_array();
		json_array_object_insert(system, "process", ctxsys);
	}

	if (ac->system_cpuavg) {
		json_t *ctxsys = json_object();
		json_array_object_insert(system, "cpuavg", ctxsys);
	}

	if (ac->system_sysfs) {
		json_t *ctxsys = json_string(ac->system_sysfs);
		json_array_object_insert(system, "sysfs", ctxsys);
	}

	if (ac->system_procfs) {
		json_t *ctxsys = json_string(ac->system_procfs);
		json_array_object_insert(system, "procfs", ctxsys);
	}

	if (ac->system_rundir) {
		json_t *ctxsys = json_string(ac->system_rundir);
		json_array_object_insert(system, "rundir", ctxsys);
	}

	if (ac->system_usrdir) {
		json_t *ctxsys = json_string(ac->system_usrdir);
		json_array_object_insert(system, "usrdir", ctxsys);
	}

	if (ac->system_etcdir) {
		json_t *ctxsys = json_string(ac->system_etcdir);
		json_array_object_insert(system, "etcdir", ctxsys);
	}
}

void userprocess_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	userprocess_node *upn = arg;

	json_t *system = json_object_get(dst, "system");
	if (!system)
	{
		system = json_object();
		json_array_object_insert(dst, "system", system);
	}

	json_t *userprocess = json_object_get(system, "userprocess");
	if (!userprocess)
	{
		userprocess = json_array();
		json_array_object_insert(system, "userprocess", userprocess);
	}

	if (upn->name)
	{
		json_t *name = json_string(upn->name);
		json_array_object_insert(userprocess, NULL, name);
	}
}

void groupprocess_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	userprocess_node *upn = arg;

	json_t *system = json_object_get(dst, "system");
	if (!system)
	{
		system = json_object();
		json_array_object_insert(dst, "system", system);
	}

	json_t *groupprocess = json_object_get(system, "groupprocess");
	if (!groupprocess)
	{
		groupprocess = json_array();
		json_array_object_insert(system, "groupprocess", groupprocess);
	}

	if (upn->name)
	{
		json_t *name = json_string(upn->name);
		json_array_object_insert(groupprocess, NULL, name);
	}
}

void sysctl_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	sysctl_node *scn = arg;

	json_t *system = json_object_get(dst, "system");
	if (!system)
	{
		system = json_object();
		json_array_object_insert(dst, "system", system);
	}

	json_t *sysctlsystem = json_object_get(system, "sysctl");
	if (!sysctlsystem)
	{
		sysctlsystem = json_array();
		json_array_object_insert(system, "sysctl", sysctlsystem);
	}

	if (scn->name)
	{
		json_t *name = json_string(scn->name);
		json_array_object_insert(sysctlsystem, NULL, name);
	}
}

void system_mapper_generate_conf(void *funcarg, void* arg)
{
	config_get_arg *cgarg = funcarg;
	json_t *dst = cgarg->dst;
	char *smapper = cgarg->arg;
	match_string *ms = arg;

	json_t *system = json_object_get(dst, "system");
	if (!system)
	{
		system = json_object();
		json_array_object_insert(dst, "system", system);
	}

	json_t *systemmapper = json_object_get(system, smapper);
	if (!systemmapper)
	{
		systemmapper = json_array();
		json_array_object_insert(system, smapper, systemmapper);
	}

	if (ms->s)
	{
		json_t *name = json_string(ms->s);
		json_array_object_insert(systemmapper, NULL, name);
	}
}

void system_pidfile_generate_conf(json_t *dst)
{
	if (!ac->system_pidfile)
		return;

	pidfile_node *node = ac->system_pidfile->head;
	if (!node)
		return;

	json_t *system = json_object_get(dst, "system");
	if (!system)
	{
		system = json_object();
		json_array_object_insert(dst, "system", system);
	}

	json_t *pidfile = NULL;
	char *pidfile_type = NULL;

	while (node)
	{
		if (node->type == 0)
		{
			pidfile_type = "pidfile";
		}
		else if (node->type == 1)
		{
			pidfile_type = "cgroup";
		}
		else
			continue;

		pidfile = json_object_get(system, pidfile_type);
		if (!pidfile)
		{
			pidfile = json_array();
			json_array_object_insert(system, pidfile_type, pidfile);
		}

		if (node->pidfile)
		{
			json_t *name = json_string(node->pidfile);
			json_array_object_insert(pidfile, NULL, name);
		}

		node = node->next;
	}
}

void modules_generate_conf(void *funcarg, void* arg)
{
	json_t *dst = funcarg;
	module_t *module = arg;

	json_t *modules = json_object_get(dst, "modules");
	if (!modules)
	{
		modules = json_object();
		json_array_object_insert(dst, "modules", modules);
	}

	if (module->path && module->key)
	{
		json_t *path = json_string(module->path);
		json_array_object_insert(modules, module->key, path);
	}
}

void resolver_generate_conf(json_t *dst)
{
	if (!ac->resolver_size)
		return;

	json_t *resolver = json_array();

	for (uint64_t i = 0; i < ac->resolver_size; ++i)
	{
		json_t *json_data = json_object();

		json_t *host = json_string(ac->srv_resolver[i]->hi->host);
		json_array_object_insert(json_data, "host", host);

		json_t *port = json_string(ac->srv_resolver[i]->hi->port);
		json_array_object_insert(json_data, "port", port);

		json_t *url = json_string(ac->srv_resolver[i]->hi->url);
		json_array_object_insert(json_data, "url", url);

		json_t *user = json_string(ac->srv_resolver[i]->hi->user);
		json_array_object_insert(json_data, "user", user);

		json_t *transport_string = json_string(ac->srv_resolver[i]->hi->transport_string);
		json_array_object_insert(json_data, "transport", transport_string);

		json_array_object_insert(resolver, NULL, json_data);
	}

	json_array_object_insert(dst, "resolver", resolver);
}

json_t *config_get()
{
	json_t *dst = json_object();
	config_get_arg cgarg;
	cgarg.dst = dst;

	config_global_get(dst);
	alligator_ht_foreach_arg(ac->aggregators, aggregator_generate_conf, dst);
	alligator_ht_foreach_arg(ac->lang_aggregator, lang_generate_conf, dst);
	alligator_ht_foreach_arg(ac->fs_x509, fs_x509_generate_conf, dst);
	alligator_ht_foreach_arg(ac->query, query_generate_conf, dst);
	alligator_ht_foreach_arg(ac->action, action_generate_conf, dst);
	alligator_ht_foreach_arg(ac->probe, probe_generate_conf, dst);
	alligator_ht_foreach_arg(ac->entrypoints, entrypoints_generate_conf, dst);
	system_config_get(dst);
	alligator_ht_foreach_arg(ac->puppeteer, puppeteer_generate_conf, dst);
	alligator_ht_foreach_arg(ac->cluster, cluster_generate_conf, dst);
	alligator_ht_foreach_arg(ac->system_userprocess,  userprocess_generate_conf, dst);
	alligator_ht_foreach_arg(ac->system_groupprocess, groupprocess_generate_conf, dst);
	alligator_ht_foreach_arg(ac->system_sysctl, sysctl_generate_conf, dst);
	alligator_ht_foreach_arg(ac->modules, modules_generate_conf, dst);
	system_pidfile_generate_conf(dst);
	resolver_generate_conf(dst);

	cgarg.arg = "process";
	alligator_ht_foreach_arg(ac->process_match->hash, system_mapper_generate_conf, &cgarg);

	cgarg.arg = "packages";
	alligator_ht_foreach_arg(ac->packages_match->hash, system_mapper_generate_conf, &cgarg);

	cgarg.arg = "services";
	alligator_ht_foreach_arg(ac->services_match->hash, system_mapper_generate_conf, &cgarg);

	return dst;
}

string *config_get_string()
{
	string *ret = NULL;
	json_t *dst = config_get();

	char *dvalue = json_dumps(dst, JSON_INDENT(1));
	if (dvalue)
	{
		size_t dvalsize = strlen(dvalue);
		ret = string_init_add(dvalue, dvalsize, dvalsize);
	}

	json_decref(dst);

	return ret;
}
