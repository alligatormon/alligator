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
#include "common/entrypoint.h"
#include "parsers/http_proto.h"
#include "parsers/multiparser.h"
#include "events/metrics.h"
#include "events/access.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

//void server_walk_cb(uv_handle_t *handle, void *arg) {
//	if (!uv_is_closing(handle)) {
//		uv_close(handle, NULL);
//	}
//}

void tcp_server_closed_client(uv_handle_t* handle)
{
	context_arg *carg = handle->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tcp server closed client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);
	(srv_carg->close_counter)++;
	carg->close_time_finish = setrtime();

	if (carg->parser_handler && !carg->parser_status)
		carg->parser_status = 1;

	aggregator_events_metric_add(srv_carg, carg, NULL, "tcp", "entrypoint", srv_carg->key);

	if (!carg->body_readed && carg->full_body)
		alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

	carg->is_closing = 0;
	if (carg->tls)
		mbedtls_ssl_free(&carg->tls_ctx);

	if (carg->full_body)
	{
		carg->buffer_request_size = carg->full_body->m;
		string_free(carg->full_body);
	}

	uv_stop(carg->loop);
	//uv_walk(carg->loop, server_walk_cb, NULL);
	//int count = carg->loop->active_reqs.count;
	//for (uint64_t i = 0; i < count; ++i) {
	//	uv_run(carg->loop, UV_RUN_ONCE);
	//}
	uv_loop_close(carg->loop);

    close(carg->loop->backend_fd);
	free(carg);
}

