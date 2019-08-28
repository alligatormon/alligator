#include <unistd.h>
#include "events/context_arg.h"
#include "evt_tls.h"
#include "uv_tls.h"
#include <uv.h>
#include "main.h"
#include "parsers/multiparser.h"
#include "events/uv_alloc.h"
#include "common/rtime.h"
#include "common/url.h"

void tls_tcp_timeout_timer(uv_timer_t *timer)
{
	extern aconf* ac;
	context_arg *carg = timer->data;
	if (ac->log_level > 1)
		printf("4: timeout carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);
	carg->socket->data = carg;
	uv_close((uv_handle_t*)carg->socket, NULL);
}

void tls_tcp_echo_read(uv_tls_t *strm, ssize_t nrd, const uv_buf_t *bfr)
{
	puts("on_r");
	if ( nrd <= 0 ) return;
	fprintf( stdout, "%s\n", bfr->base);

	uv_tls_close(strm, (uv_tls_close_cb)free);
}

void tls_tcp_on_write(uv_tls_t *utls, int status)
{
	puts("on_wr");
	puts("WR");
	if (status == -1) {
	fprintf(stderr, "error tls_tcp_on_write");
	return;
	}

	uv_tls_read(utls, tls_tcp_echo_read);
}

void on_tls_handshake(uv_tls_t *tls, int status)
{
	puts("on_hs");
	uv_buf_t dcrypted;
	context_arg *carg = tls->data;
	//dcrypted.base = "GET / HTTP/1.1\r\nHost: ya.ru\r\n\r\n";
	dcrypted.base = carg->mesg;
	dcrypted.len = strlen(dcrypted.base);

	if ( 0 == status ) // TLS connection not failed
	{
		uv_tls_write(tls, &dcrypted, tls_tcp_on_write);
	}
	else {
		uv_tls_close(tls, (uv_tls_close_cb)free);
	}
}

void tls_tcp_on_connect(uv_connect_t *req, int status)
{
	// TCP connection error check
	puts("conn");
	if( status ) {
		return;
	}

	context_arg *carg = req->data;
	evt_ctx_t *ctx = carg->ctx;
	uv_tcp_t *tcp = (uv_tcp_t*)req->handle;

	//free on uv_tls_close
	puts("uv_tls_init:");
	uv_tls_t *sclient = malloc(sizeof(*sclient));
	if( uv_tls_init(ctx, tcp, sclient) < 0 ) {
		free(sclient);
		return;
	}
	puts("done:");
	sclient->data = carg;
	puts("uv_tls_connect:");
	uv_tls_connect(sclient, on_tls_handshake);
	puts("done:");
	free(req);
}


void tcp_tls_on_connect_handler(void* arg)
{
	context_arg *carg = arg;
	if (carg->lock)
		return;
	carg->lock = 1;

	uv_loop_t *loop = uv_default_loop();
	//free on uv_close_cb via uv_tls_close call
	uv_tcp_t *client = malloc(sizeof *client);
	uv_tcp_init(loop, client);
 
	evt_ctx_t *ctx = calloc(1, sizeof(*ctx));
	if (carg->tls_certificate)
		evt_ctx_init_ex(ctx, carg->tls_certificate, carg->tls_key);
	evt_ctx_init(ctx);
	evt_ctx_set_nio(ctx, NULL, uv_tls_writer);

	//struct sockaddr_in *conn_addr = calloc(1, sizeof(*conn_addr));
	//uv_ip4_addr("87.250.250.242", 443, conn_addr);
	//uv_ip4_addr(carg->hostname, atoll(carg->port), conn_addr);

	uv_connect_t *req = malloc(sizeof(*req));
	req->data = carg;
	carg->ctx = ctx;
	//uv_tcp_connect(req, client,(struct sockaddr*)conn_addr, tls_tcp_on_connect);
	uv_tcp_connect(req, client, (struct sockaddr *)carg->dest, tls_tcp_on_connect);

	uv_run(loop, UV_RUN_DEFAULT);
	evt_ctx_free(ctx);
	//free(ctx);
}

//void on_connect_handler(void* arg)
//{
//	extern aconf* ac;
//	uv_loop_t *loop = ac->loop;
//	context_arg *carg = arg;
//	if (carg->lock)
//		return;
//	carg->lock = 1;
//
//	if (carg->proto == APROTO_HTTP)
//		carg->http_body_size = 0;
//
//	uv_tcp_t *socket = carg->socket = malloc(sizeof(*socket));
//	uv_tcp_init(loop, socket);
//	uv_tcp_keepalive(socket, 1, 60);
//
//	carg->tt_timer->data = carg;
//	uv_timer_init(loop, carg->tt_timer);
//	uv_timer_start(carg->tt_timer, tls_tcp_timeout_timer, 5000, 0);
//
//	uv_connect_t *connect = carg->connect = malloc(sizeof(*connect));
//	if (ac->log_level > 2)
//		printf("0: on_connect_handler carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, carg->socket, carg->connect, carg->key, carg->hostname);
//	connect->data = carg;
//	uv_tcp_connect(connect, socket, (struct sockaddr *)carg->dest, tls_tcp_on_connect);
//	carg->connect_time = setrtime();
//}


static void tls_timer_cb(uv_timer_t* handle) {
	(void)handle;
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->tls_aggregator, tcp_tls_on_connect_handler);
}

void tls_tcp_on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
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

	char addr[17];
	uv_ip4_name((struct sockaddr_in*)res->ai_addr, addr, 16);
	carg->dest = (struct sockaddr_in*)res->ai_addr;
	carg->key = malloc(64);
	snprintf(carg->key, 64, "%s:%u:%d", addr, carg->dest->sin_port, carg->dest->sin_family);

	tommy_hashdyn_insert(ac->tls_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
}

void do_tls_tcp_client(char *addr, char *port, void *handler, char *mesg, int proto, void *data, char *cert, char *key)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->data = data;

	carg->tls_certificate = cert;
	carg->tls_key = key;

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
	int r = uv_getaddrinfo(loop, resolver, tls_tcp_on_resolved, addr, port, NULL);
	if (r)
	{
		fprintf(stderr, "%s\n", uv_strerror(r));
		return;
	}
	else
	{
		(ac->tls_tcp_client_count)++;
	}
}

void do_tls_tcp_client_buffer(char *addr, char *port, void *handler, uv_buf_t* buffer, size_t buflen, int proto, void *data)
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
	int r = uv_getaddrinfo(loop, resolver, tls_tcp_on_resolved, addr, port, NULL);
	if (r)
	{
		fprintf(stderr, "%s\n", uv_strerror(r));
		return;
	}
	else
	{
		(ac->tls_tcp_client_count)++;
	}
}

void tls_tcp_client_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, tls_timer_cb, ac->tls_aggregator_startup, ac->tls_aggregator_repeat);
}
