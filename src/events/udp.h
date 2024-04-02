#pragma once
#include "events/context_arg.h"
void udp_server_handler(char *addr, uint16_t port, void* parser_handler, context_arg *carg);
char* udp_client(void *arg);
void udp_client_del(context_arg *carg);
void udp_server_stop(const char* addr, uint16_t port);
void udp_server_init(uv_loop_t *loop, const char* addr, uint16_t port, uint8_t tls, context_arg *carg);
void udp_client_handler();
