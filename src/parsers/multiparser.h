void alligator_multiparser(char *buf, size_t len, void (*handler)(char*, size_t, char*, int), char *response);
void redis_handler(char *metrics, size_t size, char *instance, int kind);
void aerospike_handler(char *metrics, size_t size, char *instance, int kind);
void http_proto_handler(char *metrics, size_t size, char *instance, int kind);
char* http_proto_proxer(char *metrics, size_t size, char *instance);
void zookeeper_isro_handler(char *metrics, size_t size, char *instance, int kind);
void zookeeper_wchs_handler(char *metrics, size_t size, char *instance, int kind);
void zookeeper_mntr_handler(char *metrics, size_t size, char *instance, int kind);

void clickhouse_system_handler(char *metrics, size_t size, char *instance, int kind);
void clickhouse_columns_handler(char *metrics, size_t size, char *instance, int kind);
void clickhouse_dictionary_handler(char *metrics, size_t size, char *instance, int kind);
void clickhouse_merges_handler(char *metrics, size_t size, char *instance, int kind);
void clickhouse_replicas_handler(char *metrics, size_t size, char *instance, int kind);
void beanstalkd_handler(char *metrics, size_t size, char *instance, int kind);
void memcached_handler(char *metrics, size_t size, char *instance, int kind);
