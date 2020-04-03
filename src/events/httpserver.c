#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <uv.h>
#include "main.h"
#include "events/uv_alloc.h"
#include "events/context_arg.h"
#include "common/selector.h"

typedef struct http_info
{
	uv_stream_t *client;
	char *answ;
} http_info;

//typedef struct tcp_entrypoint_info
//{
//	void *parser_handler;
//	context_arg->carg;
//} tcp_entrypoint_info;

#define CONNECTIONS_COUNT (128)

void socket_read_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf);

void http_after_connect(uv_handle_t* handle)
{
	free(handle);
}

void socket_write_cb(uv_write_t *req, int status)
{
	if (status) {
		fprintf(stderr, "Write error %s\n", uv_strerror(status));
	}

	http_info *hinfo = req->data;
	uv_stream_t *client = hinfo->client;
	if (!strncmp(hinfo->answ, "HTTP", 4))
	{
		uv_close((uv_handle_t *)client, http_after_connect);
	}

	uv_read_start((uv_stream_t *) client, alloc_buffer, socket_read_cb);
	free(hinfo->answ);
	free(hinfo);
	free(req);
}

uint8_t check_ip_port(uv_tcp_t *client, context_arg *carg)
{
	extern aconf *ac;
	struct sockaddr_in saddr = {0};
	int slen = 0;
	char ip[17] = {0};

	uv_tcp_getsockname(client, (struct sockaddr *)&saddr, &slen);
	uv_ip4_name((const struct sockaddr_in *)&saddr, ip, sizeof(ip));

	struct sockaddr_in caddr = {0};
	int caddr_len;
	uv_tcp_getpeername(client, (struct sockaddr*)&caddr, &caddr_len);
	char addr[17];

	char check_ip[17];
	uv_ip4_name((struct sockaddr_in *)&caddr, (char*) check_ip, sizeof check_ip);

	uv_inet_ntop(AF_INET, &caddr.sin_addr, addr, sizeof(addr));
	if (ac->log_level > 3)
	{
		printf("client port %d\n", ntohs(caddr.sin_port));
		printf("client host %s\n", addr);
	}
	return ip_check_access(carg->net_acl, addr);
}

void socket_read_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf)
{
	extern aconf *ac;
	context_arg *carg = client->data;

	if (!check_ip_port((uv_tcp_t*)client, carg))
	{
		if (ac->log_level > 3)
			printf("BANNED!\n");
		uv_close((uv_handle_t*) client, http_after_connect);
		return;
	}

	http_info *hinfo = calloc(1, sizeof(*hinfo));
	if (nread < 0)
	{
		if (nread != UV_EOF)
			fprintf(stderr, "Read error %s\n", uv_err_name(nread));

		uv_close((uv_handle_t*) client, http_after_connect);
	}
	else if (nread > 0)
	{
		string *str = string_init(6553500);
		alligator_multiparser(buf->base, nread, carg->parser_handler, str, carg);
		//puts("=======================================");
		//printf("multiparser:\n-----\n'%s'\n------\n'%s'\n", buf->base, str->s);
		char *answ = str->s;
		size_t answ_len = str->l;
		//printf("answ: %s\n", answ);
		//data->response = answ; //strdup("HTTP/1.1 200 OK\n\nHello World, work's done\n\n");

		uv_write_t *req = (uv_write_t *) malloc(sizeof(uv_write_t));
		uv_buf_t write_buf = uv_buf_init(answ, answ_len);
		hinfo->answ = answ;
		hinfo->client = client;
		req->data = hinfo;

		free(str);
		uv_write(req, client, &write_buf, 1, socket_write_cb);
	}

	if (buf->base)
		free(buf->base);
}



void accept_connection_cb(uv_stream_t *server, int status)
{
	//context_arg *carg = server->data;
	if (status < 0) {
		fprintf(stderr, "New connection error %s\n", uv_strerror(status));
		return;
	}

	uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
	uv_tcp_init(uv_default_loop(), client);
	client->data = server->data;

	if (uv_accept(server, (uv_stream_t*) client) == 0)
	{
		//struct sockaddr_in caddr;
		//int caddr_len;
		//uv_tcp_getpeername(client, (struct sockaddr*)&caddr, &caddr_len);
		//char addr[16];
		//uv_inet_ntop(AF_INET, &caddr.sin_addr, addr, sizeof(addr));
		//printf("client port %d\n", ntohs(caddr.sin_port));
		//printf("client host %s\n", addr);

		uv_read_start((uv_stream_t *) client, alloc_buffer, socket_read_cb);
	}
	else
	{
		uv_close((uv_handle_t*) client, http_after_connect);
	}
}

int tcp_server_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((context_arg*)obj)->key;
        return strcmp(s1, s2);
}

int8_t tcp_server_handler(char *addr, uint16_t port, void* handler, context_arg *carg)
{
	extern aconf *ac;

	if (!carg)
		carg = calloc(1, sizeof(*carg));
	carg->parser_handler = handler;
	uv_tcp_t *server = calloc(1, sizeof(uv_tcp_t));
	server->data = carg;
	struct sockaddr_in *saddr = carg->dest = calloc(1, sizeof(*saddr));

	uv_tcp_init(uv_default_loop(), server);

	uv_ip4_addr(addr, port, saddr);
	uv_tcp_bind(server, (const struct sockaddr*)saddr, 0);

	int ret = uv_listen((uv_stream_t*) server, CONNECTIONS_COUNT, accept_connection_cb);
	if(ret) {
		fprintf(stderr, "Listen error %s\n", uv_strerror(ret));
		return 0;
	}

	carg->key = malloc(1000);
	carg->server = (uv_handle_t*)server;
	snprintf(carg->key, 1000, "%s:%"u16, addr, port);
	tommy_hashdyn_insert(ac->tcp_server_handler, &(carg->node), carg, tommy_strhash_u32(0, carg->key));

	return 1;
}

int8_t tcp_server_handler_free(char *addr, uint16_t port)
{
	extern aconf *ac;
	char serverkey[1000];
	snprintf(serverkey, 1000, "%s:%"u16, addr, port);

	context_arg *carg = tommy_hashdyn_search(ac->tcp_server_handler, tcp_server_compare, serverkey, tommy_strhash_u32(0, serverkey));
	if (!carg)
		return 0;

	uv_close((uv_handle_t *)carg->server, NULL);
	tommy_hashdyn_remove_existing(ac->tcp_server_handler, &(carg->node));
	free(carg->key);
	free(carg->server);
	free(carg->dest);
	free(carg);
	return 1;
}
