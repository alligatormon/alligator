#pragma once
#include <uv.h>
#include "context_arg.h"

uint8_t check_udp_ip_port(const struct sockaddr *caddr, context_arg *carg);
uint8_t check_ip_port(uv_tcp_t *client, context_arg *carg);
void access_log_denied_tcp(uv_tcp_t *client, context_arg *listener);
void access_log_denied_udp(const struct sockaddr *addr, context_arg *listener);
