#include <stdio.h>
#include "main.h"
#include "common/url.h"
#include "common/http.h"
#include "common/fastcgi.h"
#include "modules/modules.h"
#include "modules/postgresql.h"
#include "parsers/multiparser.h"
#include "parsers/elasticsearch.h"
#include "common/netlib.h"
#include "common/base64.h"
#include "common/rpm.h"
#include "common/json_parser.h"
#include "dynconf/sd.h"

int8_t config_compare(mtlen *mt, int64_t i, char *str, size_t size)
{
	uint64_t j;
	char *cstr = mt->st[i].s;
	size_t cstr_size = mt->st[i].l;

	if (size != cstr_size)
	{
		//puts("0FALSE");
		return 0;
	}

	for (j=0; j<cstr_size && str[j] == cstr[j]; j++);

	if (j == cstr_size)
	{
		//puts("TRUE");
		return 1;
	}
	else
	{
		//puts("1FALSE");
		return 0;
	}
}

size_t config_get_field_size(mtlen *mt, int64_t i)
{
	size_t j;
	for (j=0; !config_compare(mt, i+j, "}", 1) && !config_compare(mt, i+j, "{", 1) && !config_compare(mt, i+j, ";", 1); j++);
	return j;
}

int8_t config_compare_begin(mtlen *mt, int64_t i, char *str, size_t size)
{
	//printf("compare %s and %s\n", mt->st[i].s, str);
	if ((i > 0) && (config_compare(mt, i-1, "{", 1) || config_compare(mt, i-1, "}", 1) || config_compare(mt, i-1, ";", 1)))
		return config_compare(mt, i, str, size);
	else if (i == 0)
		return config_compare(mt, i, str, size);
	return 0;
}

char *config_get_arg(mtlen *mt, int64_t i, int64_t num, size_t *out_size)
{
	int64_t j = 0;
	for (j=0; j<num && !config_compare(mt, i+j, "}", 1) && !config_compare(mt, i+j, "{", 1) && !config_compare(mt, i+j, ";", 1); ++j);
	if (j == num)
	{
		if (out_size)
			*out_size = mt->st[i+j].l;
		return mt->st[i+j].s;
	}
	else
	{
		if (out_size)
			*out_size = 0;
		return NULL;
	}
}

context_arg* context_arg_fill(mtlen *mt, int64_t *i, host_aggregator_info *hi, void *handler, char *mesg, size_t mesg_len, void *data, void *expect_function, uv_loop_t *loop)
{
	extern aconf* ac;

	context_arg *carg = calloc(1, sizeof(*carg));
	if (ac->log_level > 2)
		printf("allocated context argument with addr %p with hostname '%s' with mesg '%s'\n", carg, carg->hostname, carg->mesg);

	if (!mesg_len && mesg)
		mesg_len = strlen(mesg);

	carg->hostname = hi->host; // old scheme
	strlcpy(carg->host, hi->host, 1024); // new scheme
	strlcpy(carg->port, hi->port, 6);
	if (carg->timeout < 1)
		carg->timeout = 5000;
	carg->proto = hi->proto;
	carg->transport = hi->transport;
	carg->tls = hi->tls;
	carg->loop = loop;
	carg->parser_handler = handler;
	carg->mesg = mesg; // old scheme
	carg->mesg_len = mesg_len; // old scheme
	carg->request_buffer = uv_buf_init(mesg, mesg_len); // new scheme
	carg->buffer = malloc(sizeof(uv_buf_t));
	//*(carg->buffer) = uv_buf_init(carg->mesg, carg->mesg_len);
	carg->buffer->base = carg->mesg; // old scheme
	carg->buffer->len = carg->mesg_len; // old scheme
	carg->buflen = 1;
	carg->expect_function = expect_function;
	carg->data = data;
	carg->tt_timer = malloc(sizeof(uv_timer_t));
	carg->write = 2;
	carg->full_body = string_init(6553500);
	if (hi->proto == APROTO_HTTPS || hi->proto == APROTO_TLS)
		carg->tls = 1;
	else
		carg->tls = 0;

	if (mesg)
		carg->write = 1;
	else
		carg->write = 0;

	uint8_t n = 1;
	char *ptr;
	ptr = mt->st[*i].s;
	for (n=1; ptr && strncmp(ptr, ";", 1); ++n)
	{
		ptr = config_get_arg(mt, *i, n, NULL);
		if (!ptr)
			continue;

		if (!strncmp(ptr, "tls_certificate", 15))
		{
			char *ptrval = ptr + strcspn(ptr+15, "=") + 15;
			ptrval += strspn(ptrval, "= ");
			carg->tls_cert_file = strdup(ptrval);
		}
		else if (!strncmp(ptr, "tls_key", 7))
		{
			char *ptrval = ptr + strcspn(ptr+7, "=") + 7;
			ptrval += strspn(ptrval, "= ");
			carg->tls_key_file = strdup(ptrval);
		}
		else if (!strncmp(ptr, "tls_ca", 6))
		{
			char *ptrval = ptr + strcspn(ptr+6, "=") + 6;
			ptrval += strspn(ptrval, "= ");
			carg->tls_ca_file = strdup(ptrval);
		}
		else if (!strncmp(ptr, "timeout", 7))
		{
			char *ptrval = ptr + strcspn(ptr+7, "=") + 7;
			ptrval += strspn(ptrval, "= ");
			carg->timeout = strtoull(ptrval, NULL, 10);
		}
		if (!strncmp(ptr, "add_label", 9))
		{
			char *name;
			char *key;
			char *ptrval1 = ptr + strcspn(ptr+9, "=") + 9;
			ptrval1 += strspn(ptrval1, "= ");

			char *ptrval2 = ptrval1 + strcspn(ptrval1, ": ");
			name = strndup(ptrval1, ptrval2-ptrval1);

			ptrval2 += strspn(ptrval2, ": ");
			key = ptrval2;

			if (!carg->labels)
			{
				carg->labels = malloc(sizeof(tommy_hashdyn));
				tommy_hashdyn_init(carg->labels);
			}

			labels_hash_insert_nocache(carg->labels, name, key);
			free(name);
		}
	}
	*i += n;

	return carg;
}