void tcp_server_close_client(context_arg* carg)
{
	carglog(carg, L_INFO, "%"u64": tcp server call close client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	if (uv_is_closing((uv_handle_t*)&carg->client) == 0)
	{
		carg->close_time = setrtime();
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
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tcp server shutdowned client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, status: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	(srv_carg->shutdown_counter)++;
	carg->shutdown_time_finish = setrtime();

	tcp_server_close_client(carg);
}

void tls_server_write(uv_write_t* req, uv_stream_t* handle, char* buffer, size_t buffer_len)
{
	context_arg* carg = handle->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tcp server write client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, is_writing: %d, is_closing: %d, buffer len: %zu\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->is_writing, carg->is_closing, buffer_len);
	(srv_carg->tls_write_counter)++;

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
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tcp server writed %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, status: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	(srv_carg->write_counter)++;
	carg->write_time_finish = setrtime();

	if (carg->response_buffer.base && !strncmp(carg->response_buffer.base, "HTTP", 4))
	{
		carglog(carg, L_INFO, "%"u64": tcp server call shutdown http client %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);
		carg->shutdown_req.data = carg;
		carg->shutdown_time = setrtime();
		uv_shutdown(&carg->shutdown_req, (uv_stream_t*)&carg->client, tcp_server_shutdown_client);
	}

	if (carg->response_buffer.base)
	{
		carglog(carg, L_INFO, "%"u64": tcp server writed call free %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, status: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
		free(carg->response_buffer.base);
		carg->response_buffer = uv_buf_init(NULL, 0);
	}
}

void tcp_server_readed(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)stream->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tcp server readed %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, nread: %zd, EOF: %d, ECONNRESET: %d, ECONNABORTED: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread, (nread == UV_EOF), (nread == UV_ECONNRESET), (nread == UV_ECONNABORTED));
	(srv_carg->read_counter)++;
	carg->read_time_finish = setrtime();

	http_reply_data* hrdata;
	if (!carg->full_body || !carg->full_body->l)
	{
		if (nread > 0) {
			hrdata = http_proto_get_request_data(buf->base, nread, carg->auth_header);
			if (hrdata)
			{
				carg->expect_body_length = hrdata->content_length + hrdata->headers_size;
				carg->body = hrdata->body;
				if (carg->body)
					carg->body_readed = 1;
				else
					carg->body_readed = 0;

				// initial big size of answer only for GET method
				if (!carg->full_body && hrdata->method == HTTP_METHOD_GET)
					carg->full_body = string_init(carg->buffer_request_size);

				http_reply_data_free(hrdata);
			}
		}
	}

	if (!carg->body_readed)
	{
		char *body = strstr(buf->base, "\r\n\r\n");
		if (!body)
			body = strstr(buf->base, "\n\n");

		if (body)
			carg->body_readed = 1;
	}

	if (!carg->full_body)
		carg->full_body = string_init(1024);

	if ((nread) > 0 && (nread < 65536) && carg->expect_body_length <= (nread + carg->full_body->l) && carg->body_readed)
	{
		srv_carg->read_bytes_counter += nread;
		string *str = string_init(carg->buffer_response_size);

		if (buf && buf->base)
			string_cat(carg->full_body, buf->base, nread);

		if (carg->body)
		{
			carg->body = strstr(carg->full_body->s, "\r\n\r\n");
			if (!carg->body)
				carg->body = strstr(carg->full_body->s, "\n\n");
		}

		char *pastr = carg->full_body->l ? carg->full_body->s : buf->base;
		uint64_t paslen = carg->full_body->l ? carg->full_body->l : nread;

		alligator_multiparser(pastr, paslen, carg->parser_handler, str, carg);

		carg->response_buffer = uv_buf_init(str->s, str->l);
		carg->write_req.data = carg;
		if (uv_is_writable((uv_stream_t*)&carg->client))
		{
			if(!carg->tls && uv_is_writable((uv_stream_t *)&carg->client))
			{
				srv_carg->write_bytes_counter += str->l;
				carg->write_time = setrtime();
				uv_write(&carg->write_req, (uv_stream_t*)&carg->client, (const struct uv_buf_t *)&carg->response_buffer, 1, tcp_server_writed);
			}
			else
				tls_server_write(&carg->write_req, (uv_stream_t*)&carg->client, str->s, str->l);
		}
		string_null(carg->full_body);
		carg->buffer_response_size = str->m;
		free(str);
	}
	else if (carg->expect_body_length > (nread + carg->full_body->l))
	{
		srv_carg->read_bytes_counter += nread;
		string_cat(carg->full_body, buf->base, nread);
	}
	else if (nread >= 65536)
	{
		srv_carg->read_bytes_counter += nread;
		string_cat(carg->full_body, buf->base, nread);
	}
	else if (nread == 0)
		return;
	else if (nread == UV_EOF)
	{
		carg->shutdown_req.data = carg;
		carg->shutdown_time = setrtime();
		uv_shutdown(&carg->shutdown_req, (uv_stream_t*)&carg->client, tcp_server_shutdown_client);
	}
	else if (nread == UV_ECONNRESET || nread == UV_ECONNABORTED)
	{
		tcp_server_close_client(carg);
	}
	else
		string_cat(carg->full_body, buf->base, nread);
}

void tls_server_readed(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = stream->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tls server readed %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, nread: %zd, is_closing: %d, handshake over: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread, carg->is_closing, (carg->tls_ctx.state == MBEDTLS_SSL_HANDSHAKE_OVER));
	(srv_carg->tls_read_counter)++;
	carg->tls_read_time_finish = setrtime();

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
		srv_carg->tls_read_bytes_counter += nread;
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
		srv_carg->tls_read_bytes_counter += nread;
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
	context_arg *srv_carg = carg->srv_carg;
	(srv_carg->tls_read_counter)++;
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
	context_arg *srv_carg = carg->srv_carg;
	(srv_carg->tls_write_counter)++;
	carg->tls_write_time_finish = setrtime();
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
	context_arg *srv_carg = carg->srv_carg;
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
		srv_carg->tls_write_bytes_counter += len;
		carg->tls_write_time = setrtime();
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
	context_arg *srv_carg = carg->srv_carg;
	(srv_carg->tls_init_counter)++;
	mbedtls_ssl_init(&carg->tls_ctx);

	if (mbedtls_ssl_setup(&carg->tls_ctx, &carg->tls_conf))
		return;

	mbedtls_ssl_set_bio(&carg->tls_ctx, carg, tls_server_mbed_send, tls_server_mbed_recv, NULL);
}

