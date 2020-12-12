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

void smart_aggregator(context_arg *carg)
{
	char *type = NULL;
	char key[255];
	unsigned int sin_port = carg->dest ? htons(carg->dest->sin_port) : 0;
	snprintf(key, 254, "%s:%s:%s:%u", carg->transport_string, carg->parser_name, carg->host, sin_port);
	if (tommy_hashdyn_search(ac->aggregators, aggregator_compare, key, tommy_strhash_u32(0, key)))
	{
		printf("smart_aggregator: key '%s' already in use\n", key);
		return;
	}

	if (carg->transport == APROTO_UNIX)
		type = unix_tcp_client(carg);
	else if (carg->transport == APROTO_TCP)
		type = tcp_client(carg);
	else if (carg->transport == APROTO_TLS)
		type = tcp_client(carg);
	else if (carg->transport == APROTO_UDP)
		type = udp_client(carg);
	//else if (carg->transport == APROTO_ICMP)
	//	unix_tcp_client(carg);
	else if (carg->transport == APROTO_PROCESS)
		type = process_client(carg);
	//else if (carg->proto == APROTO_UNIXGRAM)
	//	do_unixgram_client_carg(carg);
	else if (carg->transport == APROTO_FILE)
		type = filetailer_handler(carg->host, NULL);
	else if (carg->transport == APROTO_PG)
		type = postgresql_client(carg);
	else if (carg->transport == APROTO_MY)
		type = mysql_client(carg);
	else if (carg->transport == APROTO_MONGODB)
		type = mongodb_client(carg);

	if (type)
	{
		carg->key = strdup(key);
		//snprintf(carg->key, 254, "%s:%s:%s", type, carg->parser_name, carg->url);
	}
	if (carg->key)
		tommy_hashdyn_insert(ac->aggregators, &(carg->context_node), carg, tommy_strhash_u32(0, carg->key));
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
		udp_client(carg);
	//else if (carg->transport == APROTO_ICMP)
	//	unix_tcp_client(carg);
	else if (carg->transport == APROTO_PROCESS)
		process_client(carg);
	//else if (carg->proto == APROTO_UNIXGRAM)
	//	do_unixgram_client_carg(carg);
	else if (carg->transport == APROTO_FILE)
		filetailer_handler("/var/log/", NULL);
	else if (carg->transport == APROTO_PG)
		postgresql_client(carg);
	else if (carg->transport == APROTO_MY)
		mysql_client(carg);
	else if (carg->transport == APROTO_MONGODB)
		mongodb_client(carg);
}

void try_again(context_arg *carg, char *mesg, size_t mesg_len, void *handler, char *parser_name)
{
	context_arg *new = calloc(1, sizeof(*new));
	memcpy(new, carg, sizeof(*new));

	r_time time = setrtime();
	new->context_ttl = time.sec;

	new->lock = 0;
	new->parser_handler = handler;
	new->parser_name = parser_name;
	new->key = malloc(64);
	new->buffer_request_size = 6553500;
	new->buffer_response_size = 6553500;
	new->full_body = string_init(6553500);
	//new->tt_timer = malloc(sizeof(uv_timer_t));
	//new->dest = malloc(sizeof(*new->dest));
	//memcpy(new->dest, carg->dest, sizeof(*new->dest));
	snprintf(new->key, 64, "%s(tcp://%s:%u)", parser_name, carg->host, htons(carg->dest->sin_port));

	aconf_mesg_set(new, mesg, mesg_len);
	//printf("CREATE try_again %p:%p -> %s\n", new, new->mesg, new->key);

	if (new->transport == APROTO_UNIX)
		unix_client_connect(new);
	if (new->transport == APROTO_TCP)
		tcp_client_connect(new);
	else if (new->transport == APROTO_UDP)
		udp_client_connect(new);
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
	mongodb_parser_push();
	http_parser_push();
	tcp_parser_push();
	tftp_parser_push();
	blackbox_parser_push();
	squid_parser_push();
	process_parser_push();
	riak_parser_push();
	nats_parser_push();
}

int aggregator_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((context_arg*)obj)->key;
	return strcmp(s1, s2);
}
