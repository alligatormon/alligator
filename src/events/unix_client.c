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

void on_connect(uv_connect_t* connection, int status);

//void echo_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
//	if (nread > 0) {
//		//write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t));
//		//req->buf = uv_buf_init(buf->base, nread);
//		//uv_write((uv_write_t*) req, client, &req->buf, 1, echo_write);
//		printf("b: %s\n", buf->base);
//		return;
//	}
//
//	if (nread < 0) {
//		if (nread != UV_EOF)
//			fprintf(stderr, "Read error %s\n", uv_err_name(nread));
//		uv_close((uv_handle_t*) client, NULL);
//	}
//
//	free(buf->base);
//}
//
//void on_write(uv_write_t *req, int status) { 
//	if (status)
//	{
//		fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
//		return;
//	}
//	//req->handle->data = req->data;
//	uv_read_start(req->handle, alloc_buffer, echo_read);
//
//	free(req);
//} 
//
//void on_connect(uv_connect_t* connect, int status){ 
//	if (status < 0) { 
//	printf("failed!"); 
//	} else { 
//	printf("connected! sending msg..."); 
//	write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t)); 
//	req->buf = uv_buf_init("Hello World!", 13); 
//	uv_write((uv_write_t*) req, connect->handle, &req->buf, 1, on_write); 
//	} 
//} 

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

	uv_pipe_init(loop, handle, 0); 

	connect->data = cinfo;
	uv_pipe_connect(connect, handle, cinfo->key, on_connect); 
	cinfo->connect_time = setrtime();
}

static void uggregator_timer_cb(uv_timer_t* handle) {
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->uggregator, socket_conn);
}

void do_unix_client(char *unixsockaddr, void *handler, char *mesg)
{
	if (!unixsockaddr)
		return;
	if ( !validate_path(unixsockaddr, strlen(unixsockaddr)) )
		return;
	extern aconf* ac;
	client_info *cinfo = malloc(sizeof(*cinfo));
	cinfo->proto = APROTO_UNIX;
	cinfo->parser_handler = handler;
	cinfo->mesg = mesg;
	if (mesg)
		cinfo->write = 1;
	else
		cinfo->write = 0;
	cinfo->port = NULL;
	cinfo->hostname = cinfo->key = unixsockaddr;

	tommy_hashdyn_insert(ac->uggregator, &(cinfo->node), cinfo, tommy_strhash_u32(0, cinfo->key));
}

void do_unix_client_buffer(char *unixsockaddr, void *handler, uv_buf_t *buffer, size_t buflen)
{
	if (!unixsockaddr)
		return;
	if ( !validate_path(unixsockaddr, strlen(unixsockaddr)) )
		return;
	extern aconf* ac;
	client_info *cinfo = malloc(sizeof(*cinfo));
	cinfo->proto = APROTO_UNIX;
	cinfo->parser_handler = handler;
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
