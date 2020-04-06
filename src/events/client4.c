#include "context_arg.h"
#include "mbedtls/certs.h"
#include "mbedtls/debug.h"
#include "events/uv_alloc.h"
#include "events/debug.h"
#include "main.h"
#include "parsers/http_proto.h"
#define URL "news.rambler.ru"
#define PORT "443"
#define MESG "GET / HTTP/1.1\r\nHost: news.rambler.ru\r\nConnection: Close\r\n\r\n"
#define MESG2 "UBCT1 stats_noreset\n"
#define MESG3 "stats\n"
#define MESG4 "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"

void tcp_connected(uv_connect_t* req, int status);

void tcp_client_closed(uv_handle_t *handle)
{
	puts("closed");
	context_arg* carg = handle->data;
	if (carg->tls)
	{
		mbedtls_x509_crt_free(&carg->tls_cacert);
		mbedtls_x509_crt_free(&carg->tls_cert);
		mbedtls_ssl_free(&carg->tls_ctx);
		mbedtls_ssl_config_free(&carg->tls_conf);
		mbedtls_ctr_drbg_free(&carg->tls_ctr_drbg);
		mbedtls_entropy_free(&carg->tls_entropy);
		mbedtls_ssl_free(&carg->tls_ctx);
	}
	carg->lock = 0;
	string_null(carg->full_body);
}

void tcp_client_close(uv_handle_t *handle)
{
	puts("close");
	context_arg* carg = handle->data;

	const mbedtls_x509_crt* peercert = mbedtls_ssl_get_peer_cert(&carg->tls_ctx);
	parse_cert_info(peercert, carg->host);
	//char dn_subject[1000];
	//int dn_subject_size = mbedtls_x509_dn_gets(dn_subject, 1000, &peercert->subject);
	//printf("%s (%d)\n", dn_subject, dn_subject_size);

	if (!uv_is_closing((uv_handle_t*)&carg->client))
	{
		puts("not is closing");
		if (carg->tls)
		{
			if (!carg->is_writing)
			{
				int ret = mbedtls_ssl_close_notify(&carg->tls_ctx);
				if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
					uv_close((uv_handle_t*)&carg->client, tcp_client_closed);
			}
			else
				uv_close((uv_handle_t*)&carg->client, tcp_client_closed);
		}
		else
		{
			uv_close((uv_handle_t*)&carg->client, tcp_client_closed);
		}
	}
}

void tcp_client_shutdown(uv_shutdown_t* req, int status)
{
	puts("shutdown");
	context_arg* carg = req->data;
	tcp_client_close((uv_handle_t *)&carg->client);
	free(req);
}

void tcp_client_readed(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)stream->data;
	//printf("tcp_client_readed: nread %lld, EOF: %d, UV_ECONNRESET: %d, UV_ECONNABORTED: %d, UV_ENOBUFS: %d\n", nread, UV_EOF, UV_ECONNRESET, UV_ECONNABORTED, UV_ENOBUFS);

	if (nread > 0)
	{
		if (buf && buf->base)
		{
			char *tmp = strndup(buf->base, nread);

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
			if (rc)
				alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

			free(tmp);
		}
	}

	else if (nread == 0)
		return;

	else if(nread == UV_EOF)
	{
		uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
		req->data = carg;
		uv_shutdown(req, (uv_stream_t*)&carg->client, tcp_client_shutdown);
		tcp_client_close((uv_handle_t *)&carg->client);
	}
	else if (nread == UV_ECONNRESET || nread == UV_ECONNABORTED)
		tcp_client_close((uv_handle_t *)&carg->client);
	else if (nread == UV_ENOBUFS)
		tcp_client_close((uv_handle_t *)&carg->client);
}

