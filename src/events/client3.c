#include "events/client3.h"
#include "events/sclient3.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "events/uv_alloc.h"
#include "events/context_arg.h"
#include "main.h"
#include "common/url.h"
#include "common/json_parser.h"
#include "common/selector.h"
#include "parsers/http_proto.h"
int client_start_read(context_arg* carg);


void client_delete(context_arg* carg)
{
	//if (carg->socket)
	//{
	//	//free(carg->socket);
	//	//carg->socket = NULL;
	//}
	//free(carg);
}

void client_closed(uv_handle_t* handle)
{
	puts("client_closed");
	context_arg* carg = (context_arg*)handle->data;
	carg->status = UVHTTP_CLIENT_STATUS_CLOSED;

	if (carg->deleted) {
		client_delete(carg);
	}

	uv_close((uv_handle_t*)carg->tt_timer, NULL);
	//free(carg->socket);
	//free(carg->connect);
	//free(carg->tt_timer);
	string_null(carg->full_body);
	carg->lock = 0;
}

void client_close(context_arg* carg)
{
	printf("client_close: %p\n", client_closed);
	if (uv_is_closing((uv_handle_t*)carg->socket) == 0) {
		carg->status = UVHTTP_CLIENT_STATUS_CLOSING;
		if (carg->tls)
			uvhttp_ssl_client_close((uv_handle_t*)carg->socket, client_closed);
		else
			uv_close((uv_handle_t*)carg->socket, client_closed);
	}
}

void uvhttp_client_delete(uvhttp_client client)
{
	context_arg* carg = (context_arg*)client;

	if (carg->deleted )
		return;
	else if (carg->status == UVHTTP_CLIENT_STATUS_CLOSED || carg->status == UVHTTP_CLIENT_STATUS_INITED)
			client_delete(carg);
	else {
		carg->deleted = 1;
		uvhttp_client_abort(carg);
	}
}

int uvhttp_client_abort(uvhttp_client client)
{
	context_arg* carg = (context_arg*)client;
	client_close(carg);
	return UVHTTP_OK;
}


