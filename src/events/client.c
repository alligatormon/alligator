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
	client_info *cinfo = handle->data;
	if (ac->log_level > 2)
		printf("3: on_close cinfo with addr %p(%p:%p) with key %s, hostname %s\n", cinfo, cinfo->socket, cinfo->connect, cinfo->key, cinfo->hostname);

	uv_close((uv_handle_t*)cinfo->tt_timer, NULL);
	free(cinfo->socket);
	free(cinfo->connect);
	//free(cinfo->tt_timer);
	cinfo->lock = 0;
}

void tcp_timeout_timer(uv_timer_t *timer)
{
	extern aconf* ac;
	client_info *cinfo = timer->data;
	if (ac->log_level > 1)
		printf("4: timeout cinfo with addr %p(%p:%p) with key %s, hostname %s\n", cinfo, cinfo->socket, cinfo->connect, cinfo->key, cinfo->hostname);
	cinfo->socket->data = cinfo;
	uv_close((uv_handle_t*)cinfo->socket, on_close);
}

void tcp_on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t *buf)
{
	client_info *cinfo = tcp->data;
	extern aconf* ac;
	if (ac->log_level > 2)
		printf("2r: tcp_on_read cinfo with addr %p(%p:%p) with key %s, hostname %s\n", cinfo, cinfo->socket, cinfo->connect, cinfo->key, cinfo->hostname);
	if(nread < 0 || !buf || !buf->base)
	{
		if (cinfo)
			uv_close((uv_handle_t*)tcp, on_close);
		else
			uv_close((uv_handle_t*)tcp, NULL);
		if (buf && buf->base)
			free(buf->base);

		return;
	}
	cinfo->read_time_finish = setrtime();
	int64_t connect_time = getrtime_i(cinfo->connect_time, cinfo->connect_time_finish);
	int64_t write_time = getrtime_i(cinfo->write_time, cinfo->write_time_finish);
	int64_t read_time = getrtime_i(cinfo->read_time, cinfo->read_time_finish);
	//printf("CWR %"d64":%"d64":%"d64"\n", connect_time, write_time, read_time);
	metric_add_labels3("aggregator_duration_time", &connect_time, DATATYPE_INT, 0, "proto", "tcp", "type", "connect", "url", cinfo->hostname);
	metric_add_labels3("aggregator_duration_time", &write_time, DATATYPE_INT, 0, "proto", "tcp", "type", "write", "url", cinfo->hostname);
	metric_add_labels3("aggregator_duration_time", &read_time, DATATYPE_INT, 0, "proto", "tcp", "type", "read", "url", cinfo->hostname);
	//fprintf(stderr, "read: %s (%zu)\n", buf->base, strlen(buf->base));

	if ((cinfo->proto == APROTO_HTTP || cinfo->proto == APROTO_HTTP_AUTH) && cinfo->http_body_size == 0)
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
			alligator_multiparser(hr_data->body, nread, cinfo->parser_handler, NULL, cinfo);
			uv_close((uv_handle_t*)tcp, on_close);
			free(buf->base);
			http_reply_free(hr_data);
			return;
		}

		cinfo->expect_http_length = hr_data->content_length;
		cinfo->http_body_size += nread - (hr_data->body - buf->base);
		cinfo->http_body = malloc(hr_data->content_length+1);
		if (ac->log_level > 3)
			printf("allocate %p with content length = %zu\n", cinfo->http_body, hr_data->content_length);
		size_t msize = (hr_data->content_length > cinfo->http_body_size) ? cinfo->http_body_size : hr_data->content_length;
		strlcpy(cinfo->http_body, hr_data->body, msize+1);
		http_reply_free(hr_data);
	}
	else if ((cinfo->proto == APROTO_HTTP || cinfo->proto == APROTO_HTTP_AUTH) && cinfo->http_body_size > 0)
	{

		size_t sizecopy = nread+cinfo->http_body_size > cinfo->expect_http_length ? cinfo->expect_http_length-cinfo->http_body_size : nread;
		if (ac->log_level > 3)
			printf("copy to %p with offset %zu size of %zu with nornalized size %zu\n", cinfo->http_body, cinfo->http_body_size, nread, sizecopy);
		strlcpy(cinfo->http_body+cinfo->http_body_size, buf->base, sizecopy+1);
		cinfo->http_body_size += nread;
	}

	if ((cinfo->proto == APROTO_HTTP || cinfo->proto == APROTO_HTTP_AUTH) && cinfo->http_body_size >= cinfo->expect_http_length)
	{
		alligator_multiparser(cinfo->http_body, cinfo->http_body_size, cinfo->parser_handler, NULL, cinfo);
		uv_close((uv_handle_t*)tcp, on_close);
		free(cinfo->http_body);
		cinfo->http_body_size = cinfo->expect_http_length = 0;
	}
	else if (cinfo->proto == APROTO_FCGI || cinfo->proto == APROTO_FCGI_AUTH || cinfo->proto == APROTO_UNIXFCGI)
	{
		fcgi_reply_data* fcgi_reply = fcgi_reply_parser(buf->base, nread);
		alligator_multiparser(fcgi_reply->body, nread, cinfo->parser_handler, NULL, cinfo);
		uv_close((uv_handle_t*)tcp, on_close);
	}
	else if (cinfo->proto == APROTO_HTTP || cinfo->proto == APROTO_HTTP_AUTH)
	{
		uv_read_start(tcp, alloc_buffer, tcp_on_read);
	}
	else
	{
		alligator_multiparser(buf->base, nread, cinfo->parser_handler, NULL, cinfo);
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

	client_info *cinfo = req->data;
	if (ac->log_level > 2)
		printf("2w: tcp_on_write cinfo with addr %p(%p:%p) with key %s, hostname %s\n", cinfo, cinfo->socket, cinfo->connect, cinfo->key, cinfo->hostname);
	cinfo->write_time_finish = setrtime();
	req->handle->data = cinfo;
	uv_read_start(req->handle, alloc_buffer, tcp_on_read);
	cinfo->read_time = setrtime();

	free(req);
}

