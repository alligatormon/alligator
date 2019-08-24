#include <unistd.h>
#include "events/client_info.h"
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
	client_info *cinfo = timer->data;
	if (ac->log_level > 1)
		printf("4: timeout cinfo with addr %p(%p:%p) with key %s, hostname %s\n", cinfo, cinfo->socket, cinfo->connect, cinfo->key, cinfo->hostname);
	cinfo->socket->data = cinfo;
	uv_close((uv_handle_t*)cinfo->socket, NULL);
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
	client_info *cinfo = tls->data;
	//dcrypted.base = "GET / HTTP/1.1\r\nHost: ya.ru\r\n\r\n";
	dcrypted.base = cinfo->mesg;
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

	client_info *cinfo = req->data;
	evt_ctx_t *ctx = cinfo->ctx;
	uv_tcp_t *tcp = (uv_tcp_t*)req->handle;

	//free on uv_tls_close
	puts("uv_tls_init:");
	uv_tls_t *sclient = malloc(sizeof(*sclient));
	if( uv_tls_init(ctx, tcp, sclient) < 0 ) {
		free(sclient);
		return;
	}
	puts("done:");
	sclient->data = cinfo;
	puts("uv_tls_connect:");
	uv_tls_connect(sclient, on_tls_handshake);
	puts("done:");
	free(req);
}


void tcp_tls_on_connect_handler(void* arg)
{
	client_info *cinfo = arg;
	if (cinfo->lock)
		return;
	cinfo->lock = 1;

	uv_loop_t *loop = uv_default_loop();
	//free on uv_close_cb via uv_tls_close call
	uv_tcp_t *client = malloc(sizeof *client);
	uv_tcp_init(loop, client);
 
	evt_ctx_t *ctx = calloc(1, sizeof(*ctx));
	if (cinfo->tls_certificate)
		evt_ctx_init_ex(ctx, cinfo->tls_certificate, cinfo->tls_key);
	evt_ctx_init(ctx);
	evt_ctx_set_nio(ctx, NULL, uv_tls_writer);

	//struct sockaddr_in *conn_addr = calloc(1, sizeof(*conn_addr));
	//uv_ip4_addr("87.250.250.242", 443, conn_addr);
	//uv_ip4_addr(cinfo->hostname, atoll(cinfo->port), conn_addr);

	uv_connect_t *req = malloc(sizeof(*req));
	req->data = cinfo;
	cinfo->ctx = ctx;
	//uv_tcp_connect(req, client,(struct sockaddr*)conn_addr, tls_tcp_on_connect);
	uv_tcp_connect(req, client, (struct sockaddr *)cinfo->dest, tls_tcp_on_connect);

	uv_run(loop, UV_RUN_DEFAULT);
	evt_ctx_free(ctx);
	//free(ctx);
}

//void on_connect_handler(void* arg)
//{
//	extern aconf* ac;
//	uv_loop_t *loop = ac->loop;
//	client_info *cinfo = arg;
//	if (cinfo->lock)
//		return;
//	cinfo->lock = 1;
//
//	if (cinfo->proto == APROTO_HTTP)
//		cinfo->http_body_size = 0;
//
//	uv_tcp_t *socket = cinfo->socket = malloc(sizeof(*socket));
//	uv_tcp_init(loop, socket);
//	uv_tcp_keepalive(socket, 1, 60);
//
//	cinfo->tt_timer->data = cinfo;
//	uv_timer_init(loop, cinfo->tt_timer);
//	uv_timer_start(cinfo->tt_timer, tls_tcp_timeout_timer, 5000, 0);
//
//	uv_connect_t *connect = cinfo->connect = malloc(sizeof(*connect));
//	if (ac->log_level > 2)
//		printf("0: on_connect_handler cinfo with addr %p(%p:%p) with key %s, hostname %s\n", cinfo, cinfo->socket, cinfo->connect, cinfo->key, cinfo->hostname);
//	connect->data = cinfo;
//	uv_tcp_connect(connect, socket, (struct sockaddr *)cinfo->dest, tls_tcp_on_connect);
//	cinfo->connect_time = setrtime();
//}


static void tls_timer_cb(uv_timer_t* handle) {
	(void)handle;
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->tls_aggregator, tcp_tls_on_connect_handler);
}

void tls_tcp_on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
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

	char addr[17];
	uv_ip4_name((struct sockaddr_in*)res->ai_addr, addr, 16);
	cinfo->dest = (struct sockaddr_in*)res->ai_addr;
	cinfo->key = malloc(64);
	snprintf(cinfo->key, 64, "%s:%u:%d", addr, cinfo->dest->sin_port, cinfo->dest->sin_family);

	tommy_hashdyn_insert(ac->tls_aggregator, &(cinfo->node), cinfo, tommy_strhash_u32(0, cinfo->key));
}

void do_tls_tcp_client(char *addr, char *port, void *handler, char *mesg, int proto, void *data, char *cert, char *key)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	client_info *cinfo = calloc(1, sizeof(*cinfo));
	cinfo->data = data;

	cinfo->tls_certificate = cert;
	cinfo->tls_key = key;

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