void tcp_timeout_timer(uv_timer_t *timer)
{
	extern aconf* ac;
	context_arg *carg = timer->data;
	if (ac->log_level > 1)
		printf("4: timeout carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);
	carg->socket->data = carg;
	uv_close((uv_handle_t*)carg->socket, client_closed);
}

//static int uvhttp_write(uv_write_t* req, uv_stream_t* handle, char* buffer, unsigned int buffer_len, uv_write_cb cb)
//{
//	uv_buf_t buf;
//	buf.base = buffer;
//	buf.len = buffer_len;
//	printf("buffer(%zu) '%s'\n", buffer_len, buffer);
//	return uv_write(req, handle, &buf, 1, cb);
//}

static void request_written( uv_write_t* req, int status)
{
	//if (status < 0)
		free(req);
}

static int write_client_request( context_arg* carg)
{
	int ret = 0;
	uv_write_t* write_req = 0;
	carg->status = UVHTTP_CLIENT_STATUS_REQUESTING;

	write_req = (uv_write_t*)malloc( sizeof(uv_write_t));
	write_req->data = carg;
	//printf("query: '%s'\n", carg->mesg);

	if( carg->tls )
		ret = uvhttp_ssl_client_write( write_req, (uv_stream_t*)carg->socket, carg->mesg, carg->mesg_len, request_written);
	else
	{
		//ret = uvhttp_write( write_req, (uv_stream_t*)carg->socket, carg->mesg, carg->mesg_len, request_written);
		ret = uv_write(write_req, (uv_stream_t*)carg->socket, carg->buffer, carg->buflen, request_written);
	}

	if ((ret != UVHTTP_OK) && (ret != 0) && write_req)
		free( write_req);
	return ret;
}

static void client_shutdown(uv_shutdown_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;
	client_close( carg);
	free( req);
}

void chunk_calc(context_arg* carg, char *buf, size_t nread)
{
	puts("CHUNK CALC");
	if (carg->chunked_size > nread)
	{
		printf("MATCH: 1: %lld > %lld\n", carg->chunked_size, nread);
		string_cat(carg->full_body, buf, nread);
		carg->chunked_size -= nread;

			//char del[100];
			//snprintf(del, 100, "/%c.%lld", *buf, nread);
			//FILE *fd = fopen(del, "w");
			//printf("1 SAVED: %s\n", del);
			//fwrite(buf, nread, 1, fd);
			//fclose(fd);
	}
	else
	{
		char *tmp;
		uint64_t n = 0;
		uint64_t m = 0;
		while (carg->chunked_size && n<nread)
		{
			m = strcspn(buf+n, "\r\n");
			printf("MATCH2 buf+%lld/%lld\n: %s", n, nread, buf+n);
			string_cat(carg->full_body, buf+n, m);

			//char del[100];
			//snprintf(del, 100, "/%c.%lld", *buf+n, nread);
			//FILE *fd = fopen(del, "w");
			//printf("2 SAVED: %s\n", del);
			//fwrite(buf+n, m, 1, fd);
			//fclose(fd);

			n += m;

			carg->chunked_size = strtoll(buf+n, &tmp, 16);
			printf("chunked_size %lld\n", carg->chunked_size);
			n = tmp - buf;
			n += strcspn(buf+n, "\r\n");
			n++;
		}
	}
}

//void chunk_calc(context_arg* carg, char *buf, size_t nread)
//{
//	if (carg->chunked_size > nread)
//	{
//		printf("MATCH: 1: %lld > %lld\n", carg->chunked_size, nread);
//		string_cat(carg->full_body, buf, nread);
//		carg->chunked_size -= nread;
//	}
//	else if (carg->chunked_size < nread)
//	{
//		printf("MATCH: 2: %lld > %lld\n", carg->chunked_size, nread);
//		uint64_t copy_size = carg->chunked_size;
//		string_cat(carg->full_body, buf, copy_size);
//
//		buf += copy_size;
//		copy_size = nread - carg->chunked_size - 2;
//		string_cat(carg->full_body, buf + 2, copy_size);
//		carg->chunked_size = strtoll(buf + carg->chunked_size, &buf, 16);
//		carg->chunked_size -= strlen(buf);
//	}
//	else
//		printf("MATCH: 3: %lld > %lld\n", carg->chunked_size, nread);
//	puts(buf);
//}

int8_t tcp_check_full(context_arg* carg, char *buf, size_t nread)
{
	//printf("check buf for '%s'\n", buf);
	// check body size
	puts("check for content size");
	printf("CHECK CHUNK HTTP VALIDATOR: %d && !%d", carg->chunked_expect, carg->chunked_size);
	if (carg->expect_body_length && carg->expect_body_length <= carg->full_body->l)
	{
		puts("content size match 1");
		return 1;
	}

	// check chunk http validator
	else if (carg->chunked_expect && !carg->chunked_size)
	{
		puts("chunked size 1");
		return 1;
	}

	// check expect function validator
	else if (carg->expect_function)
		return carg->expect_function(buf);

	else if (carg->expect_count <= carg->read_count)
	{
		puts("expect count 1");
		return 1;
	}

	return 0;

	//uint64_t full_body_size;
        //int8_t expect_json;
        //uint64_t expect_body_length;
        //int8_t expect_chunk;
}

static void client_data_read( uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	//printf("client_data_read: %lld, %zu\n", nread, buf->len);
	context_arg* carg = (context_arg*)stream->data;

	if (nread > 0)
	{
		printf("length %d\n", carg->full_body->l);
		if (!carg->full_body->l)
		{
			string_cat(carg->full_body, buf->base, nread);

			http_reply_data* hr_data = http_reply_parser(carg->full_body->s, carg->full_body->l);
			carg->http_body = hr_data->body;
			carg->chunked_size = hr_data->chunked_size;
			carg->chunked_expect = hr_data->chunked_expect;
			free(hr_data);

			printf("fullbody1: '%s'\n", carg->http_body);

			//char del[100];
			//snprintf(del, 100, "/%c.%lld", *buf->base, nread);
			//FILE *fd = fopen(del, "w");
			//printf("0 SAVED: %s\n", del);
			////fwrite(buf->base, nread, 1, fd);
			//fwrite(carg->http_body, strlen(carg->http_body), 1, fd);
			//fclose(fd);
		}
		else if (carg->chunked_size) // maybe chunked_expect?
		{
			printf("chunkedsize: %d\n", carg->chunked_expect);
			printf("chunkedsize1: %s\n", buf->base);
			chunk_calc(carg, buf->base, nread);
		}
		else
			string_cat(carg->full_body, buf->base, nread);

		int8_t rc = tcp_check_full(carg, carg->http_body, nread);

		printf("RC: %d\n", rc);
		//printf("bufbase parser (%zu): %s\n", nread, buf->base);
		if (rc)
		{
			printf("bufbase parser (%zu):\n==============================\n%s\n=========================\n", nread, carg->full_body->s);
			alligator_multiparser(carg->full_body->s, nread, carg->parser_handler, NULL, carg);
		}
		else
			client_start_read(carg);

		//printf("freeing %p\n", buf->base);
		//free(buf->base);
	}
	else if ( nread == 0) {
		return;
	}
	else if( nread == UV_EOF) {
		uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
		req->data = carg;
		uv_shutdown(req, (uv_stream_t*)carg->socket, client_shutdown);
	}
	//else if (nread == UV_ECONNRESET || nread == UV_ECONNABORTED) {
		//client_close(carg);
	//}
	//free(buf->base);
}

int client_start_read(context_arg* carg)
{
	puts("client_start_read");
	int ret = 0;
	printf("ssl %d\n", carg->tls);
	if ( carg->tls )
	{
		printf("SSL: %p\n", carg->socket);
		ret = uvhttp_ssl_read_client_start((uv_stream_t*)carg->socket, alloc_buffer, client_data_read);
		if ( ret != 0)
			return ret;
	}
	else
	{
		//printf("SSL2: %p\n", carg->socket);
		//ret = uvhttp_ssl_read_client_start((uv_stream_t*)carg->socket, alloc_buffer, client_data_read);
		puts("what??");
		ret = uv_read_start((uv_stream_t*)carg->socket, alloc_buffer, client_data_read);
		if ( ret == UV_EALREADY)
			ret = 0;
	}
	return ret;
}

static void client_connected(uv_connect_t* req, int status)
{
	int ret = 0;
	context_arg* carg = (context_arg*)req->data;
	if ( status < 0 ) {
		free(req);
		puts("free done");
		//exit(1);
		return;
	}
	ret = client_start_read(carg);
	if ( ret != 0) {
		free( req);
		return;
	}
	ret = write_client_request( carg );
	if ( ret != 0 ) {
		free( req);
		return;
	}
	free( req);
}

static void client_getaddrinfo(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
	int ret = 0;
	uv_connect_t* connect_req = 0;
	context_arg* carg = (context_arg*)req->data;

	char addr[17] = {'\0'};
	if (status < 0 ) {
		ret = -1;
		goto cleanup;
	}
	ret = uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);
	if ( ret != 0)
		goto cleanup;
	connect_req = (uv_connect_t*)malloc(sizeof(uv_connect_t));
	connect_req->data = carg;
	printf("1 connect_req %p\n", connect_req);
	if ( carg->tls) {
		uvhttp_ssl_client_connect( connect_req, carg->socket, res->ai_addr, client_connected);
	}
	else {
		ret = uv_tcp_connect( connect_req, carg->socket, res->ai_addr, client_connected);
		if ( ret != 0)
			goto cleanup;
	}
cleanup:
	if ( ret != 0) {
		if ( connect_req)
			free( connect_req);
	}
	if ( res) 
		uv_freeaddrinfo(res);
	if ( req)
		free( req);
}

