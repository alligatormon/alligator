#pragma once
#include <jansson.h>
json_t *config_get();
string *config_get_string();
void query_node_generate_conf(void *funcarg, void* arg);
