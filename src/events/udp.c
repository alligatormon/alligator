#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/entrypoint.h"
#include "events/metrics.h"
#include "resolver/resolver.h"
#include "events/access.h"
#include "cluster/later.h"
#include "parsers/multiparser.h"
#include "main.h"
extern aconf *ac;

void udp_on_read(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
	context_arg *carg = req->data;
	carg->read_time_finish = setrtime();
	if (carg->log_level > 1)
		printf("%"u64": udp readed %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

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
		aggregator_events_metric_add(carg, carg, NULL, "entrypoint", "aggregator", carg->host);
	}

	carg->lock = 0;
	free(buf->base);
}

void udp_on_send(uv_udp_send_t* req, int status) {
	if (status != 0) {
		fprintf(stderr, "send_cb error: %s\n", uv_strerror(status));
	}
	context_arg *carg = req->data;
	carg->write_time_finish = setrtime();
	if (carg->log_level > 1)
		printf("%"u64": udp sended %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	req->handle->data = req->data;

	uv_udp_recv_start(req->handle, alloc_buffer, udp_on_read);
	carg->read_time = setrtime();
	//free(req);
}

void udp_server_init(uv_loop_t *loop, const char* addr, uint16_t port, uint8_t tls, context_arg *carg)
{
	if (ac->log_level > 1)
		printf("init udp server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", NULL, 0, carg, addr, port);

	carg->conn_counter = 0;
	carg->read_counter = 0;
	carg->curr_ttl = carg->ttl;
	carg->key = malloc(255);
	strlcpy(carg->host, addr, HOSTHEADER_SIZE);
	snprintf(carg->key, 255, "udp:%s:%"PRIu16, carg->host, port);

	uv_udp_t *recv_socket = &carg->udp_server;
	uv_udp_init(loop, recv_socket);
	struct sockaddr_in *recv_addr = carg->recv = calloc(1, sizeof(*recv_addr));
	uv_ip4_addr(carg->host, port, recv_addr);
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
		uv_udp_recv_stop(&carg->udp_server);
		uv_close((uv_handle_t*)&carg->udp_server, NULL);
		alligator_ht_remove_existing(ac->entrypoints, &(carg->context_node));
	}
}

void udp_client_connect(void *arg)
{
	context_arg *carg = arg;
	carg->count = 0;
	if (carg->log_level > 1)
		printf("%"u64": udp client connect %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	if (carg->lock)
		return;
	if (cluster_come_later(carg))
		return;

	carg->lock = 1;
	carg->parsed = 0;
	carg->parser_status = 0;
	carg->curr_ttl = carg->ttl;

	string *data = aggregator_get_addr(carg, carg->host, DNS_TYPE_A, DNS_CLASS_IN);
	if (!data)
	{
		carg->lock = 0;
		return;
	}

	struct sockaddr_in res;
	uv_ip4_addr(data->s, carg->numport, &res);
	carg->dest = &res;

	//carg->tt_timer->data = carg;
	//uv_timer_init(carg->loop, carg->tt_timer);
	//uv_timer_start(carg->tt_timer, tcp_timeout_timer, carg->timeout, 0);

	//uv_udp_t *send_socket = malloc(sizeof(*send_socket));

	//uv_udp_send_t *send_req = malloc(sizeof(*send_req));
	carg->udp_send.data = carg;
	//send_req->data = carg;
	uv_udp_init(uv_default_loop(), &carg->udp_client);

	uv_udp_send(&carg->udp_send, &carg->udp_client, &carg->request_buffer, 1, (struct sockaddr *)carg->dest, udp_on_send);
	carg->write_time = setrtime();
}

char* udp_client(void *arg)
{
	if (!arg)
		return NULL;

	context_arg *carg = arg;
	aggregator_get_addr(carg, carg->host, DNS_TYPE_A, DNS_CLASS_IN);

	alligator_ht_insert(ac->udpaggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
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

	uv_timer_init(loop, &ac->udp_client_timer);
	uv_timer_start(&ac->udp_client_timer, udp_client_crawl, ac->aggregator_startup, ac->aggregator_repeat);
}