void tls_client_readed(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	//printf("========= tls_client_readed: nread %lld, EOF: %d, UV_ECONNRESET: %d, UV_ECONNABORTED: %d, UV_ENOBUFS: %d\n", nread, UV_EOF, UV_ECONNRESET, UV_ECONNABORTED, UV_ENOBUFS);
	context_arg* carg = stream->data;
	if (nread <= 0)
	{
		tcp_client_readed(stream, nread, 0);
		return;
	}

	if (carg->tls_ctx.state != MBEDTLS_SSL_HANDSHAKE_OVER)
	{
		int ret = 0;
		carg->ssl_read_buffer_len = nread;
		carg->ssl_read_buffer_offset = 0;
		while ((ret = mbedtls_ssl_handshake_step(&carg->tls_ctx)) == 0)
		{
			if (carg->tls_ctx.state == MBEDTLS_SSL_HANDSHAKE_OVER)
			{
				tcp_connected(&carg->connect, 0);
				break;
			}
		}
		if (carg->ssl_read_buffer_offset != nread)
			tcp_connected(&carg->connect,  -1);
		carg->ssl_read_buffer_len = 0;
		carg->ssl_read_buffer_offset = 0;
		if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
			tcp_connected(&carg->connect,  -1);
	}
	else
	{
		int read_len = 0;
		carg->ssl_read_buffer_len = nread;
		carg->ssl_read_buffer_offset = 0;
 
		while((read_len = mbedtls_ssl_read(&carg->tls_ctx, (unsigned char *)carg->user_read_buf.base,  carg->user_read_buf.len)) > 0)
			tcp_client_readed(stream, read_len, &carg->user_read_buf);

		if (read_len !=0 && read_len != MBEDTLS_ERR_SSL_WANT_READ)
		{
			if (MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY == read_len)
				tcp_client_readed(stream, nread, 0);
			else
				tcp_client_readed(stream, nread, 0);
		}
	}
}

void tls_client_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)handle->data;
	carg->user_read_buf.base = 0;
	carg->user_read_buf.len = 0;
	tcp_alloc(handle, suggested_size, &carg->user_read_buf);
	buf->base = carg->ssl_read_buffer;
	buf->len = sizeof(carg->ssl_read_buffer);
}

int tls_client_mbed_recv(void *ctx, unsigned char *buf, size_t len)
{
	context_arg* carg = (context_arg*)ctx;
	int need_copy = (carg->ssl_read_buffer_len - carg->ssl_read_buffer_offset) > len ? len : (carg->ssl_read_buffer_len - carg->ssl_read_buffer_offset);
	if (need_copy == 0)
		return MBEDTLS_ERR_SSL_WANT_READ;

	memcpy(buf, carg->ssl_read_buffer + carg->ssl_read_buffer_offset, need_copy);
	carg->ssl_read_buffer_offset += need_copy;

	return need_copy;
}

void tls_client_writed(uv_write_t* req, int status)
{
	int ret = 0;
	context_arg* carg = (context_arg*)req->data;
	if(status != 0) {
		return;
	}
	if (carg->is_write_error) {
		return;
	}
	if (carg->write_buffer.base) {
		free(carg->write_buffer.base);
		carg->write_buffer.base = 0;
	}
	if (carg->tls_ctx.state != MBEDTLS_SSL_HANDSHAKE_OVER)
	{
		while ((ret = mbedtls_ssl_handshake_step(&carg->tls_ctx)) == 0)
		{
			if (carg->tls_ctx.state == MBEDTLS_SSL_HANDSHAKE_OVER)
				break;
		}
		if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ)
		{
			tcp_connected(&carg->connect,  -1);
		}
		ret = 0;
	}
	else
	{
		ret = mbedtls_ssl_write(&carg->tls_ctx, (const unsigned char *)carg->ssl_write_buffer + carg->ssl_write_offset, carg->ssl_write_buffer_len - carg->ssl_write_offset);
		if (ret > 0)
		{
			carg->ssl_write_offset += ret;
			ret = 0;
		}
	}
	if (carg->write_buffer.base)
	{
		free(carg->write_buffer.base);
		carg->write_buffer.base = 0;
	}
	if (req)
		free(req);
}

