#pragma once
#include <uv.h>
#include "context_arg.h"

uint8_t check_udp_ip_port(const struct sockaddr *caddr, context_arg *carg);
uint8_t check_ip_port(uv_tcp_t *client, context_arg *carg);
