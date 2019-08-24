#pragma once
#include "events/client_info.h"
void udp_server_handler(char *addr, uint16_t port, void* parser_handler, client_info *cinfo);