int tls_client_mbed_send(void *ctx, const unsigned char *buf, size_t len)
{
	//puts("tls_client_mbed_send");
	context_arg* carg = (context_arg*)ctx;
	uv_write_t* write_req = 0;
	int ret = 0;
	carg->write_buffer.base = 0;
	carg->write_buffer.len = 0;
	if (carg->is_write_error)
		return -1;
	if (carg->is_async_writing == 0)
	{
		carg->write_buffer.base = (char*)malloc(len);
		memcpy(carg->write_buffer.base, buf, len);
		carg->write_buffer.len = len;
		write_req = (uv_write_t*)malloc(sizeof(uv_write_t));
		write_req->data = carg;
		ret = uv_write(write_req, (uv_stream_t*)&carg->client, &carg->write_buffer, 1, tls_client_writed);
		if (ret != 0)
		{
			len = -1;
			carg->is_write_error = 1;
			if (carg->tls_ctx.state != MBEDTLS_SSL_HANDSHAKE_OVER)
				tcp_connected(&carg->connect,  -1);
			if (write_req)
				free(write_req);
			if (carg->write_buffer.base)
			{
				free(carg->write_buffer.base);
				carg->write_buffer.base = 0;
			}
		}
		len = MBEDTLS_ERR_SSL_WANT_WRITE;
		carg->is_async_writing = 1;
	}
	else
		carg->is_async_writing = 0;

	return len;
}

int tls_client_init(uv_loop_t* loop, context_arg *carg)
{
	int ret = 0;

	mbedtls_ssl_init(&carg->tls_ctx);
	mbedtls_ssl_config_init(&carg->tls_conf);
	mbedtls_x509_crt_init(&carg->tls_cacert);
	mbedtls_ctr_drbg_init(&carg->tls_ctr_drbg);
	mbedtls_entropy_init(&carg->tls_entropy);
	mbedtls_pk_init(&carg->tls_key);
 
	if((ret = mbedtls_ctr_drbg_seed(&carg->tls_ctr_drbg, mbedtls_entropy_func, &carg->tls_entropy, (const unsigned char *) "UVHTTP", sizeof("UVHTTP") -1)) != 0)
		return ret;
 
	if (!carg->tls_ca_file)
	{
		if(mbedtls_x509_crt_parse(&carg->tls_cacert, (const unsigned char *) mbedtls_test_cas_pem, mbedtls_test_cas_pem_len))
			return ret;
	}
	else
	{
		mbedtls_x509_crt_parse_file(&carg->tls_cacert, carg->tls_ca_file);
		mbedtls_x509_crt_parse_file(&carg->tls_cert, carg->tls_cert_file);
		mbedtls_pk_parse_keyfile(&carg->tls_key, carg->tls_key_file, NULL);
		mbedtls_ssl_conf_own_cert(&carg->tls_conf, &carg->tls_cert, &carg->tls_key);
	}

	mbedtls_ssl_conf_ca_chain(&carg->tls_conf, &carg->tls_cacert, NULL);
 
	if((ret = mbedtls_ssl_config_defaults(&carg->tls_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
		return ret;

	mbedtls_ssl_conf_authmode(&carg->tls_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
	mbedtls_ssl_conf_rng(&carg->tls_conf, mbedtls_ctr_drbg_random, &carg->tls_ctr_drbg);
 
	if((ret = mbedtls_ssl_setup(&carg->tls_ctx, &carg->tls_conf)) != 0)
	        return ret;
	if((ret = mbedtls_ssl_set_hostname(&carg->tls_ctx, carg->host)) != 0)
	        return ret;

	mbedtls_ssl_set_bio(&carg->tls_ctx, carg, tls_client_mbed_send, tls_client_mbed_recv, NULL);

	mbedtls_ssl_conf_dbg(&carg->tls_conf, tls_debug, stdout);
	mbedtls_debug_set_threshold(0);


	return ret;
}

void tls_connected(uv_connect_t* req, int status)
{
	extern aconf* ac;
	int ret = 0;
	context_arg* carg = (context_arg*)req->data;
	if (ac->log_level > 1)
		printf("1: tls client connected %p(%p:%p) with key %s, hostname %s\n", carg, &carg->connect, &carg->client, carg->key, carg->host);

	if (status < 0)
	{
		tcp_connected(&carg->connect,  -1);
		return;
	}
	ret = uv_read_start((uv_stream_t*)&carg->client, tls_client_alloc, tls_client_readed);
	if (ret != 0)
	{
		tcp_connected(&carg->connect,  -1);
		return;
	}

	while ((ret = mbedtls_ssl_handshake_step(&carg->tls_ctx)) == 0)
		if (carg->tls_ctx.state == MBEDTLS_SSL_HANDSHAKE_OVER)
			break;

	if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ)
		tcp_connected(&carg->connect,  1);
}

void tcp_connected(uv_connect_t* req, int status)
{
	extern aconf* ac;
	context_arg* carg = (context_arg*)req->data;
	if (ac->log_level > 1)
		printf("1: tcp client connected %p(%p:%p) with key %s, hostname %s\n", carg, &carg->client, &carg->connect, carg->key, carg->host);

	if (status < 0)
		return;

	if (!carg->tls)
		uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, tcp_client_readed);

	memset(&carg->write_req, 0, sizeof(carg->write_req));

	carg->write_req.data = carg;

	puts("2");
	if(carg->tls)
	{
		carg->is_write_error = 0;
		carg->ssl_write_buffer_len = carg->request_buffer.len;
		carg->ssl_write_buffer = carg->request_buffer.base;
		carg->ssl_read_buffer_len = 0;
		carg->ssl_read_buffer_offset = 0;
		carg->ssl_write_offset = 0;
		carg->is_async_writing = 0;
		if (mbedtls_ssl_write(&carg->tls_ctx, (const unsigned char *)carg->request_buffer.base, carg->request_buffer.len) == MBEDTLS_ERR_SSL_WANT_WRITE)
			carg->is_writing = 1;
	}
	else
		uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->request_buffer, 1, NULL);
}

void tcp_timeout_timer(uv_timer_t *timer)
{
	extern aconf* ac;
	context_arg *carg = timer->data;
	if (ac->log_level > 1)
		printf("4: timeout carg with addr %p(%p:%p) with key %s, hostname %s\n", carg, &carg->client, &carg->connect, carg->key, carg->host);
	tcp_client_close((uv_handle_t *)&carg->client);
}

void tcp_client_connect(void *arg)
{
	extern aconf* ac;

	context_arg *carg = arg;
	if (ac->log_level > 1)
		printf("0: tcp client connect %p(%p:%p) with key %s, hostname %s, lock: %d\n", carg, &carg->client, &carg->connect, carg->key, carg->host, carg->lock);

	if (carg->lock)
		return;
	carg->lock = 1;

	carg->tt_timer->data = carg;
	uv_timer_init(carg->loop, carg->tt_timer);
	uv_timer_start(carg->tt_timer, tcp_timeout_timer, 5000, 0);
	memset(&carg->connect, 0, sizeof(carg->connect));
	carg->connect.data = carg;

	memset(&carg->client, 0, sizeof(carg->client));
	carg->client.data = carg;
	uv_tcp_init(carg->loop, &carg->client);
	if (carg->tls)
		tls_client_init(carg->loop, carg);

	if (carg->tls)
		uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)carg->dest, tls_connected);
	else
		uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)carg->dest, tcp_connected);
}