static int client_getaddr( context_arg* carg)
{
	extern aconf* ac;
	struct addrinfo hints;
	uv_getaddrinfo_t* addr_info = 0;
	int ret = 0;
	addr_info = (uv_getaddrinfo_t*)malloc(sizeof(uv_getaddrinfo_t));
	addr_info->data = carg;

	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = 0;

	ret = uv_getaddrinfo( ac->loop, addr_info, client_getaddrinfo, carg->hostname, carg->port, &hints);

	if ( ret != 0)
		free( addr_info);

	return ret;
}

static int client_new_conn_request( context_arg* carg)
{
	puts("norm start");
	extern aconf* ac;
	int ret = 0;
	if ( carg->tls) {
		carg->socket = (uv_tcp_t*)malloc( sizeof(struct uvhttp_ssl_client) );
		memset( carg->socket, 0, sizeof(struct uvhttp_ssl_client)); 
		carg->socket->data = carg;

		if ( (ret = uv_tcp_init( ac->loop, carg->socket)) != 0)
			return ret;
		printf("DEBUG carg->socket init %p\n", carg->socket);

		ret = uvhttp_ssl_client_init(carg);
		if ( ret != 0)
			return ret;
	}
	else
	{
		carg->socket = (uv_tcp_t*)malloc( sizeof(uv_tcp_t) );
		memset( carg->socket, 0, sizeof(uv_tcp_t));
		carg->socket->data = carg;
		carg->status = UVHTTP_CLIENT_STATUS_REQUESTING;
		printf("DEBUG carg->socket init %p\n", carg->socket);
		if ( (ret = uv_tcp_init( ac->loop, carg->socket)) != 0)
			return ret;
	}
	if ( (ret = client_getaddr( carg)) != 0)
		return ret;

	return ret;
}

