#pragma once
#include "events/context_arg.h"
int8_t tcp_server_handler(char *addr, uint16_t port, void* handler, context_arg *carg);
int tcp_server_init(uv_loop_t *loop, const char* ip, int port, uint8_t tls, context_arg *import_carg);
void tcp_server_stop(const char* ip, int port);
