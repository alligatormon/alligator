#include <stdio.h>
#include "main.h"
#include "common/url.h"
#include "common/http.h"
#include "common/fastcgi.h"
#include "modules/modules.h"

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
		if (!strcmp(mt->st[*i-1].s, "prometheus"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			smart_aggregator_selector(hi, NULL, "GET /metrics HTTP/1.1\nHost: test\n\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "plain"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			smart_aggregator_selector(hi, NULL, "GET /metrics HTTP/1.1\nHost: test\n\n");
		}
#ifndef _WIN64
#ifdef __linux__
		else if (!strcmp(mt->st[*i-1].s, "icmp"))
		{
			do_icmp_client(mt->st[*i].s);
		}
#endif
		else if (!strcmp(mt->st[*i-1].s, "http"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, hi->host, "alligator", hi->auth, 0);
			printf("query %s, host %s, port %s\n", query, hi->host, hi->port);
			smart_aggregator_selector(hi, http_proto_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "process"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			put_to_loop_cmd(hi->host, NULL);
		}
		else if (!strcmp(mt->st[*i-1].s, "unix"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_unix_client(hi->host, NULL, "", APROTO_UNIX);
		}
		else if (!strcmp(mt->st[*i-1].s, "unixgram"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_unixgram_client(hi->host, NULL, "");
		}
		else if (!strcmp(mt->st[*i-1].s, "clickhouse"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, "?query=select%20metric,value\%20from\%20system.metrics", hi->host, "alligator", hi->auth, 0);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query, hi->proto);

			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20metric,value\%20from\%20system.asynchronous_metrics", hi->host, "alligator", hi->auth, 0);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query, hi->proto);

			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20event,value%20from\%20system.events", hi->host, "alligator", hi->auth, 0);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query, hi->proto);

			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20database,table,name,data_compressed_bytes,data_uncompressed_bytes,marks_bytes%20from\%20system.columns%20where%20database!=%27system%27", hi->host, "alligator", hi->auth, 0);
			do_tcp_client(hi->host, hi->port, clickhouse_columns_handler, query, hi->proto);

			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20name,bytes_allocated,query_count,hit_rate,element_count,load_factor%20from\%20system.dictionaries", hi->host, "alligator", hi->auth, 0);
			do_tcp_client(hi->host, hi->port, clickhouse_dictionary_handler, query, hi->proto);

			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20database,is_mutation,table,progress,num_parts,total_size_bytes_compressed,total_size_marks,bytes_read_uncompressed,rows_read,bytes_written_uncompressed,rows_written%20from\%20system.merges", hi->host, "alligator", hi->auth, 0);
			do_tcp_client(hi->host, hi->port, clickhouse_merges_handler, query, hi->proto);

			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20database,table,is_leader,is_readonly,future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,log_pointer,total_replicas,active_replicas%20from\%20system.replicas", hi->host, "alligator", hi->auth, 0);
			do_tcp_client(hi->host, hi->port, clickhouse_replicas_handler, query, hi->proto);
		}
		else if (!strcmp(mt->st[*i-1].s, "redis"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char* query = malloc(1000);
			if (hi->pass)
				snprintf(query, 1000, "AUTH %s\r\nINFO\r\n", hi->pass);
			else
				snprintf(query, 1000, "INFO\n");
			printf("aa %p\n", hi);
			printf("auth: %s\n", hi->pass);
			//do_tcp_client(hi->host, hi->port, redis_handler, query);
			smart_aggregator_selector(hi, redis_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "memcached"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			smart_aggregator_selector(hi, memcached_handler, "stats\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "beanstalkd"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			smart_aggregator_selector(hi, beanstalkd_handler, "stats\r\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "aerospike"))
		{
			//host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			//do_tcp_client(hi->host, hi->port, aerospike_handler, "INFO\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "zookeeper"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			smart_aggregator_selector(hi, zookeeper_mntr_handler, "mntr");
			smart_aggregator_selector(hi, zookeeper_isro_handler, "isro");
			smart_aggregator_selector(hi, zookeeper_wchs_handler, "wchs");
		}
		else if (!strcmp(mt->st[*i-1].s, "gearmand"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			smart_aggregator_selector(hi, gearmand_handler, "status\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "uwsgi"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			smart_aggregator_selector(hi, uwsgi_handler, NULL);
		}
		else if (!strcmp(mt->st[*i-1].s, "php-fpm"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			size_t buf_len = 5;

			uv_buf_t *buffer = gen_fcgi_query(hi->query, hi->host, "alligator", NULL, &buf_len);

			smart_aggregator_selector_buffer(hi, php_fpm_handler, buffer, buf_len);
		}
		else if (!strcmp(mt->st[*i-1].s, "eventstore"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char stats_string[255];
			snprintf(stats_string, 255, "%s%s", hi->query, "/stats");
			char *stats_query = gen_http_query(0, stats_string, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, eventstore_stats_handler, stats_query);

			char projections_string[255];
			snprintf(projections_string, 255, "%s%s", hi->query, "/projections/any");
			char *projections_query = gen_http_query(0, projections_string, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, eventstore_projections_handler, projections_query);

			char info_string[255];
			snprintf(info_string, 255, "%s%s", hi->query, "/info");
			char *info_query = gen_http_query(0, info_string, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, eventstore_info_handler, info_query);
		}
		else if (!strcmp(mt->st[*i-1].s, "flower"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char workers_string[255];
			snprintf(workers_string, 255, "%s%s", hi->query, "/");
			char *workers_query = gen_http_query(0, workers_string, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, flower_handler, workers_query);
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
			smart_aggregator_selector(hi, powerdns_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "opentsdb"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char *query = gen_http_query(0, hi->query, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, opentsdb_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "kamailio"))
		{
			//host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			//smart_aggregator_selector(hi, kamailio_handler, "{\"jsonrpc\": \"2.0\", \"method\": \"stats.get_statistics\", \"params\": [\"all\"]}");
		}
		else if (!strcmp(mt->st[*i-1].s, "haproxy"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			smart_aggregator_selector(hi, haproxy_info_handler, "show info\n");
			smart_aggregator_selector(hi, haproxy_pools_handler, "show pools\n");
			smart_aggregator_selector(hi, haproxy_stat_handler, "show stat\n");
			smart_aggregator_selector(hi, haproxy_sess_handler, "show sess\n");
			//smart_aggregator_selector(hi, haproxy_table_handler, "show table\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "nats"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char connz_string[255];
			snprintf(connz_string, 255, "%s%s", hi->query, "/connz");
			char *connz_query = gen_http_query(0, connz_string, hi->host, "alligator", hi->auth, 0);
			smart_aggregator_selector(hi, nats_connz_handler, connz_query);

			char subsz_string[255];
			snprintf(subsz_string, 255, "%s%s", hi->query, "/subsz");
			char *subsz_query = gen_http_query(0, subsz_string, hi->host, "alligator", hi->auth, 0);
			smart_aggregator_selector(hi, nats_subsz_handler, subsz_query);

			char varz_string[255];
			snprintf(varz_string, 255, "%s%s", hi->query, "/varz");
			char *varz_query = gen_http_query(0, varz_string, hi->host, "alligator", hi->auth, 0);
			smart_aggregator_selector(hi, nats_varz_handler, varz_query);

			char routez_string[255];
			snprintf(routez_string, 255, "%s%s", hi->query, "/routez");
			char *routez_query = gen_http_query(0, routez_string, hi->host, "alligator", hi->auth, 0);
			smart_aggregator_selector(hi, nats_routez_handler, routez_query);
		}
		else if (!strcmp(mt->st[*i-1].s, "riak"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char url_string[255];
			snprintf(url_string, 255, "%s%s", hi->query, "/stats");
			char *http_query = gen_http_query(0, url_string, hi->host, "alligator", hi->auth, 0);
			smart_aggregator_selector(hi, riak_handler, http_query);
		}
		else if (!strcmp(mt->st[*i-1].s, "rabbitmq"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char overview_string[255];
			snprintf(overview_string, 255, "%s%s", hi->query, "/api/overview");
			char *overview_query = gen_http_query(0, overview_string, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, rabbitmq_overview_handler, overview_query);

			char nodes_string[255];
			snprintf(nodes_string, 255, "%s%s", hi->query, "/api/nodes");
			char *nodes_query = gen_http_query(0, nodes_string, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, rabbitmq_nodes_handler, nodes_query);

			char exchanges_string[255];
			snprintf(exchanges_string, 255, "%s%s", hi->query, "/api/exchanges");
			char *exchanges_query = gen_http_query(0, exchanges_string, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, rabbitmq_exchanges_handler, exchanges_query);

			// !!!dynamic!!!
			//char connections_string[255];
			//snprintf(connections_string, 255, "%s%s", hi->query, "/api/connections");
			//char *connections_query = gen_http_query(0, connections_string, hi->host, "alligator", hi->auth, 1);
			//smart_aggregator_selector(hi, rabbitmq_connections_handler, connections_query);

			// !!!dynamic!!!
			//char channels_string[255];
			//snprintf(channels_string, 255, "%s%s", hi->query, "/api/channels");
			//char *channels_query = gen_http_query(0, channels_string, hi->host, "alligator", hi->auth, 1);
			//smart_aggregator_selector(hi, rabbitmq_channels_handler, channels_query);

			char queues_string[255];
			snprintf(queues_string, 255, "%s%s", hi->query, "/api/queues");
			char *queues_query = gen_http_query(0, queues_string, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, rabbitmq_queues_handler, queues_query);

			char vhosts_string[255];
			snprintf(vhosts_string, 255, "%s%s", hi->query, "/api/vhosts");
			char *vhosts_query = gen_http_query(0, vhosts_string, hi->host, "alligator", hi->auth, 1);
			smart_aggregator_selector(hi, rabbitmq_vhosts_handler, vhosts_query);
		}
		else if (!strcmp(mt->st[*i-1].s, "elasticsearch"))
		{
			//host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			//char nodes_string[255];
			//snprintf(nodes_string, 255, "%s%s", hi->query, "/_nodes/stats");
			//char *nodes_query = gen_http_query(0, nodes_string, hi->host, "alligator", hi->auth, 0);
			//smart_aggregator_selector(hi, elasticsearch_nodes_handler, nodes_query);

			//char cluster_string[255];
			//snprintf(cluster_string, 255, "%s%s", hi->query, "/_cluster/stats");
			//char *cluster_query = gen_http_query(0, cluster_string, hi->host, "alligator", hi->auth, 0);
			//smart_aggregator_selector(hi, elasticsearch_cluster_handler, cluster_query);

			//char health_string[255];
			//snprintf(health_string, 255, "%s%s", hi->query, "/_cluster/health");
			//char *health_query = gen_http_query(0, health_string, hi->host, "alligator", hi->auth, 0);
			//smart_aggregator_selector(hi, elasticsearch_health_handler, health_query);
		}
		else if (!strcmp(mt->st[*i-1].s, "mssql"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			put_to_loop_cmd(hi->host, mssql_handler);
		}
		else if (!strcmp(mt->st[*i-1].s, "period"))
		{
			int64_t repeat = atoll(mt->st[*i].s);
			if (repeat > 0)
				ac->aggregator_repeat = repeat;
		}
		else if (!strcmp(mt->st[*i-1].s, "postgresql"))
		{
			//void* (*func)() = module_load("libpostgresql_client.so", "do_postgresql_client");
			init_func func = module_load("/app/src/libpostgresql_client.so", "do_postgresql_client");
			if (!func)
			{
				printf("postgresql cannot be scrape\n");
				continue;
			}

			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			func(hi->host);
		}
#endif
	}
}

void context_entrypoint_parser(mtlen *mt, int64_t *i)
{
	if ( *i == 0 )
		*i += 1;
	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		if (!(mt->st[*i].l))
			continue;

		//printf("entrypoint %"d64": %s\n", *i, mt->st[*i].s);
		if (!strncmp(mt->st[*i-1].s, "tcp", 3))
			tcp_server_handler("0.0.0.0", atoi(mt->st[*i].s), NULL);
#ifndef _WIN64
		else if (!strncmp(mt->st[*i-1].s, "udp", 3))
			udp_server_handler("0.0.0.0", atoi(mt->st[*i].s), NULL);
		else if (!strncmp(mt->st[*i-1].s, "unixgram", 8))
			unixgram_server_handler(mt->st[*i].s, NULL);
		else if (!strncmp(mt->st[*i-1].s, "unix", 4))
			unix_server_handler(mt->st[*i].s, NULL);
#endif
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
		else if (!strcmp(mt->st[*i].s, "process"))
			ac->system_process = 1;
	}
}
