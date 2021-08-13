#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/entrypoint.h"
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
	if (nread == 0)
	{
		free(buf->base);
		return;
	}


	//printf("nread %zd\n", nread);

	context_arg *carg = req->data;
	(carg->conn_counter)++;
	(carg->read_counter)++;

	if (!check_udp_ip_port(addr, carg))
	{
		if (ac->log_level > 3)
			printf("no access!\n");
		free(buf->base);
		return;
	}


	metric_add_labels("udp_entrypoint_read", &carg->read_counter, DATATYPE_UINT, carg, "entrypoint", carg->key);

	alligator_multiparser(buf->base, nread, carg->parser_handler, NULL, carg);
	if (carg->lock)
	{
		uv_udp_recv_stop(req);
		uv_close((uv_handle_t*) req, NULL);
	}
	free(buf->base);
}

void udp_on_send(uv_udp_send_t* req, int status) {
	if (status != 0) {
		fprintf(stderr, "send_cb error: %s\n", uv_strerror(status));
	}

	req->handle->data = req->data;

	uv_udp_recv_start(req->handle, alloc_buffer, udp_on_read);
	//free(req);
}

void udp_server_init(uv_loop_t *loop, const char* addr, uint16_t port, uint8_t tls, context_arg *carg)
//void udp_server_handler(char *addr, uint16_t port, void* parser_handler, context_arg *carg)
{
	if (ac->log_level > 1)
		printf("init udp server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", NULL, 0, carg, addr, port);

	carg->conn_counter = 0;
	carg->read_counter = 0;
	carg->curr_ttl = carg->ttl;
	carg->key = malloc(255);
	snprintf(carg->key, 255, "udp:%s:%"PRIu16, addr, port);

	uv_udp_t *recv_socket = carg->udp_server = calloc(1, sizeof(*recv_socket));
	uv_udp_init(loop, recv_socket);
	struct sockaddr_in *recv_addr = calloc(1, sizeof(*recv_addr));
	uv_ip4_addr(addr, port, recv_addr);
	uv_udp_bind(recv_socket, (const struct sockaddr *)recv_addr, 0);
	recv_socket->data = carg;
	uv_udp_recv_start(recv_socket, alloc_buffer, udp_on_read);

	alligator_ht_insert(ac->entrypoints, &(carg->context_node), carg, tommy_strhash_u32(0, carg->key));
}

void udp_server_stop(const char* addr, uint16_t port)
{
	char key[255];
	snprintf(key, 255, "udp:%s:%"PRIu16, addr, port);
	context_arg *carg = alligator_ht_search(ac->entrypoints, entrypoint_compare, key, tommy_strhash_u32(0, key));
	if (carg)
	{
		uv_udp_recv_stop(carg->udp_server);
		uv_close((uv_handle_t*)carg->udp_server, NULL);
		alligator_ht_remove_existing(ac->entrypoints, &(carg->context_node));
	}
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

//void tftp_handler(char *metrics, size_t size, context_arg *carg);
//void udp_send(char *msg, size_t len)
//{
//	uv_buf_t *discover_msg = malloc(sizeof(*discover_msg));
//	discover_msg->base = msg;
//	discover_msg->len = len;
//
//	uv_udp_t *send_socket = malloc(sizeof(*send_socket));
//
//	uv_udp_send_t *send_req = malloc(sizeof(*send_req));
//	context_arg *carg = send_req->data = calloc(1, sizeof(context_arg));
//	carg->key = strdup("dcedc");
//	carg->parser_handler = tftp_handler;
//	uv_udp_init(uv_default_loop(), send_socket);
//
//	struct sockaddr_in *send_addr = malloc(sizeof(*send_addr));
//	uv_ip4_addr("127.0.0.1", 69, send_addr);
//
//	uv_udp_send(send_req, send_socket, discover_msg, 1, (struct sockaddr *)send_addr, udp_on_send);
//}

void udp_client_connect(void *arg)
{
	context_arg *carg = arg;
	carg->count = 0;
	if (ac->log_level > 1)
		printf("%"u64": udp client connect %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	carg->lock = 1;
	carg->parsed = 0;
	carg->curr_ttl = carg->ttl;

	//carg->tt_timer->data = carg;
	//uv_timer_init(carg->loop, carg->tt_timer);
	//uv_timer_start(carg->tt_timer, tcp_timeout_timer, carg->timeout, 0);

	//uv_udp_t *send_socket = malloc(sizeof(*send_socket));

	//uv_udp_send_t *send_req = malloc(sizeof(*send_req));
	carg->udp_send.data = carg;
	//send_req->data = carg;
	uv_udp_init(uv_default_loop(), &carg->udp_client);

	uv_udp_send(&carg->udp_send, &carg->udp_client, &carg->request_buffer, 1, (struct sockaddr *)carg->dest, udp_on_send);
}

void aggregator_getaddrinfo2(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
	context_arg* carg = (context_arg*)req->data;
	if (ac->log_level > 1)
		printf("getaddrinfo tcp client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);

	char addr[17] = {'\0'};
	if (status < 0)
	{
		uv_freeaddrinfo(res);
		free(req);
		return;
	}

	uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);

	carg->dest = (struct sockaddr_in*)res->ai_addr;
	carg->key = malloc(64);
	if (carg->transport == APROTO_UDP)
	{
		snprintf(carg->key, 64, "udp://%s:%u", addr, htons(carg->dest->sin_port));
		alligator_ht_insert(ac->udpaggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	}
	else
	{
		snprintf(carg->key, 64, "tcp://%s:%u", addr, htons(carg->dest->sin_port));
		alligator_ht_insert(ac->aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	}


	//uv_freeaddrinfo(res);
	free(req);
}

void aggregator_resolve_host2(context_arg* carg)
{
	if (ac->log_level > 1)
		printf("resolve host call tcp client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);
	struct addrinfo hints;
	uv_getaddrinfo_t* addr_info = 0;
	addr_info = malloc(sizeof(uv_getaddrinfo_t));
	addr_info->data = carg;

	hints.ai_family = PF_INET;
	if (carg->transport == APROTO_UDP)
	{
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
	}
	else
	{
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
	}
	hints.ai_flags = 0;

	if (uv_getaddrinfo(carg->loop, addr_info, aggregator_getaddrinfo2, carg->host, carg->port, &hints))
		free(addr_info);

}

char* udp_client(void *arg)
{
	if (!arg)
		return NULL;

	context_arg *carg = arg;

	aggregator_resolve_host2(carg);
	return "udp";
}

void udp_client_del(context_arg *carg)
{
	if (!carg)
		return;

	uv_udp_recv_stop(&carg->udp_client);
	alligator_ht_remove_existing(ac->udpaggregator, &(carg->node));
}

static void udp_client_crawl(uv_timer_t* handle) {
	(void)handle;
	alligator_ht_foreach(ac->udpaggregator, udp_client_connect);
}

void udp_client_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, udp_client_crawl, ac->aggregator_startup, ac->aggregator_repeat);
}
