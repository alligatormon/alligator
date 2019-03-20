#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "events/a_signal.h"
#include "events/uv_alloc.h"
#include "parsers/multiparser.h"
static uv_loop_t * loop;
/* 
 * Stores everything about a request
 */
typedef struct client_request_data {
	time_t start;
	char *text;
	size_t text_len;
	char *response;
	int work_started;
	uv_tcp_t *client;
	uv_work_t *work_req;
	uv_write_t *write_req;
	uv_timer_t *timer;
	const uv_buf_t *buf;
	void *parser_handler;
} client_request_data;
/* Allocate buffers as requested by UV */
//static void alloc_buffer(uv_handle_t * handle, size_t size, uv_buf_t *buf) {
//	char *base;
//	base = (char *) calloc(1, size);
//	if(!base)
//		*buf = uv_buf_init(NULL, 0);
//	else
//		*buf = uv_buf_init(base, size);
//}

/* Callback to free the handle */
static void on_close_free(uv_handle_t* handle)
{
	free(handle);
}
/* 
 * Callback for freeing up all resources allocated for request   
 */
static void close_data(client_request_data *data)
{
	if(!data)
		return;
	if(data->client)
		uv_close((uv_handle_t *)data->client, 
		       on_close_free);
	if(data->work_req)
		free(data->work_req);
	if(data->write_req)
		free(data->write_req);
	if(data->timer)
		uv_close((uv_handle_t *)data->timer, on_close_free);
	if(data->text)
		free(data->text);
	if(data->response)
		free(data->response);
	free(data);
}
/*
 *  Callback for when the TCP write is complete
 */
static void on_write_end(uv_write_t *req, int status) {
	if (status)
		fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
	client_request_data *data;
	data = req->data;
	close_data(data);
}
/* 
 * Callback for post completion of the work associated with the 
 * request
 */
static void after_process_command(uv_work_t *req, int status) {
	if (status)
		fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
	client_request_data *data;
	data = req->data;
	uv_buf_t buf = uv_buf_init(data->response, strlen(data->response)+1);
	data->write_req = malloc(sizeof(*data->write_req));
	data->write_req->data = data;
	uv_timer_stop(data->timer);
	uv_write(data->write_req, (uv_stream_t *)data->client, &buf, 1, on_write_end);
}
/*
 * Callback for doing the actual work. 
 */
static void process_command(uv_work_t *req) {
	client_request_data *data;
	data = req->data;
	// TODO calculate body answer size by abount metric count
	if (data && data->buf && data->buf->base)
	{
		char *answ = malloc(MAX_RESPONSE_SIZE);
		alligator_multiparser(data->buf->base, strlen(data->buf->base), NULL, answ);
		free(data->buf->base);
		data->response = answ; //strdup("HTTP/1.1 200 OK\n\nHello World, work's done\n\n");
	}
}
/* Callback for read function, called multiple times per request */
static void read_cb(uv_stream_t * stream, ssize_t nread, const uv_buf_t *buf) {
	client_request_data *data;
	char *tmp;
	data = stream->data;
	if (nread == -1 || nread==UV_EOF) {
		free(buf->base);
		uv_timer_stop(data->timer);		
		close_data(data);
	} else {
		if(!data->text) {
			data->text = malloc(nread+1);
			memcpy(data->text, buf->base, nread);
			data->text[nread] = '\0';
			data->text_len = nread;
		} else {
			tmp = realloc(data->text, data->text_len + nread + 1);
			memcpy(tmp + data->text_len, buf->base, nread);
			tmp[data->text_len + nread] = '\0';
			data->text = tmp;
			data->text_len += nread;
		}
		if(!data->work_started && data->text_len && (strstr(data->text,"\r\n") || strstr(data->text,"\n\n"))) {
			data->work_req = malloc(sizeof(*data->work_req));
			data->buf = buf;
			data->work_req->data = data;
			data->work_started = 1;
			uv_read_stop(stream);
			uv_queue_work(loop, data->work_req, process_command, after_process_command);
		}
	}
}
/* Callback for the timer which signifies a timeout */
static void client_timeout_cb(uv_timer_t* handle)
{
	struct client_request_data *data;
	data = (struct client_request_data *)handle->data;
	uv_timer_stop(handle);
	if(data->work_started)
		return;
	close_data(data);
}
/* Callback for handling the new connection */
static void connection_cb(uv_stream_t * server, int status) {
	/* if status not zero there was an error */
	if (status == -1) {
		return;
	}
	uv_tcp_t * client = malloc(sizeof(uv_tcp_t));
	struct client_request_data *data = server->data;
	data->start = time(NULL);
	client->data = data;
	data->client = client;
	/* initialize the new client */
	uv_tcp_init(loop, client);
	if (uv_accept(server, (uv_stream_t *) client) == 0)
	{
		/* start reading from stream */
		uv_timer_t *timer;
		timer = malloc(sizeof(*timer));
		timer->data = data;
		data->timer = timer;
		uv_timer_init(loop, timer);
		uv_timer_set_repeat(timer, 1);
		uv_timer_start(timer, client_timeout_cb, 10000, 20000);
		uv_read_start((uv_stream_t *) client, alloc_buffer, read_cb);
	} else {
		/* close client stream on error */
		close_data(data);
	}
}


//void on_after_work(uv_work_t* req, int status) {
//	free(req);
//}
//void on_work1(uv_work_t* req)
//{
//	printf("Sleeping...\n");
//	sleep(1);
//	//printf("UnSleeping...\n");
//}
//void on_timer1(uv_timer_t* timer) {
//	uint64_t timestamp = uv_hrtime();
//	//printf("on_timer [%"PRIu64" ms]\n", (timestamp / 1000000) % 100000);
//	uv_work_t* work_req = (uv_work_t*)malloc(sizeof(*work_req));
//	uv_queue_work(uv_default_loop(), work_req, on_work1, on_after_work);
//}

void tcp_server_handler(char *addr, uint16_t port, void* handler)
{
	(void)handler;
	loop = uv_default_loop();
	struct sockaddr_in *soaddr = calloc(1, sizeof(*soaddr));
	uv_tcp_t *server = calloc(1, sizeof(*server));
	uv_ip4_addr(addr, port, soaddr);
	/* initialize the server */
	uv_tcp_init(loop, server);
	/* bind the server to the address above */
	uv_tcp_bind(server, (struct sockaddr *)soaddr, 0);
	struct client_request_data *data = calloc(1, sizeof(*data));
	data->parser_handler = handler;
	server->data = data;
	int r = uv_listen((uv_stream_t *) server, 128, connection_cb);
	if (r)
	{
		fprintf(stderr, "Error on listening: %s.\n", uv_strerror(r));
		return;
	}
}
