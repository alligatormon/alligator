#ifndef _WIN32
#define _GNU_SOURCE
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
#include "main.h"

typedef struct unixgram_info
{
	char *key;
	struct sockaddr_un *local;
	socklen_t local_len;
	struct sockaddr_un *remote;
	socklen_t remote_len;
	int fd;
	void *parser_handler;
	char *mesg;

	tommy_node node;
} unixgram_info;

void unixgram_poll_cb(uv_poll_t* handle, int status, int events)
{
	if (status)
	{
		fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
		return;
	}
	uv_close((uv_handle_t*)handle, NULL);
}

void unixgram_cb(uv_poll_t* handle, int status, int events)
{
	if (status)
	{
		fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
		return;
	}
	unixgram_info *unfo = handle->data;
	char buf[65535];
	int size;
	if ( ( size = recvfrom(unfo->fd, buf, 65535, 0, (struct sockaddr*)unfo->local, &unfo->local_len)) <= 0 )
	{
		perror("recvfrom: ");
		printf("recvfrom err\n");
		uv_close((uv_handle_t*)handle, NULL);
		return;
	}
	buf[size]=0;
	//printf("buf %s\n", buf);
	alligator_multiparser(buf, size, unfo->parser_handler, NULL);

	uv_close((uv_handle_t*)handle, NULL);

	close(unfo->fd);
	printf("deleting file %s\n", unfo->local->sun_path);
	unlink(unfo->local->sun_path);
	free(unfo->remote);
	free(unfo->local);
}

void do_unixgram(void *arg)
{
	extern aconf* ac;

	if ( !arg )
		return;
	unixgram_info *unfo = arg;
	printf("run %s\n", unfo->key);
	printf("arg %p\n", unfo);
	//char *client_sock = malloc(255);

	socklen_t remote_len = sizeof(struct sockaddr_un);
	struct sockaddr_un *remote = calloc(1, remote_len);
	remote->sun_family = AF_UNIX;
	strcpy(remote->sun_path, unfo->key);
	unfo->remote_len = remote_len;
	unfo->remote = remote;

	size_t local_len = sizeof(struct sockaddr_un);
	struct sockaddr_un *local = calloc(1, local_len);
	local->sun_family = AF_UNIX;
	//strcpy(local->sun_path, client_sock);
	//free(client_sock);
	snprintf(local->sun_path, 255, "/tmp/alligator_%"d64".sock", ac->request_cnt);
	unlink(local->sun_path);
	unfo->local_len = local_len;
	unfo->local = local;

	int s;
	if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		perror("socket");
		return;
	}
	struct timeval tv_out; 
	tv_out.tv_sec = 1; 
	tv_out.tv_usec = 0; 
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out); 
	unfo->fd = s;

	int len = strlen(local->sun_path) + sizeof(local->sun_family);
	if (bind(s, (struct sockaddr *)local, len) == -1)
	{
		perror("bind");
		return;
	}

	if (sendto(s, unfo->mesg, strlen(unfo->mesg), 0, (struct sockaddr*)remote, remote_len) == -1)
	{
		perror("sendto");
		return;
	}
	printf("sent %zu bytes\n", strlen(unfo->mesg));
	uv_poll_t *poll_handler = calloc(1, sizeof(*poll_handler));
	poll_handler->data = unfo;
	uv_poll_init_socket(uv_default_loop(), poll_handler, s);
	uv_poll_start(poll_handler, UV_READABLE, unixgram_cb);

	uv_run(uv_default_loop(), 0);
}

void do_unixgram_client(char *unixsockaddr, void *handler, char *mesg)
{
	extern aconf* ac;
	unixgram_info *unfo = calloc(1, sizeof(*unfo));
	unfo->parser_handler = handler;
	unfo->mesg = mesg;
	unfo->key = unixsockaddr;

	tommy_hashdyn_insert(ac->unixgram_aggregator, &(unfo->node), unfo, tommy_strhash_u32(0, unfo->key));
}

static void udgregator_timer_cb(uv_timer_t* handle)
{
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->unixgram_aggregator, do_unixgram);
}

void unixgram_client_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *udgregator_timer = calloc(1, sizeof(*udgregator_timer));
	uv_timer_init(loop, udgregator_timer);
	uv_timer_start(udgregator_timer, udgregator_timer_cb, 1000, 1000);
}

// server
void unixgram_serve_cb(uv_poll_t* handle, int status, int events)
{
	if (status)
	{
		fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
		return;
	}
	unixgram_info *unfo = handle->data;
	char buf[65535];
	int size;
	if ( ( size = recvfrom(unfo->fd, buf, 65535, 0, (struct sockaddr*)unfo->local, &unfo->local_len)) <= 0 )
	{
		perror("recvfrom: ");
		return;
	}
	buf[size]=0;
	//printf("buf %s\n", buf);
	alligator_multiparser(buf, size, unfo->parser_handler, NULL);
}

void unixgram_server_handler(char *addr, void* parser_handler)
{
	//udp_server_info *udpinfo = malloc(sizeof(*udpinfo));
	//udpinfo->parser_handler = parser_handler;

	//socklen_t remote_len = sizeof(struct sockaddr_un);
	//struct sockaddr_un *remote = calloc(1, remote_len);
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
	unixgram_info *unfo = calloc(1, sizeof(*unfo));
	unfo->parser_handler = parser_handler;
	unfo->remote = 0;
	unfo->local = 0;
	unfo->fd = s;

	uv_poll_t *poll_handler = calloc(1, sizeof(*poll_handler));
	poll_handler->data = unfo;
	uv_poll_init_socket(uv_default_loop(), poll_handler, s);
	uv_poll_start(poll_handler, UV_READABLE, unixgram_serve_cb);

	//char buf[65535];
	//int n = recvfrom(s, buf, 65535, 0, (struct sockaddr*)remote, &remote_len);
	//if( n < 0)
	//{
	//	perror("recvfrom");
	//}
	//close(s);
}
#endif
