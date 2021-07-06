#include <uv.h>
#include "common/url.h"
#include "events/context_arg.h"
#include "events/client.h"
#include "main.h"
#include "parsers/multiparser.h"
#include "parsers/elasticsearch.h"
#include "parsers/mysql.h"
#include "parsers/postgresql.h"
#include "parsers/mongodb.h"
#include "events/filetailer.h"
#include "events/udp.h"
#include "events/client.h"
#include "events/process.h"
#include "dynconf/sd.h"
char* icmp_client(context_arg *carg);

int smart_aggregator_default_key(char *key, char* transport_string, char* parser_name, char* host, char* port, char *query)
{
	return snprintf(key, 254, "%s:%s:%s:%s%s%s", transport_string, parser_name, host, port, *query == '/' ? "" : "/", query);
}

int smart_aggregator(context_arg *carg)
{
	char *type = NULL;

	char key[255];
	if (!carg->key)
	{
		unsigned int sin_port = carg->dest ? htons(carg->dest->sin_port) : 0;
		snprintf(key, 254, "%s:%s:%s:%u", carg->transport_string, carg->parser_name, carg->host, sin_port);
		smart_aggregator_default_key(key, carg->transport_string, carg->parser_name, carg->host, carg->port, carg->query_url);
	}
	else
		strlcpy(key, carg->key, 255);

	if (ac->log_level > 0)
		printf("smart_aggregator key: '%s'/%d\n", key, carg->transport);

	if (tommy_hashdyn_search(ac->aggregators, aggregator_compare, key, tommy_strhash_u32(0, key)))
	{
		if (ac->log_level > 2)
			printf("smart_aggregator: key '%s' already in use\n", key);
		return 0; // err, need for carg_free
	}

	if (carg->transport == APROTO_UNIX)
		type = unix_tcp_client(carg);
	else if (carg->transport == APROTO_TCP)
		type = tcp_client(carg);
	else if (carg->transport == APROTO_TLS)
		type = tcp_client(carg);
	else if (carg->transport == APROTO_UDP)
		type = udp_client(carg);
	else if (carg->transport == APROTO_ICMP)
		type = icmp_client(carg);
	else if (carg->transport == APROTO_PROCESS)
		type = process_client(carg);
	//else if (carg->proto == APROTO_UNIXGRAM)
	//	do_unixgram_client_carg(carg);
	else if (carg->transport == APROTO_FILE)
		type = filetailer_handler(carg);
	else if (carg->transport == APROTO_PG)
		type = postgresql_client(carg);
	else if (carg->transport == APROTO_MY)
		type = mysql_client(carg);
	else if (carg->transport == APROTO_MONGODB)
		type = mongodb_client(carg);
	else if (carg->transport == APROTO_ZKCONF)
		type = zk_client(carg);

	if (type && !carg->key)
	{
		carg->key = strdup(key);
		//snprintf(carg->key, 254, "%s:%s:%s", type, carg->parser_name, carg->url);
	}

	if (carg->key)
		tommy_hashdyn_insert(ac->aggregators, &(carg->context_node), carg, tommy_strhash_u32(0, carg->key));

	return 1; // OK
}

void smart_aggregator_del(context_arg *carg)
{
	if (carg->transport == APROTO_UNIX)
		unix_tcp_client_del(carg);
	else if (carg->transport == APROTO_TCP)
		tcp_client_del(carg);
	else if (carg->transport == APROTO_TLS)
		tcp_client_del(carg);
	else if (carg->transport == APROTO_UDP)
		udp_client_del(carg);
	else if (carg->transport == APROTO_ICMP)
		icmp_client_del(carg);
	else if (carg->transport == APROTO_PROCESS)
		process_client_del(carg);
	//else if (carg->proto == APROTO_UNIXGRAM)
	//	do_unixgram_client_carg(carg);
	else if (carg->transport == APROTO_FILE)
		filetailer_handler_del(carg);
	else if (carg->transport == APROTO_PG)
		postgresql_client_del(carg);
	else if (carg->transport == APROTO_MY)
		mysql_client_del(carg);
	else if (carg->transport == APROTO_MONGODB)
		mongodb_client_del(carg);
}

void smart_aggregator_del_key(char *key)
{
	context_arg *carg = tommy_hashdyn_search(ac->aggregators, aggregator_compare, key, tommy_strhash_u32(0, key));
	if (carg)
		smart_aggregator_del(carg);
}

void smart_aggregator_del_key_gen(char *transport_string, char *parser_name, char *host, char *port, char *query)
{
	char key[255];
	smart_aggregator_default_key(key, transport_string, parser_name, host, port, query);
	smart_aggregator_del_key(key);
}

