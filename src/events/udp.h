#pragma once
#include "events/context_arg.h"
void udp_server_handler(char *addr, uint16_t port, void* parser_handler, context_arg *carg);
char* udp_client(void *arg);
