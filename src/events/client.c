#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "dstructures/tommyds/tommyds/tommy.h"
#include "main.h"
#include "client.h"
#include "parsers/multiparser.h"
#include "events/uv_alloc.h"
#include "common/rtime.h"
#include "common/url.h"
#include "parsers/http_proto.h"
#include "common/fastcgi.h"

void on_close(uv_handle_t* handle)
{
	extern aconf* ac;
	context_arg *carg = handle->data;
	if (ac->log_level > 2)
		printf("3: on_close carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);

	uv_close((uv_handle_t*)carg->tt_timer, NULL);
	free(carg->socket);
	free(carg->connect);
	//free(carg->tt_timer);
	carg->lock = 0;
}

void tcp_timeout_timer(uv_timer_t *timer)
{
	extern aconf* ac;
	context_arg *carg = timer->data;
	if (ac->log_level > 1)
		printf("4: timeout carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);
	carg->socket->data = carg;
	uv_close((uv_handle_t*)carg->socket, on_close);
}

void tcp_on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t *buf)
{
	context_arg *carg = tcp->data;
	extern aconf* ac;
	if (ac->log_level > 2)
		printf("2r: tcp_on_read carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);
	if(nread < 0 || !buf || !buf->base)
	{
		if (carg)
			uv_close((uv_handle_t*)tcp, on_close);
		else
			uv_close((uv_handle_t*)tcp, NULL);
		if (buf && buf->base)
			free(buf->base);

		return;
	}
	carg->read_time_finish = setrtime();
	int64_t connect_time = getrtime_i(carg->connect_time, carg->connect_time_finish);
	int64_t write_time = getrtime_i(carg->write_time, carg->write_time_finish);
	int64_t read_time = getrtime_i(carg->read_time, carg->read_time_finish);
	//printf("CWR %"d64":%"d64":%"d64"\n", connect_time, write_time, read_time);
	metric_add_labels3("aggregator_duration_time", &connect_time, DATATYPE_INT, carg, "proto", "tcp", "type", "connect", "url", carg->hostname);
	metric_add_labels3("aggregator_duration_time", &write_time, DATATYPE_INT, carg, "proto", "tcp", "type", "write", "url", carg->hostname);
	metric_add_labels3("aggregator_duration_time", &read_time, DATATYPE_INT, carg, "proto", "tcp", "type", "read", "url", carg->hostname);
	//fprintf(stderr, "read: %s (%zu)\n", buf->base, strlen(buf->base));

	if ((carg->proto == APROTO_HTTP || carg->proto == APROTO_HTTP_AUTH) && carg->http_body_size == 0)
	{
		http_reply_data* hr_data = http_reply_parser(buf->base, nread);
		if (!hr_data)
		{
			uv_close((uv_handle_t*)tcp, on_close);
			free(buf->base);
			return;
		}

		if (!hr_data->content_length)
		{
			alligator_multiparser(hr_data->body, nread, carg->parser_handler, NULL, carg);
			uv_close((uv_handle_t*)tcp, on_close);
			free(buf->base);
			http_reply_free(hr_data);
			return;
		}

		carg->expect_http_length = hr_data->content_length;
		carg->http_body_size += nread - (hr_data->body - buf->base);
		carg->http_body = malloc(hr_data->content_length+1);
		if (ac->log_level > 3)
			printf("allocate %p with content length = %zu\n", carg->http_body, hr_data->content_length);
		size_t msize = (hr_data->content_length > carg->http_body_size) ? carg->http_body_size : hr_data->content_length;
		strlcpy(carg->http_body, hr_data->body, msize+1);
		http_reply_free(hr_data);
	}
	else if ((carg->proto == APROTO_HTTP || carg->proto == APROTO_HTTP_AUTH) && carg->http_body_size > 0)
	{

		size_t sizecopy = nread+carg->http_body_size > carg->expect_http_length ? carg->expect_http_length-carg->http_body_size : nread;
		if (ac->log_level > 3)
			printf("copy to %p with offset %zu size of %zu with nornalized size %zu\n", carg->http_body, carg->http_body_size, nread, sizecopy);
		strlcpy(carg->http_body+carg->http_body_size, buf->base, sizecopy+1);
		carg->http_body_size += nread;
	}

	if ((carg->proto == APROTO_HTTP || carg->proto == APROTO_HTTP_AUTH) && carg->http_body_size >= carg->expect_http_length)
	{
		alligator_multiparser(carg->http_body, carg->http_body_size, carg->parser_handler, NULL, carg);
		uv_close((uv_handle_t*)tcp, on_close);
		free(carg->http_body);
		carg->http_body_size = carg->expect_http_length = 0;
	}
	else if (carg->proto == APROTO_FCGI || carg->proto == APROTO_FCGI_AUTH || carg->proto == APROTO_UNIXFCGI)
	{
		fcgi_reply_data* fcgi_reply = fcgi_reply_parser(buf->base, nread);
		alligator_multiparser(fcgi_reply->body, nread, carg->parser_handler, NULL, carg);
		uv_close((uv_handle_t*)tcp, on_close);
	}
	else if (carg->proto == APROTO_HTTP || carg->proto == APROTO_HTTP_AUTH)
	{
		uv_read_start(tcp, alloc_buffer, tcp_on_read);
	}
	else
	{
		alligator_multiparser(buf->base, nread, carg->parser_handler, NULL, carg);
		uv_close((uv_handle_t*)tcp, on_close);
	}

	free(buf->base);
}

void tcp_on_write(uv_write_t* req, int status)
{
	if (status)
	{
		fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
		free(req);
		//uv_close((uv_handle_t*)req->handle, on_close);
		//uv_close((uv_handle_t*)req->handle, NULL);
		return;
	}

	extern aconf* ac;

	context_arg *carg = req->data;
	if (ac->log_level > 2)
		printf("2w: tcp_on_write carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);
	carg->write_time_finish = setrtime();
	req->handle->data = carg;
	uv_read_start(req->handle, alloc_buffer, tcp_on_read);
	carg->read_time = setrtime();

	free(req);
}

void tcp_on_connect(uv_connect_t* connection, int status)
{
	if ( status < 0 )
		return;

	extern aconf* ac;

	context_arg *carg = connection->data;
	uv_stream_t* stream = connection->handle;
	connection->handle->data = carg;
	stream->data = carg;
	carg->connect_time_finish = setrtime();

	if (ac->log_level > 2)
		printf("1: tcp_on_connect carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);
	uv_read_start(stream, alloc_buffer, tcp_on_read);
	if (carg->write == 1)
	{
		uv_buf_t buffer = uv_buf_init(carg->mesg, strlen(carg->mesg));
		uv_write_t *request = malloc(sizeof(*request));
		request->data = carg;
		uv_write(request, stream, &buffer, 1, tcp_on_write);
		carg->write_time = setrtime();
	}
	else if (carg->write == 2)
	{
		uv_write_t *request = malloc(sizeof(*request));
		request->data = carg;

		uv_write(request, stream, carg->buffer, carg->buflen, tcp_on_write);
		carg->write_time = setrtime();
	}
}

void on_connect_handler(void* arg)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	context_arg *carg = arg;
	if (carg->lock)
		return;
	carg->lock = 1;

	if (carg->proto == APROTO_HTTP)
		carg->http_body_size = 0;

	uv_tcp_t *socket = carg->socket = malloc(sizeof(*socket));
	uv_tcp_init(loop, socket);
	uv_tcp_keepalive(socket, 1, 60);

	carg->tt_timer->data = carg;
	uv_timer_init(loop, carg->tt_timer);
	uv_timer_start(carg->tt_timer, tcp_timeout_timer, 5000, 0);

	uv_connect_t *connect = carg->connect = malloc(sizeof(*connect));
	if (ac->log_level > 2)
		printf("0: on_connect_handler carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);
	connect->data = carg;
	uv_tcp_connect(connect, socket, (struct sockaddr *)carg->dest, tcp_on_connect);
	carg->connect_time = setrtime();
}


static void timer_cb(uv_timer_t* handle) {
	(void)handle;
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->aggregator, on_connect_handler);
}

void tcp_on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
{
	extern aconf* ac;
	context_arg *carg = resolver->data;

	if (status == -1 || !res)
	{
		fprintf(stderr, "getaddrinfo callback error\n");
		return;
	}
	else
		if (ac->log_level > 2)
			printf("resolved %s\n", carg->hostname);

	char *addr = calloc(17, sizeof(*addr));
	uv_ip4_name((struct sockaddr_in*)res->ai_addr, addr, 16);
	carg->dest = (struct sockaddr_in*)res->ai_addr;
	carg->key = malloc(64);
	snprintf(carg->key, 64, "%s:%u:%d", addr, carg->dest->sin_port, carg->dest->sin_family);

	tommy_hashdyn_insert(ac->aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
}

void do_tcp_client(char *addr, char *port, void *handler, char *mesg, int proto, void *data)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->data = data;

	if (ac->log_level > 2)
		printf("allocated CINFO with addr %p with hostname '%s' with mesg '%s'\n", carg, addr, mesg);
	carg->mesg = mesg;
	if (mesg)
		carg->write = 1;
	else
		carg->write = 0;
	carg->parser_handler = handler;
	carg->hostname = addr;
	carg->proto = proto;
	carg->port = port;
	carg->tt_timer = malloc(sizeof(uv_timer_t));

	uv_getaddrinfo_t *resolver = malloc(sizeof(*resolver));
	resolver->data = carg;
	int r = uv_getaddrinfo(loop, resolver, tcp_on_resolved, addr, port, NULL);
	if (r)
	{
		fprintf(stderr, "%s\n", uv_strerror(r));
		return;
	}
	else
	{
		(ac->tcp_client_count)++;
	}
}

void do_tcp_client_buffer(char *addr, char *port, void *handler, uv_buf_t* buffer, size_t buflen, int proto, void *data)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->data = data;

	if (ac->log_level > 2)
		printf("allocated CINFO with addr %p with hostname '%s' with buf '%p'\n", carg, addr, buffer);
	carg->buffer = buffer;
	carg->buflen = buflen;
	carg->write = 2;
	carg->parser_handler = handler;
	carg->hostname = addr;
	carg->proto = proto;
	carg->port = port;
	carg->tt_timer = malloc(sizeof(uv_timer_t));

	uv_getaddrinfo_t *resolver = malloc(sizeof(*resolver));
	resolver->data = carg;
	int r = uv_getaddrinfo(loop, resolver, tcp_on_resolved, addr, port, NULL);
	if (r)
	{
		fprintf(stderr, "%s\n", uv_strerror(r));
		return;
	}
	else
	{
		(ac->tcp_client_count)++;
	}
}

void do_tcp_client_carg(context_arg *carg)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_getaddrinfo_t *resolver = malloc(sizeof(*resolver));
	resolver->data = carg;
	int r = uv_getaddrinfo(loop, resolver, tcp_on_resolved, carg->hostname, carg->port, NULL);
	if (r)
	{
		fprintf(stderr, "%s\n", uv_strerror(r));
		return;
	}
	else
	{
		(ac->tcp_client_count)++;
	}
}

void tcp_client_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, timer_cb, ac->aggregator_startup, ac->aggregator_repeat);
}