void tcp_on_connect(uv_connect_t* connection, int status)
{
	if ( status < 0 )
		return;

	extern aconf* ac;

	client_info *cinfo = connection->data;
	uv_stream_t* stream = connection->handle;
	connection->handle->data = cinfo;
	stream->data = cinfo;
	cinfo->connect_time_finish = setrtime();

	if (ac->log_level > 2)
		printf("1: tcp_on_connect cinfo with addr %p(%p:%p) with key %s, hostname %s\n", cinfo, cinfo->socket, cinfo->connect, cinfo->key, cinfo->hostname);
	uv_read_start(stream, alloc_buffer, tcp_on_read);
	if (cinfo->write == 1)
	{
		uv_buf_t buffer = uv_buf_init(cinfo->mesg, strlen(cinfo->mesg));
		uv_write_t *request = malloc(sizeof(*request));
		request->data = cinfo;
		uv_write(request, stream, &buffer, 1, tcp_on_write);
		cinfo->write_time = setrtime();
	}
	else if (cinfo->write == 2)
	{
		uv_write_t *request = malloc(sizeof(*request));
		request->data = cinfo;

		uv_write(request, stream, cinfo->buffer, cinfo->buflen, tcp_on_write);
		cinfo->write_time = setrtime();
	}
}

void on_connect_handler(void* arg)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	client_info *cinfo = arg;
	if (cinfo->lock)
		return;
	cinfo->lock = 1;

	if (cinfo->proto == APROTO_HTTP)
		cinfo->http_body_size = 0;

	uv_tcp_t *socket = cinfo->socket = malloc(sizeof(*socket));
	uv_tcp_init(loop, socket);
	uv_tcp_keepalive(socket, 1, 60);

	cinfo->tt_timer->data = cinfo;
	uv_timer_init(loop, cinfo->tt_timer);
	uv_timer_start(cinfo->tt_timer, tcp_timeout_timer, 5000, 0);

	uv_connect_t *connect = cinfo->connect = malloc(sizeof(*connect));
	if (ac->log_level > 2)
		printf("0: on_connect_handler cinfo with addr %p(%p:%p) with key %s, hostname %s\n", cinfo, cinfo->socket, cinfo->connect, cinfo->key, cinfo->hostname);
	connect->data = cinfo;
	uv_tcp_connect(connect, socket, (struct sockaddr *)cinfo->dest, tcp_on_connect);
	cinfo->connect_time = setrtime();
}


static void timer_cb(uv_timer_t* handle) {
	(void)handle;
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->aggregator, on_connect_handler);
}

void tcp_on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
{
	extern aconf* ac;
	client_info *cinfo = resolver->data;

	if (status == -1 || !res)
	{
		fprintf(stderr, "getaddrinfo callback error\n");
		return;
	}
	else
		if (ac->log_level > 2)
			printf("resolved %s\n", cinfo->hostname);

	char *addr = calloc(17, sizeof(*addr));
	uv_ip4_name((struct sockaddr_in*)res->ai_addr, addr, 16);
	cinfo->dest = (struct sockaddr_in*)res->ai_addr;
	cinfo->key = malloc(64);
	snprintf(cinfo->key, 64, "%s:%u:%d", addr, cinfo->dest->sin_port, cinfo->dest->sin_family);

	tommy_hashdyn_insert(ac->aggregator, &(cinfo->node), cinfo, tommy_strhash_u32(0, cinfo->key));
}

void do_tcp_client(char *addr, char *port, void *handler, char *mesg, int proto, void *data)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	client_info *cinfo = calloc(1, sizeof(*cinfo));
	cinfo->data = data;

	if (ac->log_level > 2)
		printf("allocated CINFO with addr %p with hostname '%s' with mesg '%s'\n", cinfo, addr, mesg);
	cinfo->mesg = mesg;
	if (mesg)
		cinfo->write = 1;
	else
		cinfo->write = 0;
	cinfo->parser_handler = handler;
	cinfo->hostname = addr;
	cinfo->proto = proto;
	cinfo->port = port;
	cinfo->tt_timer = malloc(sizeof(uv_timer_t));

	uv_getaddrinfo_t *resolver = malloc(sizeof(*resolver));
	resolver->data = cinfo;
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
	client_info *cinfo = calloc(1, sizeof(*cinfo));
	cinfo->data = data;

	if (ac->log_level > 2)
		printf("allocated CINFO with addr %p with hostname '%s' with buf '%p'\n", cinfo, addr, buffer);
	cinfo->buffer = buffer;
	cinfo->buflen = buflen;
	cinfo->write = 2;
	cinfo->parser_handler = handler;
	cinfo->hostname = addr;
	cinfo->proto = proto;
	cinfo->port = port;
	cinfo->tt_timer = malloc(sizeof(uv_timer_t));

	uv_getaddrinfo_t *resolver = malloc(sizeof(*resolver));
	resolver->data = cinfo;
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

void tcp_client_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, timer_cb, ac->aggregator_startup, ac->aggregator_repeat);
}