//void tcp_server_connected(uv_stream_t* stream, int status) 
void tcp_server_connected(uv_work_t *workload)
{
	context_arg *carg = workload->data;
	uv_stream_t* stream = carg->server_stream;

	carglog(carg, L_INFO, "%"u64": tcp server thread %ld accepted on server %p, context %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, pthread_self(), carg->srv_carg, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);
	if (carg->tls)
		tls_server_init_client(carg->loop, carg);

	if (uv_tcp_nodelay(&carg->client, 1))
		tcp_server_close_client(carg);

	if (uv_accept(stream, (uv_stream_t*)&carg->client))
		tcp_server_close_client(carg);

	if (!check_ip_port((uv_tcp_t*)&carg->client, carg))
	{
		carglog(carg, L_ERROR, "no access!\n");
		tcp_server_close_client(carg);
		return;
	}

	carg->read_time = setrtime();
	if (carg->tls)
	{
		carg->tls_read_time = setrtime();
		if (uv_read_start((uv_stream_t*)&carg->client, tls_server_alloc, tls_server_readed))
			tcp_server_close_client(carg);
	}
	else
	{
		if (uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, tcp_server_readed)) {
			tcp_server_close_client(carg);
		}
	}
	//uv_run(carg->loop, UV_RUN_NOWAIT);
	uv_run(carg->loop, UV_RUN_ONCE);
}

void tcp_server_connected_stop(uv_work_t *workload, int status) {
	context_arg *carg = workload->data;
	free(carg->loop);

	free(workload);
}

void tcp_server_connected_threaded(uv_stream_t* stream, int status) {
	context_arg* srv_carg = stream->data;

	if (status != 0)
		return;

	uv_work_t *workload = calloc(1, sizeof(*workload));

	context_arg *carg = malloc(sizeof(*carg));
	memcpy(carg, srv_carg, sizeof(context_arg));
	carglog(carg, L_INFO, "%"u64": tcp server accepted on server %p, context %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, srv_carg, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);
	(srv_carg->conn_counter)++;

	carg->loop = malloc(sizeof *carg->loop);
	uv_loop_init(carg->loop);
	uv_tcp_init(carg->loop, &carg->client);

	carg->srv_carg = srv_carg;
	carg->client.data = carg;
	carg->curr_ttl = carg->ttl;
	workload->data = carg;
	carg->server_stream = stream;
   
	uv_queue_work(uv_default_loop(), workload, tcp_server_connected, tcp_server_connected_stop);
}

context_arg *tcp_server_init(uv_loop_t *loop, const char* ip, int port, uint8_t tls, context_arg *import_carg)
{
	struct sockaddr_in addr;

	context_arg* srv_carg;
	if (import_carg)
		srv_carg = import_carg;
	else
		srv_carg = calloc(1, sizeof(context_arg));

	srv_carg->key = malloc(255);
	strlcpy(srv_carg->host, ip, HOSTHEADER_SIZE);
	snprintf(srv_carg->key, 255, "tcp:%s:%u", srv_carg->host, port);

	carglog(srv_carg, L_INFO, "init server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", loop, tls, srv_carg, srv_carg->host, port);

	srv_carg->loop = loop;
	srv_carg->tls = tls;

	srv_carg->server.data = srv_carg;

	if (srv_carg->tls)
		if (!tls_server_init(srv_carg->loop, srv_carg))
		{
			return NULL;
		}

	if(uv_ip4_addr(srv_carg->host, port, &addr))
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

	int ret = uv_listen((uv_stream_t*)&srv_carg->server, 1024, tcp_server_connected_threaded);
	if (ret)
	{
		carglog(srv_carg, L_FATAL, "Listen '%s:%d' error %s\n", srv_carg->host, port, uv_strerror(ret));
		return NULL;
	}

	alligator_ht_insert(ac->entrypoints, &(srv_carg->context_node), srv_carg, tommy_strhash_u32(0, srv_carg->key));
	return srv_carg;
}

void tcp_server_stop(const char* ip, int port)
{
	char key[255];
	snprintf(key, 255, "tcp:%s:%u", ip, port);
	context_arg *carg = alligator_ht_search(ac->entrypoints, entrypoint_compare, key, tommy_strhash_u32(0, key));
	if (carg)
	{
		uv_close((uv_handle_t*)&carg->server, NULL);
		alligator_ht_remove_existing(ac->entrypoints, &(carg->context_node));
	}
	carg_free(carg);
}

//int main()
//{
//	context_arg* carg = tcp_server_init(uv_default_loop(), "0.0.0.0", 444, 1);
//	uv_run(uv_default_loop(), UV_RUN_DEFAULT);
//}
