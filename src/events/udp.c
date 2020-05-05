#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
extern aconf *ac;

void udp_on_read(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
	if (nread < 0)
	{
		fprintf(stderr, "Read error %s\n", uv_err_name(nread));
		//uv_close((uv_handle_t*) req, NULL);
		free(buf->base);
		return;
	}

	context_arg *carg = req->data;
	(carg->conn_counter)++;
	(carg->read_counter)++;

	if (!check_udp_ip_port(addr, carg))
	{
		if (ac->log_level > 3)
			printf("no access!\n");
	}


	metric_add_labels("udp_entrypoint_connect", &carg->conn_counter, DATATYPE_UINT, carg, "entrypoint", carg->key);
	metric_add_labels("udp_entrypoint_read", &carg->read_counter, DATATYPE_UINT, carg, "entrypoint", carg->key);

	alligator_multiparser(buf->base, nread, carg->parser_handler, NULL, carg);
	free(buf->base);
}

void udp_on_send(uv_udp_send_t* req, int status) {
	if (status != 0) {
		fprintf(stderr, "send_cb error: %s\n", uv_strerror(status));
	}

	uv_udp_recv_start(req->handle, alloc_buffer, udp_on_read);
	free(req);
}

void udp_server_handler(char *addr, uint16_t port, void* parser_handler, context_arg *carg)
{
	if (!carg)
		carg = malloc(sizeof(*carg));
	carg->parser_handler = parser_handler;

	if (ac->log_level > 1)
		printf("init udp server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", NULL, 0, carg, addr, port);

	carg->parser_handler = parser_handler;
	carg->conn_counter = 0;
	carg->read_counter = 0;
	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s:%"PRIu16, addr, port);

	uv_loop_t *loop = uv_default_loop();
	uv_udp_t *recv_socket = calloc(1, sizeof(*recv_socket));
	uv_udp_init(loop, recv_socket);
	struct sockaddr_in *recv_addr = calloc(1, sizeof(*recv_addr));
	uv_ip4_addr(addr, port, recv_addr);
	uv_udp_bind(recv_socket, (const struct sockaddr *)recv_addr, 0);
	recv_socket->data = carg;
	uv_udp_recv_start(recv_socket, alloc_buffer, udp_on_read);
}

//int main() {
//	uv_loop_t *loop = uv_default_loop();
//
//	udp_server_handler("0.0.0.0", 1111, NULL);
//
//
//	uv_udp_t *send_socket = malloc(sizeof(*send_socket));
//	uv_udp_send_t *send_req = malloc(sizeof(*send_req));
//	uv_buf_t *discover_msg = malloc(sizeof(*discover_msg));
//	discover_msg->base = strdup("PING");
//	discover_msg->len = 4;
//	uv_udp_init(loop, send_socket);
//
//	struct sockaddr_in *send_addr = malloc(sizeof(*send_addr));
//	uv_ip4_addr("127.0.0.1", 1717, send_addr);
//	uv_udp_send(send_req, send_socket, discover_msg, 1, (struct sockaddr *)send_addr, udp_on_send);
//
//	//return uv_run(loop, UV_RUN_DEFAULT);
//}