void context_aggregate_parser(mtlen *mt, int64_t *i)
{
	if ( *i == 0 )
		*i += 1;

	extern aconf *ac;

	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		if (!(mt->st[*i].l))
			continue;

		//printf("%"d64": %s\n", *i, mt->st[*i].s);
		if (!strcmp(mt->st[*i-1].s, "http"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 0);
			context_arg *carg = context_arg_fill(mt, i, hi, http_proto_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
#ifdef __linux__
		else if (!strcmp(mt->st[*i-1].s, "icmp"))
		{
			do_icmp_client(mt->st[*i].s);
		}
#endif
		else if (!strcmp(mt->st[*i-1].s, "process"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			put_to_loop_cmd(hi->host, NULL);
		}
		else if (!strcmp(mt->st[*i-1].s, "unix"))
		{
			//host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			//do_unix_client(hi->host, NULL, "", APROTO_UNIX);
		}
		else if (!strcmp(mt->st[*i-1].s, "unixgram"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_unixgram_client(hi->host, NULL, "");
		}
		else if (!strcmp(mt->st[*i-1].s, "clickhouse"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			int64_t g = *i;

			char *query = gen_http_query(0, hi->query, "/?query=select%20metric,value\%20from\%20system.metrics", hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, &g, hi, clickhouse_system_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			query = gen_http_query(0, hi->query, "/?query=select%20metric,value\%20from\%20system.asynchronous_metrics", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, clickhouse_system_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
                        
			g = *i;
			query = gen_http_query(0, hi->query, "/?query=select%20event,value%20from\%20system.events", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, clickhouse_system_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
                        
			g = *i;
			query = gen_http_query(0, hi->query, "/?query=select%20database,table,name,data_compressed_bytes,data_uncompressed_bytes,marks_bytes%20from\%20system.columns%20where%20database!=%27system%27", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, clickhouse_columns_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
                        
			g = *i;
			query = gen_http_query(0, hi->query, "/?query=select%20name,bytes_allocated,query_count,hit_rate,element_count,load_factor%20from\%20system.dictionaries", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, clickhouse_dictionary_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
                        
			g = *i;
			query = gen_http_query(0, hi->query, "/?query=select%20database,is_mutation,table,progress,num_parts,total_size_bytes_compressed,total_size_marks,bytes_read_uncompressed,rows_read,bytes_written_uncompressed,rows_written%20from\%20system.merges", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, clickhouse_merges_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
                        
			g = *i;
			query = gen_http_query(0, hi->query, "/?query=select%20database,table,is_leader,is_readonly,future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,log_pointer,total_replicas,active_replicas%20from\%20system.replicas", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, clickhouse_replicas_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
			*i = g;
		}
		else if (!strcmp(mt->st[*i-1].s, "redis"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char* query = malloc(1000);
			if (hi->pass)
				snprintf(query, 1000, "AUTH %s\r\nINFO ALL\r\n", hi->pass);
			else
				snprintf(query, 1000, "INFO ALL\n");

			context_arg *carg = context_arg_fill(mt, i, hi, redis_handler, query, 0, NULL, redis_validator, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "sentinel"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char* query = malloc(1000);
			if (hi->pass)
				snprintf(query, 1000, "AUTH %s\r\nINFO\r\n", hi->pass);
			else
				snprintf(query, 1000, "INFO\n");

			context_arg *carg = context_arg_fill(mt, i, hi, sentinel_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "memcached"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			context_arg *carg = context_arg_fill(mt, i, hi, memcached_handler, "stats\n", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "beanstalkd"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			context_arg *carg = context_arg_fill(mt, i, hi, beanstalkd_handler, "stats\r\n", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "aerospike"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			int64_t g = *i;

			context_arg *carg = context_arg_fill(mt, &g, hi, aerospike_statistics_handler, "\2\1\0\0\0\0\0\0", 8, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			//buffer = malloc(sizeof(*buffer));
			//*buffer = uv_buf_init("\2\1\0\0\0\0\0\vnamespaces\n", 20);
			//smart_aggregator_selector_buffer(hi, aerospike_get_namespaces_handler, buffer, 1);

			//buffer = malloc(sizeof(*buffer));
			//*buffer = uv_buf_init("\2\1\0\0\0\0\0\7status\n", 16);
			//smart_aggregator_selector_buffer(hi, aerospike_status_handler, buffer, 1);

			g = *i;
			carg = context_arg_fill(mt, &g, hi, aerospike_status_handler, "\2\1\0\0\0\0\0\7status\n", 16, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			++*i;
			while (!config_compare(mt, *i, ";", 1))
			{
				//buffer = malloc(sizeof(*buffer));
				char *aeronamespace = malloc(255);
				aeronamespace[0] = 2;
				aeronamespace[1] = 1;
				aeronamespace[2] = 0;
				aeronamespace[3] = 0;
				aeronamespace[4] = 0;
				aeronamespace[5] = 0;
				aeronamespace[6] = 0;
				aeronamespace[7] = '\13' + mt->st[*i].l;
				snprintf(aeronamespace+8, 255-8, "namespace/%s\n", mt->st[*i].s);
				g = *i;
				carg = context_arg_fill(mt, &g, hi, aerospike_namespace_handler, aeronamespace, 20 + mt->st[*i].l, NULL, NULL, ac->loop);
				smart_aggregator(carg);
				++*i;
			}
			*i = g;
		}
		else if (!strcmp(mt->st[*i-1].s, "zookeeper"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			int64_t g = *i;

			context_arg *carg = context_arg_fill(mt, &g, hi, zookeeper_mntr_handler, "mntr", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			carg = context_arg_fill(mt, &g, hi, zookeeper_isro_handler, "isro", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			carg = context_arg_fill(mt, &g, hi, zookeeper_wchs_handler, "wchs", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "gearmand"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			context_arg *carg = context_arg_fill(mt, i, hi, gearmand_handler, "status\n", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "uwsgi"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			context_arg *carg = context_arg_fill(mt, i, hi, uwsgi_handler, NULL, 0, NULL, json_validator, ac->loop);
			smart_aggregator(carg);
		}
		//else if (!strcmp(mt->st[*i-1].s, "php-fpm"))
		//{
		//	host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
		//	size_t buf_len = 5;

		//	uv_buf_t *buffer = gen_fcgi_query(hi->query, hi->host, "alligator", NULL, &buf_len);

		//	smart_aggregator_selector_buffer(hi, php_fpm_handler, buffer, buf_len);
		//}
		else if (!strcmp(mt->st[*i-1].s, "eventstore"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			int64_t g = *i;

			char *stats_query = gen_http_query(0, hi->query, "/stats", hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, &g, hi, eventstore_stats_handler, stats_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *projections_query = gen_http_query(0, hi->query, "/projections/any", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, eventstore_projections_handler, projections_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *info_query = gen_http_query(0, hi->query, "/info", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, eventstore_info_handler, info_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
			*i = g;
		}
		else if (!strcmp(mt->st[*i-1].s, "flower"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char *workers_query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, i, hi, flower_handler, workers_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "powerdns"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *header = malloc(100);
			char *query = malloc(255);
			*header = 0;
			if (mt->st[*i+1].s[0] != ';' && (!strncmp(mt->st[*i+1].s,"header=",7)))
			{
				size_t key_size = strcspn(mt->st[*i+1].s+7, ":");
				strlcpy(header, mt->st[*i+1].s+7, key_size+1);
				header[key_size] = ':';
				header[key_size+1] = ' ';
				header[key_size+2] = 0;
				strlcpy(header+key_size+2, mt->st[*i+1].s+key_size+8, mt->st[*i+1].l-7-key_size);
			}
			snprintf(query, 255, "GET /api/v1/servers/localhost/statistics HTTP/1.1\r\nHost: localhost:8081\r\nUser-Agent: alligator\r\n%s\r\n\r\n", header);
			//char *query = gen_http_query(0, hi->query, hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, i, hi, powerdns_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "opentsdb"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, i, hi, opentsdb_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "kamailio"))
		{
			//host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			//smart_aggregator_selector(hi, kamailio_handler, "{\"jsonrpc\": \"2.0\", \"method\": \"stats.get_statistics\", \"params\": [\"all\"]}");
		}
		else if (!strcmp(mt->st[*i-1].s, "haproxy"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			int64_t g = *i;

			context_arg *carg = context_arg_fill(mt, &g, hi, haproxy_info_handler, "show info\n", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			carg = context_arg_fill(mt, &g, hi, haproxy_pools_handler, "show pools\n", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			carg = context_arg_fill(mt, &g, hi, haproxy_stat_handler, "show stat\n", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			carg = context_arg_fill(mt, &g, hi, haproxy_sess_handler, "show sess\n", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			carg = context_arg_fill(mt, &g, hi, haproxy_table_handler, "show table\n", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
			*i = g;
		}
		else if (!strcmp(mt->st[*i-1].s, "nats"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			int64_t g = *i;

			char *connz_query = gen_http_query(0, hi->query, "/connz", hi->host, "alligator", hi->auth, 0);
			context_arg *carg = context_arg_fill(mt, &g, hi, nats_connz_handler, connz_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *subsz_query = gen_http_query(0, hi->query, "/subsz", hi->host, "alligator", hi->auth, 0);
			carg = context_arg_fill(mt, &g, hi, nats_subsz_handler, subsz_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *varz_query = gen_http_query(0, hi->query, "/varz", hi->host, "alligator", hi->auth, 0);
			carg = context_arg_fill(mt, &g, hi, nats_varz_handler, varz_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *routez_query = gen_http_query(0, hi->query, "/routez", hi->host, "alligator", hi->auth, 0);
			carg = context_arg_fill(mt, &g, hi, nats_routez_handler, routez_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
			*i = g;
		}
		else if (!strcmp(mt->st[*i-1].s, "riak"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char *http_query = gen_http_query(0, hi->query, "/stats", hi->host, "alligator", hi->auth, 0);
			context_arg *carg = context_arg_fill(mt, i, hi, riak_handler, http_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "rabbitmq"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			int64_t g = *i;

			char *overview_query = gen_http_query(0, hi->query, "/api/overview", hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, &g, hi, rabbitmq_overview_handler, overview_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *nodes_query = gen_http_query(0, hi->query, "/api/nodes", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, rabbitmq_nodes_handler, nodes_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *exchanges_query = gen_http_query(0, hi->query, "/api/exchanges", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, rabbitmq_exchanges_handler, exchanges_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			// !!!dynamic!!!
			char *connections_query = gen_http_query(0, hi->query, "/api/connections", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, rabbitmq_connections_handler, connections_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			// !!!dynamic!!!
			char *channels_query = gen_http_query(0, hi->query, "/api/channels", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, rabbitmq_channels_handler, channels_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *queues_query = gen_http_query(0, hi->query, "/api/queues", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, rabbitmq_queues_handler, queues_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *vhosts_query = gen_http_query(0, hi->query, "/api/vhosts", hi->host, "alligator", hi->auth, 1);
			carg = context_arg_fill(mt, &g, hi, rabbitmq_vhosts_handler, vhosts_query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
			*i = g;
		}
		else if (!strcmp(mt->st[*i-1].s, "nginx_upstream_check"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, "?format=csv", hi->host, "alligator", hi->auth, 0);
			context_arg *carg = context_arg_fill(mt, i, hi, nginx_upstream_check_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (config_compare_begin(mt, *i-1, "dummy", 5))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 0);
			context_arg *carg = context_arg_fill(mt, i, hi, dummy_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "elasticsearch"))
		{
			elastic_settings *data = calloc(1, sizeof(*data));
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			context_arg *carg;

			int64_t g = *i;
			char *nodes_query = gen_http_query(0, hi->query, "/_nodes/stats", hi->host, "alligator", hi->auth, 0);
			carg = context_arg_fill(mt, &g, hi, elasticsearch_nodes_handler, nodes_query, 0, data, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *cluster_query = gen_http_query(0, hi->query, "/_cluster/stats", hi->host, "alligator", hi->auth, 0);
			carg = context_arg_fill(mt, &g, hi, elasticsearch_cluster_handler, cluster_query, 0, data, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *health_query = gen_http_query(0, hi->query, "/_cluster/health?level=shards", hi->host, "alligator", hi->auth, 0);
			carg = context_arg_fill(mt, &g, hi, elasticsearch_health_handler, health_query, 0, data, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *index_query = gen_http_query(0, hi->query, "/_stats", hi->host, "alligator", hi->auth, 0);
			carg = context_arg_fill(mt, &g, hi, elasticsearch_index_handler, index_query, 0, data, NULL, ac->loop);
			smart_aggregator(carg);

			g = *i;
			char *settings_query = gen_http_query(0, hi->query, "/_settings", hi->host, "alligator", hi->auth, 0);
			carg = context_arg_fill(mt, &g, hi, elasticsearch_settings_handler, settings_query, 0, data, NULL, ac->loop);
			smart_aggregator(carg);
			*i = g;
		}
		else if (!strcmp(mt->st[*i-1].s, "monit"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, "/_status?format=xml&level=full", hi->host, "alligator", hi->auth, 0);
			context_arg *carg = context_arg_fill(mt, i, hi, monit_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "gdnsd"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			//uv_buf_t *buffer;

			//buffer = malloc(sizeof(*buffer));
			//*buffer = uv_buf_init("S\0\0\0\0\0\0\0", 8);
			//smart_aggregator_selector_buffer(hi, gdnsd_handler, buffer, 1);

			context_arg *carg = context_arg_fill(mt, i, hi, gdnsd_handler, "S\0\0\0\0\0\0\0", 8, NULL, gdnsd_validator, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "syslog-ng"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			context_arg *carg = context_arg_fill(mt, i, hi, syslog_ng_handler, "STATS\n", 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "consul"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, i, hi, consul_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "nifi"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 0);
			context_arg *carg = context_arg_fill(mt, i, hi, nifi_handler, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "varnish"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			context_arg *carg = context_arg_fill(mt, i, hi, varnish_handler, NULL, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "prometheus_metrics"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, i, hi, NULL, query, 0, NULL, NULL, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "jsonparse"))
		{
			puts("jsonparse");
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, i, hi, json_handler, query, 0, NULL, json_check, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "hadoop"))
		{
			puts("hadoop");
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1);
			context_arg *carg = context_arg_fill(mt, i, hi, hadoop_handler, query, 0, NULL, json_check, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "unbound"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			context_arg *carg = context_arg_fill(mt, i, hi, unbound_handler, "UBCT1 stats_noreset\n", 0, NULL, unbound_validator, ac->loop);
			smart_aggregator(carg);
		}
		else if (!strcmp(mt->st[*i-1].s, "tls_fs"))
		{
			puts("tls_fs");
			char *dir = config_get_arg(mt, *i, 0, NULL);
			char *match = config_get_arg(mt, *i, 1, NULL);
			tls_fs_recurse(dir, match);
			*i += 2;
		}
		else if (!strcmp(mt->st[*i-1].s, "period"))
		{
			int64_t repeat = atoll(mt->st[*i].s);
			if (repeat > 0)
				ac->aggregator_repeat = repeat;
		}
		else if (!strcmp(mt->st[*i-1].s, "postgresql"))
		{
			//host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			//func(hi->host);
			//do_pg_client();
		}
	}
}

typedef struct loghandler_key {
	char *key;

	tommy_node node;
} loghandler_key;

int log_handler_comparer(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((loghandler_key*)obj)->key;
        return strcmp(s1, s2);
}

void context_log_handler_parser(mtlen *mt, int64_t *i)
{
	*i += 1;

	if (mt->st[*i].s[0] == ';')
	{
		return;
	}
	else if (mt->st[*i].s[0] == '{' || mt->st[*i+1].s[0] == '{')
	{
		int templateflag = 0;
		char *template = NULL;
		for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
		{
			//printf("\n========\n> '%s' (%d)\n", mt->st[*i].s, mt->st[*i].l);
			if(!strncmp(mt->st[*i-1].s, "template", 7))
			{
				puts("template");
				templateflag = 1;
				template = malloc(10000);
				strlcpy(template, mt->st[*i].s, mt->st[*i].l);
				
				//tommy_hashdyn *hash = malloc(sizeof(*hash));
				//tommy_hashdyn_init(hash);

				//char *key = strdup(mt->st[*i].s);
				//uint32_t key_hash = tommy_strhash_u32(0, key);
				//loghandler_key *lh_key = tommy_hashdyn_search(hash, log_handler_comparer, key, key_hash);
				//if (!lh_key)
				//{
				//	lh_key = malloc(sizeof(*lh_key));
				//	lh_key->key = key;
				//	printf("insert %s\n", key);
				//	tommy_hashdyn_insert(hash, &(lh_key->node), lh_key, key_hash);
				//}
			}
			else if (templateflag && mt->st[*i].s[0] == ';' && mt->st[*i].l == 1)
			{
				puts("templateflaf_end");
				templateflag = 0;
			}
			else if (templateflag && mt->st[*i].s[0] != ';')
			{
				puts("templateflag");
				strncat(template, mt->st[*i].s, mt->st[*i].l);
			}
			printf("TRYING METRIC: %d && %d && %d\n", !strncmp(mt->st[*i-1].s, "metric", 6), mt->st[*i].s[0] == '{', mt->st[*i].l == 1);
			if(!strncmp(mt->st[*i-1].s, "metric", 6) && mt->st[*i].s[0] == '{' && mt->st[*i].l == 1)
			{
				puts("metric");
				char *metric_name = NULL;
				uint8_t type = REGEX_GAUGE;
				uint64_t value = 0;
				char *from = NULL;

				puts("IN");
				for (++*i; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
				{
					printf("trying %s with pre %s and %s\n", mt->st[*i].s, mt->st[*i-1].s, mt->st[*i-2].s);
					if(!strncmp(mt->st[*i-1].s, "name", 7) && (mt->st[*i-2].s[0] == ';' || mt->st[*i-2].s[0] == '}') && mt->st[*i-2].l == 1)
					{
						metric_name = strdup(mt->st[*i].s);
						puts(">> metric_name");
						puts(metric_name);
					}
					else if(!strncmp(mt->st[*i-1].s, "type", 4) && (mt->st[*i-2].s[0] == ';' || mt->st[*i-2].s[0] == '}') && mt->st[*i-2].l == 1)
					{
						puts(">> type");
						if(strncmp(mt->st[*i].s, "counter", 7))
						{
							value = 1;
							type = REGEX_COUNTER;
						}
						else if(strncmp(mt->st[*i].s, "increase", 8))
						{
							value = 0;
							type = REGEX_INCREASE;
						}
						else if(strncmp(mt->st[*i].s, "gauge", 5))
						{
							value = 0;
							type = REGEX_GAUGE;
						}
					}
					else if(!strncmp(mt->st[*i-1].s, "value", 5) && (mt->st[*i-2].s[0] == ';' || mt->st[*i-2].s[0] == '}') && mt->st[*i-2].l == 1)
					{
						puts(">> value");
						from = strdup(mt->st[*i].s);
						value = 0;
					}
					else if(!strncmp(mt->st[*i-1].s, "label", 5) && mt->st[*i].s[0] == '{' && mt->st[*i].l == 1)
					{
						puts(">> label");
						char *label_name = NULL;
						char *label_key = NULL;
						for (++*i; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
						{
							puts("BOOM");
							printf("trying %s with pre %s and %s\n", mt->st[*i].s, mt->st[*i-1].s, mt->st[*i-2].s);
							if(!strncmp(mt->st[*i-1].s, "name", 4) && (mt->st[*i-2].s[0] == ';' || mt->st[*i-2].s[0] == '{') && mt->st[*i-2].l == 1)
							{
								label_name = strdup(mt->st[*i].s);
							}
							else if(!strncmp(mt->st[*i-1].s, "key", 3) && (mt->st[*i-2].s[0] == ';' || mt->st[*i-2].s[0] == '}') && mt->st[*i-2].l == 1)
							{
								label_key = strdup(mt->st[*i].s);
							}
						}
						printf("> label_name = %s\n> label_key = %s\n", label_name, label_key);
					}
				}
				if (metric_name)
					printf("> metric_name = %s\n", metric_name);
				printf("> type = %u\n> value = %"u64"\n", type, value);
				if(from)
					printf("> from = %s\n", from);
			}
		}
		puts("OUT");
		printf("template = %s\n", template);
	}
	else
		printf("'%s', '%s'\n", mt->st[*i].s, mt->st[*i+1].s);
}

void context_entrypoint_parser(mtlen *mt, int64_t *i)
{
	extern aconf *ac;
	void (*handler)(char*, size_t, context_arg*) = 0;

	if ( *i == 0 )
		*i += 1;

	context_arg *carg = NULL;
	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		if (!(mt->st[*i].l))
			continue;

		if (!carg)
			carg = calloc(1, sizeof(*carg));

		//printf("entrypoint %"d64": %s\n", *i, mt->st[*i].s);
		if(!strncmp(mt->st[*i-1].s, "handler", 7) && !strncmp(mt->st[*i].s, "log", 3))
		{
			handler = &log_handler;
			//context_log_handler_parser(mt, i);
		}
		else if(!strncmp(mt->st[*i-1].s, "handler", 7) && !strncmp(mt->st[*i].s, "rsyslog-impstats", 16))
		{
			handler = &rsyslog_impstats_handler;
		}
		else if(config_compare_begin(mt, *i, "mapping", 7))
		{
			++*i;
			mapping_metric *mm = context_mapping_parser(mt, i);
			if (!carg->mm)
				carg->mm = mm;
			else
				push_mapping_metric(carg->mm, mm);
		}
		else if (config_compare_begin(mt, *i, "allow", 5))
		{
			if (!carg->net_acl)
				carg->net_acl = calloc(1, sizeof(*carg->net_acl));

			char *ptr = config_get_arg(mt, *i, 1, NULL);
			if (ptr)
				network_range_push(carg->net_acl, strdup(ptr), IPACCESS_ALLOW);
		}
		else if (config_compare_begin(mt, *i, "deny", 4))
		{
			if (!carg->net_acl)
				carg->net_acl = calloc(1, sizeof(*carg->net_acl));

			char *ptr = config_get_arg(mt, *i, 1, NULL);
			if (ptr)
				network_range_push(carg->net_acl, strdup(ptr), IPACCESS_DENY);
		}
		else if (config_compare_begin(mt, *i, "httpauth", 8))
		{
			if (!carg->net_acl)
				carg->net_acl = calloc(1, sizeof(*carg->net_acl));

			char *ptr;
			ptr = config_get_arg(mt, *i, 1, NULL);
			if (!strncmp(ptr, "basic", 5))
			{
				ptr = config_get_arg(mt, *i, 2, NULL);
				size_t sz;
				carg->auth_basic = base64_encode(ptr, strlen(ptr), &sz);
			}
			else if (!strncmp(ptr, "bearer", 6))
			{
				ptr = config_get_arg(mt, *i, 2, NULL);
				carg->auth_bearer = strdup(ptr);
			}
		}
		else if (!strncmp(mt->st[*i-1].s, "tcp", 3) || !strncmp(mt->st[*i-1].s, "tls", 3))
		{
			uint8_t tls = !strncmp(mt->st[*i-1].s, "tls", 3) ? 1 : 0;
			char *port = strstr(mt->st[*i].s, ":");
			if (port)
			{
				char *host = strndup(mt->st[*i].s, port - mt->st[*i].s);
				carg->parser_handler = handler;
				tcp_server_init(ac->loop, host, atoi(port+1), tls, carg);
			}
			else
			{
				carg->parser_handler = handler;
				tcp_server_init(ac->loop, "0.0.0.0", atoi(mt->st[*i].s), tls, carg);
			}
			carg = NULL;
		}
		else if (!strncmp(mt->st[*i-1].s, "udp", 3))
		{
			char *port = strstr(mt->st[*i].s, ":");
			if (port)
			{
				char *host = strndup(mt->st[*i].s, port - mt->st[*i].s);
				udp_server_handler(host, atoi(port+1), handler, carg);
			}
			else
				udp_server_handler("0.0.0.0", atoi(mt->st[*i].s), handler, carg);
			carg = NULL;
		}
		else if (!strncmp(mt->st[*i-1].s, "unixgram", 8))
		{
			unixgram_server_handler(mt->st[*i].s, handler);
			carg = NULL;
		}
		else if (!strncmp(mt->st[*i-1].s, "unix", 4))
		{
			unix_server_handler(mt->st[*i].s, handler);
			carg = NULL;
		}
	}
}

void context_system_parser(mtlen *mt, int64_t *i)
{
	if ( *i == 0 )
		*i += 1;

	extern aconf *ac;

	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		if (!(mt->st[*i].l))
			continue;

		if (!strcmp(mt->st[*i].s, "base"))
			ac->system_base = 1;
		else if (!strcmp(mt->st[*i].s, "disk"))
			ac->system_disk = 1;
		else if (!strcmp(mt->st[*i].s, "network"))
			ac->system_network = 1;
		else if (!strcmp(mt->st[*i].s, "vm"))
			ac->system_vm = 1;
		else if (!strcmp(mt->st[*i].s, "firewall"))
			ac->system_firewall = 1;
		else if (!strcmp(mt->st[*i].s, "packages"))
		{
			ac->system_packages = 1;
#ifdef __linux__
			char *ptr = config_get_arg(mt, *i, 1, NULL);
			ac->rpmlib = rpm_library_init(ptr);
#endif
		}
		else if (!strcmp(mt->st[*i].s, "smart"))
			ac->system_smart = 1;
		//else if (!strcmp(mt->st[*i].s, "process"))
		else if (config_compare_begin(mt, *i, "process", 7))
		{
			ac->system_process = 1;
			++*i;
			for (; mt->st[*i].s[0] != ';'; ++*i)
			{
				match_push(ac->process_match, mt->st[*i].s, mt->st[*i].l);
			}
			--*i;
		}
		else if (!strncmp(mt->st[*i].s, "add_label", 9))
		{
			char *name;
			char *key;
			char *ptr = mt->st[*i].s;
			char *ptrval1 = ptr + strcspn(ptr+9, "=") + 9;
			ptrval1 += strspn(ptrval1, "= ");

			char *ptrval2 = ptrval1 + strcspn(ptrval1, ": ");
			name = strndup(ptrval1, ptrval2-ptrval1);

			ptrval2 += strspn(ptrval2, ": ");
			key = ptrval2;

			if (!ac->system_carg->labels)
			{
				ac->system_carg->labels = malloc(sizeof(tommy_hashdyn));
				tommy_hashdyn_init(ac->system_carg->labels);
			}

			labels_hash_insert_nocache(ac->system_carg->labels, name, key);
			free(name);
		}
	}
}

void context_log_level_parser(mtlen *mt, int64_t *i)
{
	extern aconf *ac;
	ac->log_level = atoll(mt->st[*i+1].s);
}

void context_ttl_parser(mtlen *mt, int64_t *i)
{
	extern aconf *ac;
	ac->ttl = atoll(mt->st[*i+1].s);
}

void context_persistence_parser(mtlen *mt, int64_t *i)
{
	extern aconf *ac;

	if ( *i == 0 )
		*i += 1;

	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		if (!(mt->st[*i].l))
			continue;

		if (config_compare_begin(mt, *i, "directory", 9))
		{
			ac->persistence_dir = strdup(mt->st[*i+1].s);
			mkdirp(ac->persistence_dir);
		}
		if (config_compare_begin(mt, *i, "period", 6))
			ac->persistence_period = atoll(mt->st[*i+1].s)*1000;
	}
}

void context_modules_parser(mtlen *mt, int64_t *i)
{
	if ( *i == 0 )
		*i += 1;

	extern aconf *ac;

	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		if (!(mt->st[*i].l))
			continue;

#ifdef __linux__
//		if (config_compare_begin(mt, *i, "postgresql", 10))
//		{
//			extern pq_library *pqlib;
//			char *module = mt->st[(*i)++].s;
//			char *path = mt->st[*i].s;
//			pqlib = pg_init(path);
//			if (ac->log_level > 1)
//				printf("postgresql module %s with path %s\n", module, path);
//		}
#endif
		if (config_compare_begin(mt, *i, "mysql", 5))
		{
			char *module = mt->st[(*i)++].s;
			char *path = mt->st[*i].s;
			//mylib = my_init(path);
			if (ac->log_level > 1)
				printf("mysql module %s with path %s\n", module, path);
		}
	}
}

typedef struct query_parameter {
	char *dyn_conf;
	char *sdiscovery;
} query_parameter;

query_parameter* query_parameter_fill(mtlen *mt, int64_t i)
{
	query_parameter *qparam = calloc(1, sizeof(*qparam));
	uint8_t n = 1;
	char *ptr;
	ptr = mt->st[i].s;
	for (n=1; ptr && strncmp(ptr, ";", 1); ++n)
	{
		ptr = config_get_arg(mt, i, n, NULL);
		if (!ptr)
			continue;

		else if (!strncmp(ptr, "conf", 4))
		{
			char *ptrval = ptr + strcspn(ptr+4, "=") + 4;
			ptrval += strspn(ptrval, "= ");
			qparam->dyn_conf = strdup(ptrval);
		}
		else if (!strncmp(ptr, "discovery", 9))
		{
			char *ptrval = ptr + strcspn(ptr+9, "=") + 9;
			ptrval += strspn(ptrval, "= ");
			qparam->sdiscovery = strdup(ptrval);
		}
	}

	return qparam;
}

void context_configuration_parser(mtlen *mt, int64_t *i)
{
	if ( *i == 0 )
		*i += 1;

	extern aconf *ac;

	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		if (!(mt->st[*i].l))
			continue;

		if (config_compare_begin(mt, *i, "etcd", 4))
		{
			++*i;
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			query_parameter* qparam = query_parameter_fill(mt, *i);
			if (qparam->dyn_conf)
			{
				char *replacedquery = malloc(255);
				snprintf(replacedquery, 255, "%sv2%s?recursive=true", hi->query, qparam->dyn_conf);
				char *query = gen_http_query(0, replacedquery, NULL, hi->host, "alligator", hi->auth, 1);
				int64_t g = *i;
				context_arg *carg = context_arg_fill(mt, &g, hi, sd_etcd_configuration, query, 0, NULL, NULL, ac->loop);
				smart_aggregator(carg);
			}
			if (qparam->sdiscovery)
			{
				char *replacedquery = malloc(255);
				snprintf(replacedquery, 255, "%sv2%s?recursive=true", hi->query, qparam->sdiscovery);
				char *query = gen_http_query(0, replacedquery, NULL, hi->host, "alligator", hi->auth, 1);
				int64_t g = *i;
				context_arg *carg = context_arg_fill(mt, &g, hi, sd_etcd_configuration, query, 0, NULL, NULL, ac->loop);
				smart_aggregator(carg);
			}

			free(qparam);
		}
		if (config_compare_begin(mt, *i, "consul", 6))
		{
			++*i;
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			query_parameter* qparam = query_parameter_fill(mt, *i);
			if (qparam->dyn_conf)
			{
				char *replacedquery = malloc(255);
				snprintf(replacedquery, 255, "%sv1%s?recurse", hi->query, qparam->dyn_conf);
				char *query = gen_http_query(0, replacedquery, NULL, hi->host, "alligator", hi->auth, 1);
				int64_t g = *i;
				context_arg *carg = context_arg_fill(mt, &g, hi, sd_consul_configuration, query, 0, NULL, NULL, ac->loop);
				smart_aggregator(carg);
			}
			if (qparam->sdiscovery)
			{
				char *replacedquery = malloc(255);
				snprintf(replacedquery, 255, "%sv1%s?recurse", hi->query, qparam->sdiscovery);
				char *query = gen_http_query(0, replacedquery, NULL, hi->host, "alligator", hi->auth, 1);
				int64_t g = *i;
				context_arg *carg = context_arg_fill(mt, &g, hi, sd_consul_discovery, query, 0, NULL, NULL, ac->loop);
				smart_aggregator(carg);
			}

			free(qparam);
		}
	}
}
