#include <uv.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/un.h>
#include "main.h"
#include "client.h"
#include "common/url.h"

typedef struct { 
	uv_write_t req; 
	uv_buf_t buf; 
} write_req_t; 

void tcp_on_connect(uv_connect_t* connection, int status);
void tcp_timeout_timer(uv_timer_t *timer);
void on_close(uv_handle_t* handle);

void socket_conn(void* arg)
{
	if (!arg)
		return;
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	context_arg *carg = arg;
	uv_pipe_t *handle = malloc(sizeof(uv_pipe_t));
	carg->socket = (uv_tcp_t*)handle;
	uv_connect_t *connect = carg->connect = (uv_connect_t*)malloc(sizeof(uv_connect_t));

	carg->tt_timer->data = carg;
	uv_timer_init(loop, carg->tt_timer);
	uv_timer_start(carg->tt_timer, tcp_timeout_timer, 5000, 0);

	uv_pipe_init(loop, handle, 0); 
	connect->data = carg;
	uv_pipe_connect(connect, handle, carg->key, tcp_on_connect);
	carg->connect_time = setrtime();
}

static void uggregator_timer_cb(uv_timer_t* handle) {
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->uggregator, socket_conn);
}

void do_unix_client(char *unixsockaddr, void *handler, char *mesg, int proto, void *data)
{
	if (!unixsockaddr)
		return;
	if ( !validate_path(unixsockaddr, strlen(unixsockaddr)) )
		return;
	extern aconf* ac;
	context_arg *carg = malloc(sizeof(*carg));
	carg->proto = proto;
	carg->data = data;
	carg->parser_handler = handler;
	carg->tt_timer = malloc(sizeof(uv_timer_t));
	carg->mesg = mesg;
	if (mesg)
		carg->write = 1;
	else
		carg->write = 0;
	carg->port = NULL;
	carg->hostname = carg->key = unixsockaddr;

	tommy_hashdyn_insert(ac->uggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
}

void do_unix_client_buffer(char *unixsockaddr, void *handler, uv_buf_t *buffer, size_t buflen, int proto, void *data)
{
	if (!unixsockaddr)
		return;
	if ( !validate_path(unixsockaddr, strlen(unixsockaddr)) )
		return;
	extern aconf* ac;
	context_arg *carg = malloc(sizeof(*carg));
	carg->proto = proto;
	carg->data = data;
	carg->parser_handler = handler;
	carg->tt_timer = malloc(sizeof(uv_timer_t));
	carg->mesg = NULL;
	carg->write = 2;
	carg->buffer = buffer;
	carg->buflen = buflen;
	carg->port = NULL;
	carg->hostname = carg->key = unixsockaddr;

	tommy_hashdyn_insert(ac->uggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
}

void unix_client_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *uggregator_timer = calloc(1, sizeof(*uggregator_timer));
	uv_timer_init(loop, uggregator_timer);
	uv_timer_start(uggregator_timer, uggregator_timer_cb, 1000, 1000);
}
