#pragma once
#include "common/rtime.h"
typedef struct client_info
{
	struct sockaddr_in *dest;
	uv_connect_t *connect;
	uv_tcp_t *socket;
	char *key;
	void *parser_handler;
	char *mesg;
	char *hostname;
	r_time connect_time;
	r_time connect_time_finish;
	r_time write_time;
	r_time write_time_finish;
	r_time read_time;
	r_time read_time_finish;
	int lock;

	tommy_node node;
} client_info;

void tcp_client_handler();
void do_tcp_client(char *addr, char *port, void *handler, char *mesg);
