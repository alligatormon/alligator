#include <stdio.h>
#include "main.h"
#include "common/url.h"
#include "common/http.h"

void context_aggregate_parser(mtlen *mt, int64_t *i)
{
	if ( *i == 0 )
		*i += 1;

	for (; *i<mt->m && strncmp(mt->st[*i].s, "}", 1); *i+=1)
	{
		//printf("%"d64": %s\n", *i, mt->st[*i].s);
		if (!strcmp(mt->st[*i-1].s, "prometheus"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_tcp_client(hi->host, hi->port, NULL, "GET /metrics HTTP/1.1\nHost: test\n\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "plain"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_tcp_client(hi->host, hi->port, NULL, "GET /metrics HTTP/1.1\nHost: test\n\n");
		}
#ifndef _WIN64
#ifndef __APPLE__
		else if (!strcmp(mt->st[*i-1].s, "icmp"))
		{
			do_icmp_client(mt->st[*i].s);
		}
#endif
		else if (!strcmp(mt->st[*i-1].s, "http"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, hi->query, hi->host, "alligator", hi->auth);
			printf("query %s, host %s, port %s\n", query, hi->host, hi->port);
			do_tcp_client(hi->host, hi->port, http_proto_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "exec"))
		{
			put_to_loop_cmd(mt->st[*i].s, NULL);
		}
		else if (!strcmp(mt->st[*i-1].s, "unix"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_unix_client(hi->host, NULL, "");
		}
		else if (!strcmp(mt->st[*i-1].s, "unixgram"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_unixgram_client(hi->host, NULL, "");
		}
		else if (!strcmp(mt->st[*i-1].s, "clickhouse"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			char *query = gen_http_query(0, "?query=select%20metric,value\%20from\%20system.metrics", hi->host, "alligator", hi->auth);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query);

			hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20metric,value\%20from\%20system.asynchronous_metrics", hi->host, "alligator", hi->auth);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query);

			hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20event,value%20from\%20system.events", hi->host, "alligator", hi->auth);
			do_tcp_client(hi->host, hi->port, clickhouse_system_handler, query);

			hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20database,table,name,data_compressed_bytes,data_uncompressed_bytes,marks_bytes%20from\%20system.columns%20where%20database!=%27system%27", hi->host, "alligator", hi->auth);
			do_tcp_client(hi->host, hi->port, clickhouse_columns_handler, query);

			hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20name,bytes_allocated,query_count,hit_rate,element_count,load_factor%20from\%20system.dictionaries", hi->host, "alligator", hi->auth);
			do_tcp_client(hi->host, hi->port, clickhouse_dictionary_handler, query);

			hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20database,is_mutation,table,progress,num_parts,total_size_bytes_compressed,total_size_marks,bytes_read_uncompressed,rows_read,bytes_written_uncompressed,rows_written%20from\%20system.merges", hi->host, "alligator", hi->auth);
			do_tcp_client(hi->host, hi->port, clickhouse_merges_handler, query);

			hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			query = gen_http_query(0, "?query=select%20database,table,is_leader,is_readonly,future_parts,parts_to_check,queue_size,inserts_in_queue,merges_in_queue,log_max_index,log_pointer,total_replicas,active_replicas%20from\%20system.replicas", hi->host, "alligator", hi->auth);
			do_tcp_client(hi->host, hi->port, clickhouse_replicas_handler, query);
		}
		else if (!strcmp(mt->st[*i-1].s, "redis"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_tcp_client(hi->host, hi->port, redis_handler, "INFO\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "aerospike"))
		{
			//host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			//do_tcp_client(hi->host, hi->port, aerospike_handler, "INFO\n");
		}
		else if (!strcmp(mt->st[*i-1].s, "zookeeper"))
		{
			host_aggregator_info *hi = parse_url(mt->st[*i].s, mt->st[*i].l);
			do_tcp_client(hi->host, hi->port, zookeeper_mntr_handler, "mntr");
			do_tcp_client(hi->host, hi->port, zookeeper_isro_handler, "isro");
			do_tcp_client(hi->host, hi->port, zookeeper_wchs_handler, "wchs");
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
		printf("entrypoint %"d64": %s\n", *i, mt->st[*i].s);
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