static void tcp_client_crawl(uv_timer_t* handle) {
	(void)handle;
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->aggregator, tcp_client_connect);
}

void aggregator_getaddrinfo(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
	extern aconf *ac;
	context_arg* carg = (context_arg*)req->data;

	char addr[17] = {'\0'};
	if (status < 0)
	{
		uv_freeaddrinfo(res);
		free(req);
		return;
	}

	uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);

	carg->dest = (struct sockaddr_in*)res->ai_addr;
	carg->key = malloc(64);
	snprintf(carg->key, 64, "%s:%u:%d", addr, carg->dest->sin_port, carg->dest->sin_family);

	tommy_hashdyn_insert(ac->aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));

	uv_freeaddrinfo(res);
	free(req);
}

void aggregator_resolve_host(context_arg* carg)
{
	struct addrinfo hints;
	uv_getaddrinfo_t* addr_info = 0;
	addr_info = malloc(sizeof(uv_getaddrinfo_t));
	addr_info->data = carg;

	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = 0;

	if (uv_getaddrinfo(carg->loop, addr_info, aggregator_getaddrinfo, carg->host, carg->port, &hints))
		free(addr_info);

}

void tcp_client(void *arg)
{
	if (!arg)
		return;

	context_arg *carg = arg;

	aggregator_resolve_host(carg);
}

