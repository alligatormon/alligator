#include <stdio.h>
#include "main.h"
#include "common/url.h"
#include "common/http.h"
#include "common/fastcgi.h"
#include "modules/modules.h"

int8_t config_compare(mtlen *mt, int64_t i, char *str, size_t size)
{
	uint64_t j;
	char *cstr = mt->st[i].s;
	size_t cstr_size = mt->st[i].l;

	//printf("i=%d\n", i);
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

int8_t config_compare_begin(mtlen *mt, int64_t i, char *str, size_t size)
{
	//printf("compare %s and %s\n", mt->st[i].s, str);
	if (config_compare(mt, i-1, "{", 1) || config_compare(mt, i-1, "}", 1) || config_compare(mt, i-1, ";", 1))
	{
		return config_compare(mt, i, str, size);
	}
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
			char *query = gen_http_query(0, "/?query=select%20metric,value\%20from\%20system.metrics", hi->host, "alligator", hi->auth, 1);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query, hi->proto);

			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "/?query=select%20metric,value\%20from\%20system.asynchronous_metrics", hi->host, "alligator", hi->auth, 1);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query, hi->proto);
                        
			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "/?query=select%20event,value%20from\%20system.events", hi->host, "alligator", hi->auth, 1);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query, hi->proto);
                        
			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "/?query=select%20database,table,name,data_compressed_bytes,data_uncompressed_bytes,marks_bytes%20from\%20system.columns%20where%20database!=%27system%27", hi->host, "alligator", hi->auth, 1);
			do_tcp_client(hi->host, hi->port, clickhouse_columns_handler, query, hi->proto);
                        
			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "/?query=select%20name,bytes_allocated,query_count,hit_rate,element_count,load_factor%20from\%20system.dictionaries", hi->host, "alligator", hi->auth, 1);
			do_tcp_client(hi->host, hi->port, clickhouse_dictionary_handler, query, hi->proto);
                        
			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "/?query=select%20database,is_mutation,table,progress,num_parts,total_size_bytes_compressed,total_size_marks,bytes_read_uncompressed,rows_read,bytes_written_uncompressed,rows_written%20from\%20system.merges", hi->host, "alligator", hi->auth, 1);
			do_tcp_client(hi->host, hi->port, clickhouse_merges_handler, query, hi->proto);
                        
			//hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "/?query=select%20database,table,is_leader,is_readonly,future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,log_pointer,total_replicas,active_replicas%20from\%20system.replicas", hi->host, "alligator", hi->auth, 1);
			do_tcp_client(hi->host, hi->port, clickhouse_replicas_handler, query, hi->proto);
		}
		else if (!strcmp(mt->st[*i-1].s, "redis"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char* query = malloc(1000);
			if (hi->pass)
				snprintf(query, 1000, "AUTH %s\r\nINFO ALL\r\n", hi->pass);
			else
				snprintf(query, 1000, "INFO ALL\n");
			//do_tcp_client(hi->host, hi->port, redis_handler, query);
			smart_aggregator_selector(hi, redis_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "sentinel"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char* query = malloc(1000);
			if (hi->pass)
				snprintf(query, 1000, "AUTH %s\r\nINFO\r\n", hi->pass);
			else
				snprintf(query, 1000, "INFO\n");
			smart_aggregator_selector(hi, sentinel_handler, query);
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
		else if (!strcmp(mt->st[*i-1].s, "nginx_upstream_check"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char string[255];
			snprintf(string, 255, "%s%s", hi->query, "?format=csv");
			char *query = gen_http_query(0, string, hi->host, "alligator", hi->auth, 0);
			//do_tcp_client(hi->host, hi->port, nginx_upstream_check_handler, query, hi->proto);
			smart_aggregator_selector(hi, nginx_upstream_check_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "elasticsearch"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);

			char nodes_string[255];
			snprintf(nodes_string, 255, "%s%s", hi->query, "/_nodes/stats");
			char *nodes_query = gen_http_query(0, nodes_string, hi->host, "alligator", hi->auth, 0);
			smart_aggregator_selector(hi, elasticsearch_nodes_handler, nodes_query);

			char cluster_string[255];
			snprintf(cluster_string, 255, "%s%s", hi->query, "/_cluster/stats");
			char *cluster_query = gen_http_query(0, cluster_string, hi->host, "alligator", hi->auth, 0);
			smart_aggregator_selector(hi, elasticsearch_cluster_handler, cluster_query);

			char health_string[255];
			snprintf(health_string, 255, "%s%s", hi->query, "/_cluster/health");
			char *health_query = gen_http_query(0, health_string, hi->host, "alligator", hi->auth, 0);
			smart_aggregator_selector(hi, elasticsearch_health_handler, health_query);
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
				printf("> type = %u\n> value = %llu\n", type, value);
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
	void (*handler)(char*, size_t, client_info*) = 0;

	if ( *i == 0 )
		*i += 1;

	client_info *cinfo = calloc(1, sizeof(*cinfo));
	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		if (!(mt->st[*i].l))
			continue;

		//printf("entrypoint %"d64": %s\n", *i, mt->st[*i].s);
		if(!strncmp(mt->st[*i-1].s, "handler", 7) && !strncmp(mt->st[*i].s, "log", 3))
		{
			handler = &log_handler;
			//context_log_handler_parser(mt, i);
		}
		else if(config_compare_begin(mt, *i, "mapping", 7))
		{
			++*i;
			mapping_metric *mm = context_mapping_parser(mt, i);
			if (!cinfo->mm)
				cinfo->mm = mm;
			else
				push_mapping_metric(cinfo->mm, mm);
			printf("gg cinfo %p with mm %p\n", cinfo, cinfo->mm);
		}
		else if (!strncmp(mt->st[*i-1].s, "tcp", 3))
		{
			char *port = strstr(mt->st[*i].s, ":");
			printf("cc cinfo %p with mm %p\n", cinfo, cinfo->mm);
			if (port)
			{
				char *host = strndup(mt->st[*i].s, port - mt->st[*i].s);
				tcp_server_handler(host, atoi(port+1), handler, cinfo);
			}
			else
				tcp_server_handler("0.0.0.0", atoi(mt->st[*i].s), handler, cinfo);
		}
#ifndef _WIN64
		else if (!strncmp(mt->st[*i-1].s, "udp", 3))
		{
			char *port = strstr(mt->st[*i].s, ":");
			if (port)
			{
				char *host = strndup(mt->st[*i].s, port - mt->st[*i].s);
				udp_server_handler(host, atoi(port+1), handler);
			}
			else
				udp_server_handler("0.0.0.0", atoi(mt->st[*i].s), handler);
		}
		else if (!strncmp(mt->st[*i-1].s, "unixgram", 8))
			unixgram_server_handler(mt->st[*i].s, handler);
		else if (!strncmp(mt->st[*i-1].s, "unix", 4))
			unix_server_handler(mt->st[*i].s, handler);
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
