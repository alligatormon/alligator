#include "events/context_arg.h"
void sd_consul_configuration(char *metrics, size_t size, context_arg *carg);
void sd_etcd_configuration(char *conf, size_t size, context_arg *carg);
void sd_consul_discovery(char *conf, size_t conf_len, context_arg *carg);
char* zk_client(context_arg* carg);
void sd_consul_configuration_parser_push();
void sd_consul_discovery_parser_push();
void sd_etcd_parser_push();
