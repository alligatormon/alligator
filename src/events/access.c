#include <uv.h>
#include <stdio.h>
#include <stdio.h>
#include "context_arg.h"
#include "main.h"
extern aconf *ac;

uint8_t check_ip_port(uv_tcp_t *client, context_arg *carg)
{
	struct sockaddr_storage caddr = {0};
	int caddr_len = sizeof(caddr);;
	uv_tcp_getpeername(client, (struct sockaddr*)&caddr, &caddr_len);
	char addr[17];

	char check_ip[17];
	struct sockaddr_in *in_caddr = (struct sockaddr_in*)&caddr;
	uv_ip4_name(in_caddr, (char*) check_ip, sizeof check_ip);

	uv_inet_ntop(AF_INET, (void *)&in_caddr->sin_addr, addr, sizeof(addr));
	if (ac->log_level > 3)
	{
		printf("tcp client port %d\n", ntohs(in_caddr->sin_port));
		printf("tcp client host %s\n", addr);
	}
	return ip_check_access(carg->net_acl, addr);
}

uint8_t check_udp_ip_port(const struct sockaddr *caddr, context_arg *carg)
{
	char sender[17] = { 0 };
	uv_ip4_name((const struct sockaddr_in*) caddr, sender, 16);

	if (ac->log_level > 3)
	{
		printf("udp client host %s\n", sender);
	}
	return ip_check_access(carg->net_acl, sender);
}
