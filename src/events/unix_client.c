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
	client_info *cinfo = arg;
	uv_pipe_t *handle = malloc(sizeof(uv_pipe_t));
	cinfo->socket = (uv_tcp_t*)handle;
	uv_connect_t *connect = cinfo->connect = (uv_connect_t*)malloc(sizeof(uv_connect_t));

	cinfo->tt_timer->data = cinfo;
	uv_timer_init(loop, cinfo->tt_timer);
	uv_timer_start(cinfo->tt_timer, tcp_timeout_timer, 5000, 0);

	uv_pipe_init(loop, handle, 0); 
	connect->data = cinfo;
	uv_pipe_connect(connect, handle, cinfo->key, tcp_on_connect);
	cinfo->connect_time = setrtime();
}

static void uggregator_timer_cb(uv_timer_t* handle) {
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->uggregator, socket_conn);
}

void do_unix_client(char *unixsockaddr, void *handler, char *mesg, int proto)
{
	if (!unixsockaddr)
		return;
	if ( !validate_path(unixsockaddr, strlen(unixsockaddr)) )
		return;
	extern aconf* ac;
	client_info *cinfo = malloc(sizeof(*cinfo));
	cinfo->proto = proto;
	cinfo->parser_handler = handler;
	cinfo->tt_timer = malloc(sizeof(uv_timer_t));
	cinfo->mesg = mesg;
	if (mesg)
		cinfo->write = 1;
	else
		cinfo->write = 0;
	cinfo->port = NULL;
	cinfo->hostname = cinfo->key = unixsockaddr;

	tommy_hashdyn_insert(ac->uggregator, &(cinfo->node), cinfo, tommy_strhash_u32(0, cinfo->key));
}

void do_unix_client_buffer(char *unixsockaddr, void *handler, uv_buf_t *buffer, size_t buflen, int proto)
{
	if (!unixsockaddr)
		return;
	if ( !validate_path(unixsockaddr, strlen(unixsockaddr)) )
		return;
	extern aconf* ac;
	client_info *cinfo = malloc(sizeof(*cinfo));
	cinfo->proto = proto;
	cinfo->parser_handler = handler;
	cinfo->tt_timer = malloc(sizeof(uv_timer_t));
	cinfo->mesg = NULL;
	cinfo->write = 2;
	cinfo->buffer = buffer;
	cinfo->buflen = buflen;
	cinfo->port = NULL;
	cinfo->hostname = cinfo->key = unixsockaddr;

	tommy_hashdyn_insert(ac->uggregator, &(cinfo->node), cinfo, tommy_strhash_u32(0, cinfo->key));
}

void unix_client_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *uggregator_timer = calloc(1, sizeof(*uggregator_timer));
	uv_timer_init(loop, uggregator_timer);
	uv_timer_start(uggregator_timer, uggregator_timer_cb, 1000, 1000);
}