void uvhttp_client_request()
{
	extern aconf* ac;
	context_arg* carg = calloc(1, sizeof(*carg));
	carg->status = UVHTTP_CLIENT_STATUS_INITED;
	carg->tls = 1;
	carg->mesg = strdup("GET /test08 HTTP/1.1\r\nHost: 127.0.0.1:443\r\nUser-Agent: UVHttp\r\n\r\n");
	carg->mesg_len = strlen(carg->mesg);

	carg->hostname = strdup("127.0.0.1");
	carg->port = strdup("443");

	client_new_conn_request(carg);
}





void tcp_on_connect()
{
};

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

	//uv_tcp_t *socket = carg->socket = malloc(sizeof(*socket));
	//uv_tcp_t *socket = (uv_tcp_t*)malloc( sizeof(struct uvhttp_ssl_client));
	uv_tcp_init(loop, carg->socket);
	uv_tcp_keepalive(carg->socket, 1, 60);

	carg->tt_timer->data = carg;
	uv_timer_init(loop, carg->tt_timer);
	uv_timer_start(carg->tt_timer, tcp_timeout_timer, 5000, 0);

	uv_connect_t *connect = carg->connect = malloc(sizeof(*connect));
	//if (ac->log_level > 2)
	//	printf("0: on_connect_handler carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);
	//connect->data = carg;
	//uv_tcp_connect(connect, socket, (struct sockaddr *)carg->dest, tcp_on_connect);
	//carg->connect_time = setrtime();
	carg->status = UVHTTP_CLIENT_STATUS_REQUESTING;
	//carg->socket = socket;
	carg->socket->data = carg;
	carg->connect->data = carg;

	//uvhttp_ssl_client_init(ac->loop, carg->socket);
	//puts("uvhttp_ssl_client_init end");

	if ( carg->tls) {
		uvhttp_ssl_client_init(carg);
		uvhttp_ssl_client_connect(carg->connect, carg->socket, (struct sockaddr *)carg->dest, client_connected);
	}
	else {
		uv_tcp_connect(carg->connect, carg->socket, (struct sockaddr *)carg->dest, client_connected);
	}
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

void do_tcp_client(char *addr, char *port, void *handler, char *mesg, int proto, void *data, int tls)
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
	carg->status = UVHTTP_CLIENT_STATUS_INITED;
	carg->hostname = addr;
	carg->proto = proto;
	carg->port = port;
	if (tls)
		carg->tls = 1;
	carg->mesg_len = strlen(mesg);
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

void do_tcp_client_buffer(char *addr, char *port, void *handler, uv_buf_t* buffer, size_t buflen, int proto, void *data, int tls)
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
	carg->status = UVHTTP_CLIENT_STATUS_INITED;
	carg->proto = proto;
	carg->port = port;
	carg->tls = tls;
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
	printf("ssl %d\n", carg->tls);
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_getaddrinfo_t *resolver = malloc(sizeof(*resolver));
	carg->uvbuf = malloc(65536);
	carg->socket = malloc(sizeof(*carg->socket));
	uv_tcp_init(loop, carg->socket);
	carg->socket->data = carg;
	resolver->data = carg;
	carg->status = UVHTTP_CLIENT_STATUS_INITED;

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