void try_again(context_arg *carg, char *mesg, size_t mesg_len, void *handler, char *parser_name, void *validator, char *override_key, void *data)
{
	host_aggregator_info *hi = parse_url(carg->url, strlen(carg->url));
	tommy_hashdyn *newenv = env_struct_duplicate(carg->env);
	context_arg *new = context_arg_json_fill(NULL, hi, handler, parser_name, mesg, mesg_len, data, validator, 0, ac->loop, newenv, carg->follow_redirects, carg->stdin_s, carg->stdin_l);

	new->key = override_key;
	if (!new->key)
	{
		new->key = malloc(64);
		snprintf(new->key, 64, "%s(tcp://%s:%u)", parser_name, carg->host, htons(carg->dest->sin_port));
	}

	r_time time = setrtime();
	new->context_ttl = time.sec;
	new->log_level = carg->log_level;

	if (ac->log_level > 2)
		printf("try_again allocated context argument %p with hostname '%s' with mesg '%s'\n", carg, carg->host, carg->mesg);

	if (!smart_aggregator(new))
		carg_free(new);
}

void aggregator_oneshot(context_arg *carg, char *url, size_t url_len, char *mesg, size_t mesg_len, void *handler, char *parser_name, void *validator, char *override_key, uint64_t follow_redirects, void *data, char *s_stdin, size_t l_stdin)
{
	host_aggregator_info *hi = parse_url(url, url_len);

	tommy_hashdyn *newenv = NULL;
	if (carg)
		newenv = env_struct_duplicate(carg->env);

	context_arg *new = context_arg_json_fill(NULL, hi, handler, parser_name, mesg, mesg_len, data, validator, 0, ac->loop, newenv, follow_redirects, s_stdin, l_stdin);

	new->key = override_key;
	if (!new->key)
	{
		new->key = malloc(64);
		snprintf(new->key, 64, "%s(%s://%s:%s)", parser_name, hi->transport_string, hi->host, hi->port);
	}

	r_time time = setrtime();
	new->context_ttl = time.sec;

	new->log_level = carg? carg->log_level : ac->log_level;

	if (ac->log_level > 2)
		printf("try_again allocated context argument %p with hostname '%s' with mesg '%s'\n", carg, carg->host, carg->mesg);

	if (!smart_aggregator(new))
		carg_free(new);
}

int actx_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((aggregate_context*)obj)->key;
        return strcmp(s1, s2);
}

void aggregate_ctx_init()
{
	ac->aggregate_ctx = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->aggregate_ctx);

	redis_parser_push();
	elasticsearch_parser_push();
	gearmand_parser_push();
	beanstalkd_parser_push();
	eventstore_parser_push();
	clickhouse_parser_push();
	flower_parser_push();
	gdnsd_parser_push();
	hadoop_parser_push();
	sd_etcd_parser_push();
	sd_consul_configuration_parser_push();
	sd_consul_discovery_parser_push();
	sd_zk_parser_push();
	nginx_upstream_check_parser_push();
	json_parser_push();
	consul_parser_push();
	prometheus_metrics_parser_push();
	sentinel_parser_push();
	opentsdb_parser_push();
	varnish_parser_push();
	rabbitmq_parser_push();
	uwsgi_parser_push();
	haproxy_parser_push();
	memcached_parser_push();
	aerospike_parser_push();
	monit_parser_push();
	unbound_parser_push();
	syslog_ng_parser_push();
	zookeeper_parser_push();
	ntpd_parser_push();
	ipmi_parser_push();
	pg_parser_push();
	pgbouncer_parser_push();
	odyssey_parser_push();
	pgpool_parser_push();
	patroni_parser_push();
	mysql_parser_push();
	sphinxsearch_parser_push();
	mongodb_parser_push();
	http_parser_push();
	tcp_parser_push();
	tftp_parser_push();
	blackbox_parser_push();
	squid_parser_push();
	process_parser_push();
	riak_parser_push();
	nats_parser_push();
	lighttpd_parser_push();
	httpd_parser_push();
	nsd_parser_push();
	dummy_parser_push();
	named_parser_push();
	jmx_parser_push();
	jks_parser_push();
	kubernetes_ingress_parser_push();
	kubernetes_endpoint_parser_push();
	kubeconfig_parser_push();
	oracle_parser_push();
	oracle_query_parser_push();
	couchdb_parser_push();
	couchbase_parser_push();
	druid_parser_push();
	druid_worker_parser_push();
	druid_historical_parser_push();
	druid_broker_parser_push();
	mogilefs_parser_push();
	moosefs_parser_push();
}

int aggregator_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((context_arg*)obj)->key;
	return strcmp(s1, s2);
}
