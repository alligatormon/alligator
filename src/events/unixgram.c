#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <uv.h>
#include <stdlib.h>
#include "cluster/later.h"
#include "parsers/multiparser.h"
#include "common/entrypoint.h"
#include "common/logs.h"
#include "main.h"

extern aconf* ac;

void unixgram_cb(uv_poll_t* handle, int status, int events)
{
	context_arg *carg = handle->data;
	if (status)
	{
		carglog(carg, L_ERROR, "uv_write error: %s\n", uv_strerror(status));
		return;
	}
	char buf[65535];
	int size;
	if ((size = recvfrom(carg->fd, buf, 65535, 0, (struct sockaddr*)carg->local, &carg->local_len)) <= 0)
	{
		if (carg->log_level > 1) {
			perror("recvfrom: ");
			printf("recvfrom err\n");
		}
		uv_close((uv_handle_t*)handle, NULL);
		return;
	}
	buf[size]=0;
	alligator_multiparser(buf, size, carg->parser_handler, NULL, NULL);

	uv_close((uv_handle_t*)handle, NULL);

	(carg->read_counter)++;
	metric_add_labels("unixgram_entrypoint_read", &carg->read_counter, DATATYPE_UINT, carg, "entrypoint", carg->key);

	close(carg->fd);
	if (carg->log_level > 0)
		printf("deleting file %s\n", carg->local->sun_path);
	unlink(carg->local->sun_path);
	free(carg->remote);
	free(carg->local);

	++carg->read_counter;
	if (carg->period)
		uv_timer_set_repeat(carg->period_timer, carg->period);
}

void unixgram_client_repeat_period(uv_timer_t *timer);
void do_unixgram(void *arg)
{
	if (!arg)
		return;

	context_arg *carg = arg;
	if (carg->log_level > 1) {
		printf("run %s\n", carg->key);
		printf("arg %p\n", carg);
	}
	//char *client_sock = malloc(255);

	if (cluster_come_later(carg))
		return;

	if (carg->period && !carg->read_counter) {
		carg->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->period_timer->data = carg;
		uv_timer_init(carg->loop, carg->period_timer);
		uv_timer_start(carg->period_timer, unixgram_client_repeat_period, carg->period, 0);
	}

	carg->parser_status = 0;
	socklen_t remote_len = sizeof(struct sockaddr_un);
	struct sockaddr_un *remote = calloc(1, remote_len);
	remote->sun_family = AF_UNIX;
	strcpy(remote->sun_path, carg->key);
	carg->remote_len = remote_len;
	carg->remote = remote;

	size_t local_len = sizeof(struct sockaddr_un);
	struct sockaddr_un *local = calloc(1, local_len);
	local->sun_family = AF_UNIX;
	//strcpy(local->sun_path, client_sock);
	//free(client_sock);
	snprintf(local->sun_path, 108, "/tmp/alligator_%"u64".sock", ac->request_cnt);
	unlink(local->sun_path);
	carg->local_len = local_len;
	carg->local = local;

	int s;
	if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		if (carg->log_level > 0)
			perror("socket");
		return;
	}
	struct timeval tv_out; 
	tv_out.tv_sec = 1; 
	tv_out.tv_usec = 0; 
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out); 
	carg->fd = s;

	int len = strlen(local->sun_path) + sizeof(local->sun_family);
	if (bind(s, (struct sockaddr *)local, len) == -1)
	{
		if (carg->log_level > 0)
			perror("bind");
		free(remote);
		free(local);
		return;
	}

	if (sendto(s, carg->mesg, strlen(carg->mesg), 0, (struct sockaddr*)remote, remote_len) == -1)
	{
		if (carg->log_level > 0)
			perror("sendto");
		free(remote);
		free(local);
		return;
	}

	if (carg->log_level > 0)
		printf("sent %zu bytes\n", strlen(carg->mesg));

	uv_poll_t *poll_handler = calloc(1, sizeof(*poll_handler));
	poll_handler->data = carg;
	uv_poll_init_socket(uv_default_loop(), poll_handler, s);
	uv_poll_start(poll_handler, UV_READABLE, unixgram_cb);

	uv_run(uv_default_loop(), 0);
}

void unixgram_client_repeat_period(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (!carg->period)
		return;

	do_unixgram((void*)carg);
}

void for_unixgram_client_connect(void *arg)
{
	context_arg *carg = arg;
	if (carg->period && carg->read_counter)
		return;

	do_unixgram(arg);
}

void do_unixgram_client(char *unixsockaddr, void *handler, char *mesg)
{
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->parser_handler = handler;
	carg->mesg = mesg;

	alligator_ht_insert(ac->unixgram_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
}

static void udgregator_timer_cb(uv_timer_t* handle)
{
	alligator_ht_foreach(ac->unixgram_aggregator, for_unixgram_client_connect);
}

void unixgram_client_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->udgregator_timer);
	uv_timer_start(&ac->udgregator_timer, udgregator_timer_cb, 1000, 1000);
}

// server
void unixgram_serve_cb(uv_poll_t* handle, int status, int events)
{
	context_arg *carg = handle->data;
	if (status)
	{
		carglog(carg, L_ERROR, "uv_write error: %s\n", uv_strerror(status));
		return;
	}
	char buf[65535];
	int size;
	if ( ( size = recvfrom(carg->fd, buf, 65535, 0, (struct sockaddr*)carg->local, &carg->local_len)) <= 0 )
	{
		perror("recvfrom: ");
		return;
	}
	buf[size]=0;
	alligator_multiparser(buf, size, carg->parser_handler, NULL, NULL);
}

void unixgram_server_init(uv_loop_t *loop, char *addr, context_arg *carg)
{
	if (!carg->key)
		carg->key = malloc(255);
	snprintf(carg->key, 255, "unixgram:%s", addr);

	int s = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (s == -1)
	{
		perror("socket");
		return;
	}
	size_t local_len = sizeof(struct sockaddr_un);
	struct sockaddr_un *local = calloc(1, local_len);
	local->sun_family = AF_UNIX;
	strcpy(local->sun_path, addr);
	unlink(local->sun_path);
	if (bind(s, (struct sockaddr *)local, local_len) == -1)
	{
		perror("bind");
		return;
	}
	carg->remote = 0;
	carg->local = 0;
	carg->fd = s;

	uv_poll_t *poll_handler = carg->poll = calloc(1, sizeof(*poll_handler));
	poll_handler->data = carg;
	uv_poll_init_socket(loop, poll_handler, s);
	uv_poll_start(poll_handler, UV_READABLE, unixgram_serve_cb);

	alligator_ht_insert(ac->entrypoints, &(carg->context_node), carg, tommy_strhash_u32(0, carg->key));
}

void unixgram_server_stop(const char* addr)
{
	char key[255];
	snprintf(key, 255, "unixgram:%s", addr);
	context_arg *carg = alligator_ht_search(ac->entrypoints, entrypoint_compare, key, tommy_strhash_u32(0, key));
	if (carg)
	{
		uv_poll_stop(carg->poll);
		unlink(addr);
		alligator_ht_remove_existing(ac->entrypoints, &(carg->context_node));
	}
}
