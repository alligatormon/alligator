#define MAX_RESPONSE_SIZE 6553500
#include "events/client_info.h"
void alligator_multiparser(char *buf, size_t len, void (*handler)(char*, size_t, client_info*), char *response, client_info *cinfo);
void redis_handler(char *metrics, size_t size, client_info *cinfo);
void aerospike_handler(char *metrics, size_t size, client_info *cinfo);
void http_proto_handler(char *metrics, size_t size, client_info *cinfo);
char* http_proto_proxer(char *metrics, size_t size, client_info *cinfo);
void zookeeper_isro_handler(char *metrics, size_t size, client_info *cinfo);
void zookeeper_wchs_handler(char *metrics, size_t size, client_info *cinfo);
void zookeeper_mntr_handler(char *metrics, size_t size, client_info *cinfo);

void clickhouse_system_handler(char *metrics, size_t size, client_info *cinfo);
void clickhouse_columns_handler(char *metrics, size_t size, client_info *cinfo);
void clickhouse_dictionary_handler(char *metrics, size_t size, client_info *cinfo);
void clickhouse_merges_handler(char *metrics, size_t size, client_info *cinfo);
void clickhouse_replicas_handler(char *metrics, size_t size, client_info *cinfo);
void beanstalkd_handler(char *metrics, size_t size, client_info *cinfo);
void memcached_handler(char *metrics, size_t size, client_info *cinfo);
void mssql_handler(char *metrics, size_t size, client_info *cinfo);
void gearmand_handler(char *metrics, size_t size, client_info *cinfo);
void haproxy_info_handler(char *metrics, size_t size, client_info *cinfo);
void haproxy_pools_handler(char *metrics, size_t size, client_info *cinfo);
void haproxy_stat_handler(char *metrics, size_t size, client_info *cinfo);
void haproxy_sess_handler(char *metrics, size_t size, client_info *cinfo);
void haproxy_table_handler(char *metrics, size_t size, client_info *cinfo);