void unix_tcp_client(context_arg* carg)
{
	if (!carg)
		return;

	//extern aconf* ac;
	//uv_loop_t *loop = ac->loop;
	//context_arg *carg = arg;
	//uv_pipe_t *handle = malloc(sizeof(uv_pipe_t));
	//carg->socket = (uv_tcp_t*)handle;
	//uv_connect_t *connect = carg->connect = (uv_connect_t*)malloc(sizeof(uv_connect_t));

	uv_pipe_init(carg->loop, (uv_pipe_t*)&carg->client, 0); 
	carg->connect.data = carg;
	carg->client.data = carg;
	if (carg->tls)
		tls_client_init(carg->loop, carg);

	if (carg->tls)
		uv_pipe_connect(&carg->connect, (uv_pipe_t *)&carg->client, carg->host, tls_connected);
	else
		uv_pipe_connect(&carg->connect, (uv_pipe_t *)&carg->client, carg->host, tcp_connected);
}

void fill_unixunbound(context_arg *carg)
{
	carg->tls = 1;
	carg->request_buffer = uv_buf_init(MESG2, sizeof(MESG2));
	carg->tls_ca_file = strdup("/etc/unbound/unbound_server.pem");
	carg->tls_cert_file = strdup("/etc/unbound/unbound_control.pem");
	carg->tls_key_file = strdup("/etc/unbound/unbound_control.key");
	memcpy(carg->host, "/var/run/unbound.sock",  strlen("/var/run/unbound.sock"));

	unix_tcp_client(carg);
}

void fill_tcpunbound(context_arg *carg)
{
	carg->tls = 1;
	carg->request_buffer = uv_buf_init(MESG2, sizeof(MESG2));
	carg->tls_ca_file = strdup("/etc/unbound/unbound_server.pem");
	carg->tls_cert_file = strdup("/etc/unbound/unbound_control.pem");
	carg->tls_key_file = strdup("/etc/unbound/unbound_control.key");
	memcpy(carg->host, "127.0.0.1",  strlen("127.0.0.1"));
	memcpy(carg->port, "8953", strlen("8953"));

	tcp_client(carg);
}

void fill_tcpmemcached(context_arg *carg)
{
	carg->tls = 1;
	carg->request_buffer = uv_buf_init(MESG3, sizeof(MESG3));
	carg->tls_ca_file = strdup("/app/certs/ca.key");
	carg->tls_cert_file = strdup("/app/certs/client2.crt");
	carg->tls_key_file = strdup("/app/certs/client2.key");
	memcpy(carg->host, "127.0.0.1",  strlen("127.0.0.1"));
	memcpy(carg->port, "11211", strlen("11211"));
	tcp_client(carg);
}

void fill_news(context_arg *carg)
{
	carg->tls = 1;
	carg->request_buffer = uv_buf_init(MESG, sizeof(MESG));
	memcpy(carg->host, URL,  strlen(URL));
	memcpy(carg->port, PORT, strlen(PORT));
	tcp_client(carg);
}

void fill_nginx(context_arg *carg)
{
	carg->tls = 1;
	carg->request_buffer = uv_buf_init(MESG4, sizeof(MESG4)-1);
	carg->tls_ca_file = strdup("/app/certs/ca.key");
	carg->tls_cert_file = strdup("/app/certs/client2.crt");
	carg->tls_key_file = strdup("/app/certs/client2.key");
	memcpy(carg->host, "127.0.0.1",  strlen("127.0.0.1"));
	memcpy(carg->port, "443", strlen("443"));
	tcp_client(carg);
}

//int main()
//{
//
//	context_arg* carg = calloc(1, sizeof(*carg));
//	uv_loop_t *loop = carg->loop = uv_loop_new();
//
//	//fill_unixunbound(carg);
//	//fill_tcpunbound(carg);
//	fill_news(carg);
//	//fill_tcpmemcached(carg);
//	//fill_nginx(carg);
//	uv_run(loop, UV_RUN_DEFAULT);
//}

void tcp_client_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, tcp_client_crawl, ac->aggregator_startup, ac->aggregator_repeat);
}
