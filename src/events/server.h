#pragma once
#include "events/context_arg.h"
int8_t tcp_server_handler(char *addr, uint16_t port, void* handler, context_arg *carg);
