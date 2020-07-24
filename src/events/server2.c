#include <stdlib.h>
#include <uv.h>
#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "context_arg.h"
#include "mbedtls/certs.h"
#include "mbedtls/debug.h"
#include "events/debug.h"
#include "events/uv_alloc.h"
#include "main.h"
extern aconf *ac;

void tcp_server_closed_client(uv_handle_t* handle)
{
	context_arg *carg = handle->data;
	if (ac->log_level > 1)
		printf("%"u64": tcp server closed client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	carg->is_closing = 0;
	if (carg->tls)
		mbedtls_ssl_free(&carg->tls_ctx);

	string_free(carg->full_body);
	free(carg);
}

void tcp_server_close_client(context_arg* carg)
{
	if (ac->log_level > 1)
		printf("%"u64": tcp server call close client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	if (uv_is_closing((uv_handle_t*)&carg->client) == 0)
	{
		if (carg->tls)
		{
			carg->is_closing = 1;

			int ret = mbedtls_ssl_close_notify(&carg->tls_ctx);
			if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
			{
				if (uv_is_closing((uv_handle_t*)&carg->client) == 0)
					uv_close((uv_handle_t *)&carg->client, tcp_server_closed_client);
			}
		}
		else
			uv_close((uv_handle_t*)&carg->client, tcp_server_closed_client);
	}
}

void tcp_server_shutdown_client(uv_shutdown_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;
	if (ac->log_level > 1)
		printf("%"u64": tcp server shutdowned client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, status: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);

	tcp_server_close_client(carg);
}

void tls_server_write(uv_write_t* req, uv_stream_t* handle, char* buffer, size_t buffer_len)
{
	context_arg* carg = handle->data;
	if (ac->log_level > 1)
		printf("%"u64": tcp server write client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, is_writing: %d, is_closing: %d, buffer len: %zu\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->is_writing, carg->is_closing, buffer_len);

	if (carg->is_writing || carg->is_closing)
		return;

	carg->is_write_error = 0;
	carg->ssl_write_buffer_len = buffer_len;
	carg->ssl_write_buffer = buffer;
	carg->ssl_read_buffer_len = 0;
	carg->ssl_read_buffer_offset = 0;
	carg->ssl_write_offset = 0;
	carg->is_async_writing = 0;
	if (mbedtls_ssl_write(&carg->tls_ctx, (const unsigned char *)buffer, buffer_len) == MBEDTLS_ERR_SSL_WANT_WRITE)
		carg->is_writing = 1;
}

void tcp_server_writed(uv_write_t* req, int status) 
{
	context_arg *carg = req->data;
	if (ac->log_level > 1)
		printf("%"u64": tcp server writed %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, status: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);

	if (carg->response_buffer.base && !strncmp(carg->response_buffer.base, "HTTP", 4))
	{
		if (ac->log_level > 1)
			printf("%"u64": tcp server call shutdown http client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);
		carg->shutdown_req.data = carg;
		uv_shutdown(&carg->shutdown_req, (uv_stream_t*)&carg->client, tcp_server_shutdown_client);
	}

	if (carg->response_buffer.base)
	{
		if (ac->log_level > 1)
			printf("%"u64": tcp server writed call free %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, status: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
		free(carg->response_buffer.base);
		carg->response_buffer = uv_buf_init(NULL, 0);
	}
}

void tcp_server_readed(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)stream->data;
	if (ac->log_level > 1)
		printf("%"u64": tcp server readed %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, nread: %zd, EOF: %d, ECONNRESET: %d, ECONNABORTED: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread, (nread == UV_EOF), (nread == UV_ECONNRESET), (nread == UV_ECONNABORTED));

	if ((nread) > 0 && (nread < 65536))
	{
		string *str = string_init(6553500);
		char *pastr = carg->full_body->l ? carg->full_body->s : buf->base;
		uint64_t paslen = carg->full_body->l ? carg->full_body->l : nread;
		//alligator_multiparser(buf->base, nread, carg->parser_handler, str, carg);
		alligator_multiparser(pastr, paslen, carg->parser_handler, str, carg);
		carg->write_req.data = carg;
		carg->response_buffer = uv_buf_init(str->s, str->l);
		if (uv_is_writable((uv_stream_t*)&carg->client))
		{
			if(!carg->tls && uv_is_writable((uv_stream_t *)&carg->client))
				uv_write(&carg->write_req, (uv_stream_t*)&carg->client, (const struct uv_buf_t *)&carg->response_buffer, 1, tcp_server_writed);
			else
				tls_server_write(&carg->write_req, (uv_stream_t*)&carg->client, str->s, str->l);
		}
		free(str);
	}
	if ((nread) > 0 && (nread >= 65536))
	{
		string_cat(carg->full_body, buf->base, nread);
	}
	else if (nread == 0)
		return;
	else if (nread == UV_EOF)
	{
		carg->shutdown_req.data = carg;
		uv_shutdown(&carg->shutdown_req, (uv_stream_t*)&carg->client, tcp_server_shutdown_client);
	}
	else if (nread == UV_ECONNRESET || nread == UV_ECONNABORTED)
	{
		tcp_server_close_client(carg);
	}
}

void tls_server_readed(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = stream->data;
	if (ac->log_level > 1)
		printf("%"u64": tls server readed %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, nread: %zd, is_closing: %d, handshake over: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread, carg->is_closing, (carg->tls_ctx.state == MBEDTLS_SSL_HANDSHAKE_OVER));

	if (nread <= 0)
	{
		tcp_server_readed(stream, nread, 0);
		return;
	}

	if (carg->is_closing)
	{
		mbedtls_ssl_close_notify(&carg->tls_ctx);
		if (uv_is_closing((uv_handle_t*)carg) == 0)
			uv_close((uv_handle_t*)stream, tcp_server_closed_client);
	}
	else if (carg->tls_ctx.state != MBEDTLS_SSL_HANDSHAKE_OVER)
	{
		int ret = 0;
		carg->ssl_read_buffer_len = nread;
		carg->ssl_read_buffer_offset = 0;
		while ((ret = mbedtls_ssl_handshake_step(&carg->tls_ctx)) == 0)
		{
			if (carg->tls_ctx.state == MBEDTLS_SSL_HANDSHAKE_OVER)
				break;
		}
		if (carg->ssl_read_buffer_offset != nread)
			nread = -1;
		carg->ssl_read_buffer_len = 0;
		carg->ssl_read_buffer_offset = 0;
		if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
			nread = -1;
	}
	else {
		int read_len = 0;
		carg->ssl_read_buffer_len = nread;
		carg->ssl_read_buffer_offset = 0;

		while((read_len = mbedtls_ssl_read(&carg->tls_ctx, (unsigned char *)carg->user_read_buf.base,  carg->user_read_buf.len)) > 0)
			tcp_server_readed(stream, read_len, &carg->user_read_buf);

		if (read_len !=0 && read_len != MBEDTLS_ERR_SSL_WANT_READ)
		{
			if (MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY == read_len)
				nread = UV_ECONNABORTED;
			else
				nread = -1;
		}
	}

	if (nread <= 0)
		tcp_server_readed(stream, nread, 0);
}

void tls_server_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	context_arg* carg = handle->data;
	carg->user_read_buf.base = 0;
	carg->user_read_buf.len = 0;
	tcp_alloc(handle, suggested_size, &carg->user_read_buf);
	buf->base = carg->ssl_read_buffer;
	buf->len = sizeof(carg->ssl_read_buffer);
}

int tls_server_mbed_recv(void *ctx, unsigned char *buf, size_t len)
{
	context_arg* carg= (context_arg*)ctx;
	int need_copy = (carg->ssl_read_buffer_len - carg->ssl_read_buffer_offset) > len ? len : (carg->ssl_read_buffer_len - carg->ssl_read_buffer_offset);
	if (need_copy == 0)
		return MBEDTLS_ERR_SSL_WANT_READ;

	memcpy(buf, carg->ssl_read_buffer + carg->ssl_read_buffer_offset, need_copy);
	carg->ssl_read_buffer_offset += need_copy;
	return need_copy ;
}

void tls_server_writed(uv_write_t* req, int status)
{
	context_arg* carg = req->data;
	int ret;
	if (carg->is_write_error)
	{
		carg->is_writing = 0;
		carg->is_write_error = 1;
	}
	if (carg->is_closing)
	{
		mbedtls_ssl_close_notify(&carg->tls_ctx);
		//if (uv_is_closing((uv_handle_t*)carg) == 0)
			//uv_close((uv_handle_t*)&carg->client, tcp_server_closed_client);
	}
	else if (carg->tls_ctx.state != MBEDTLS_SSL_HANDSHAKE_OVER)
	{
		while ((ret = mbedtls_ssl_handshake_step(&carg->tls_ctx)) == 0)
		{
			if (carg->tls_ctx.state == MBEDTLS_SSL_HANDSHAKE_OVER)
				break;
		}
	}
	else
	{
		ret = mbedtls_ssl_write(&carg->tls_ctx, (const unsigned char *)carg->ssl_write_buffer + carg->ssl_write_offset, carg->ssl_write_buffer_len - carg->ssl_write_offset);
		if (ret > 0)
		{
			carg->ssl_write_offset += ret;
			if (carg->ssl_write_offset == carg->ssl_write_buffer_len)
				carg->is_writing = 0;
		}
	}
	free(carg->write_buffer.base);
	carg->write_buffer.base = 0;
	//tcp_server_writed(req, status);
}

int tls_server_mbed_send(void *ctx, const unsigned char *buf, size_t len)
{
	context_arg* carg = (context_arg*)ctx;
	carg->write_buffer.base = 0;
	carg->write_buffer.len = 0;
	if (carg->is_write_error)
		return -1;
	printf("ssl sent %zu/%zu/%d/%d/%d\n", len, mbedtls_ssl_get_max_frag_len(&carg->tls_ctx), mbedtls_ssl_get_max_out_record_payload(&carg->tls_ctx), MBEDTLS_SSL_OUT_CONTENT_LEN, mbedtls_ssl_get_max_out_record_payload(&carg->tls_ctx));

	if (carg->is_async_writing == 0)
	{
		carg->write_buffer.base = malloc(len);
		memcpy(carg->write_buffer.base, buf, len);
		carg->write_buffer.len = len;
		carg->write_req.data = carg;
		int ret = uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->write_buffer, 1, tls_server_writed);
		if (!ret)
		{
			len = MBEDTLS_ERR_SSL_WANT_WRITE;
			carg->is_async_writing = 1;
		}
		else
		{
			len = -1;
			carg->is_write_error = 1;
			carg->is_writing = 0;

			if (carg->write_buffer.base)
			{
				free(carg->write_buffer.base);
				carg->write_buffer.base = 0;
			}
		}
	}
	else
		carg->is_async_writing = 0;

	return len;
}

int8_t tls_server_init(uv_loop_t* loop, context_arg* carg)
{
	mbedtls_ssl_config_init(&carg->tls_conf);
	mbedtls_x509_crt_init(&carg->tls_cert);
	mbedtls_ctr_drbg_init(&carg->tls_ctr_drbg);
	mbedtls_pk_init(&carg->tls_key);
	mbedtls_entropy_init(&carg->tls_entropy); // TODO: UB

	if (mbedtls_ctr_drbg_seed(&carg->tls_ctr_drbg, mbedtls_entropy_func, &carg->tls_entropy, (const unsigned char *) "Alligator", sizeof("Alligator") -1))
		return 0;

	if (mbedtls_x509_crt_parse(&carg->tls_cert, (const unsigned char *) mbedtls_test_srv_crt, mbedtls_test_srv_crt_len) < 0)
		return 0;

	if (mbedtls_x509_crt_parse(&carg->tls_cert, (const unsigned char *) mbedtls_test_cas_pem, mbedtls_test_cas_pem_len))
		return 0;

	if (mbedtls_pk_parse_key(&carg->tls_key, (const unsigned char *) mbedtls_test_srv_key, mbedtls_test_srv_key_len, NULL, 0) < 0)
		return 0;

	if (mbedtls_ssl_config_defaults(&carg->tls_conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT))
		return 0;

	mbedtls_ssl_conf_authmode(&carg->tls_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
	mbedtls_ssl_conf_ca_chain(&carg->tls_conf, &carg->tls_cert, NULL);
	if (mbedtls_ssl_conf_own_cert(&carg->tls_conf, &carg->tls_cert, &carg->tls_key))
		return 0;

	mbedtls_ssl_conf_rng(&carg->tls_conf, mbedtls_ctr_drbg_random, &carg->tls_ctr_drbg);

	mbedtls_ssl_conf_dbg(&carg->tls_conf, tls_debug, stdout);
	mbedtls_debug_set_threshold(0);

	return 1;
}

void tls_server_init_client(uv_loop_t* loop, context_arg* carg)
{
	mbedtls_ssl_init(&carg->tls_ctx);

	if (mbedtls_ssl_setup(&carg->tls_ctx, &carg->tls_conf))
		return;

	mbedtls_ssl_set_bio(&carg->tls_ctx, carg, tls_server_mbed_send, tls_server_mbed_recv, NULL);
}

void tcp_server_connected(uv_stream_t* stream, int status) 
{
	context_arg* srv_carg = stream->data;

	if (status != 0)
		return;

	context_arg *carg = malloc(sizeof(*carg));
	memcpy(carg, srv_carg, sizeof(context_arg));
	if (ac->log_level > 1)
		printf("%"u64": tcp server accepted on server %p, context %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, srv_carg, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	uv_tcp_init(carg->loop, &carg->client);

	carg->client.data = carg;
	carg->full_body = string_init(6553500);
	carg->curr_ttl = carg->ttl;

	if (carg->tls)
		tls_server_init_client(carg->loop, carg);

	if (uv_tcp_nodelay(&carg->client, 1))
		tcp_server_close_client(carg);

	if (uv_accept(stream, (uv_stream_t*)&carg->client))
		tcp_server_close_client(carg);

	if (!check_ip_port((uv_tcp_t*)&carg->client, carg))
	{
		if (ac->log_level > 3)
			printf("no access!\n");
		tcp_server_close_client(carg);
		return;
	}

	if (carg->tls)
	{
		if (uv_read_start((uv_stream_t*)&carg->client, tls_server_alloc, tls_server_readed))
			tcp_server_close_client(carg);
	}
	else
	{
		if (uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, tcp_server_readed))
			tcp_server_close_client(carg);
	}
}

context_arg *tcp_server_init(uv_loop_t *loop, const char* ip, int port, uint8_t tls, context_arg *import_carg)
{
	struct sockaddr_in addr;

	context_arg* srv_carg;
	if (import_carg)
		srv_carg = import_carg;
	else
		srv_carg = calloc(1, sizeof(context_arg));

	if (ac->log_level > 1)
		printf("init server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", loop, tls, srv_carg, ip, port);

	srv_carg->loop = loop;
	srv_carg->tls = tls;

	srv_carg->server.data = srv_carg;

	if (srv_carg->tls)
		if (!tls_server_init(srv_carg->loop, srv_carg))
		{
			return NULL;
		}

	if(uv_ip4_addr(ip, port, &addr))
	{
		return NULL;
	}

	uv_tcp_init(srv_carg->loop, &srv_carg->server);
	if(uv_tcp_bind(&srv_carg->server, (const struct sockaddr*)&addr, 0))
	{
		return NULL;
	}

	if (uv_tcp_nodelay(&srv_carg->server, 1))
	{
		return NULL;
	}

	int ret = uv_listen((uv_stream_t*)&srv_carg->server, 1024, tcp_server_connected);
	if (ret)
	{
		fprintf(stderr, "Listen error %s\n", uv_strerror(ret));
		return NULL;
	}

	return srv_carg;
}

//int main()
//{
//	context_arg* carg = tcp_server_init(uv_default_loop(), "0.0.0.0", 444, 1);
//	uv_run(uv_default_loop(), UV_RUN_DEFAULT);
//}
