#include "context_arg.h"
#include "mbedtls/certs.h"
#include "mbedtls/debug.h"
#include "events/uv_alloc.h"
#include "events/debug.h"
#include "main.h"
#include "parsers/http_proto.h"
extern aconf* ac;

void tcp_connected(uv_connect_t* req, int status);

void tcp_client_closed(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	if (ac->log_level > 1)
		printf("%"u64": tcp client closed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, TTL: %"d64"\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->context_ttl);
	(carg->close_counter)++;
	carg->close_time_finish = setrtime();

	aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);
	metric_add_labels5("alligator_parser_status", &carg->parsed, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);
	uv_timer_stop(&carg->tt_timer);

	if (carg->tls)
	{
		mbedtls_x509_crt_free(&carg->tls_cacert);
		mbedtls_x509_crt_free(&carg->tls_cert);
		mbedtls_pk_free(&carg->tls_key);
		mbedtls_ssl_free(&carg->tls_ctx);
		mbedtls_ssl_config_free(&carg->tls_conf);
		mbedtls_ctr_drbg_free(&carg->tls_ctr_drbg);
		mbedtls_entropy_free(&carg->tls_entropy);
		mbedtls_ssl_free(&carg->tls_ctx);
	}
	carg->lock = 0;
	string_null(carg->full_body);

	if (carg->context_ttl)
	{
		r_time time = setrtime();
		if (time.sec >= carg->context_ttl)
			smart_aggregator_del(carg);
			//tcp_client_del(carg->key);
			//carg_free(carg);
	}
}

void tcp_client_close(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	if (ac->log_level > 1)
		printf("%"u64": tls client call close %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	const mbedtls_x509_crt* peercert = mbedtls_ssl_get_peer_cert(&carg->tls_ctx);
	parse_cert_info(peercert, carg->host, carg->host);
	//mbedtls_x509_crt_free(peercert);

	carg->close_time = setrtime();
	if (!uv_is_closing((uv_handle_t*)&carg->client) && !carg->is_closing)
	{
		if (carg->tls)
		{
			//printf("is writing: %d, is closing: %d\n", carg->is_writing, carg->is_closing);
			if (!carg->is_writing)
			{
				int ret = mbedtls_ssl_close_notify(&carg->tls_ctx);
				if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
				{
					carg->is_closing = 1;
					uv_close((uv_handle_t*)&carg->client, tcp_client_closed);
				}
			}
			else
			{
				carg->is_closing = 1;
				uv_close((uv_handle_t*)&carg->client, tcp_client_closed);
			}
		}
		else
		{
			carg->is_closing = 1;
			uv_close((uv_handle_t*)&carg->client, tcp_client_closed);
		}
	}
}

void tcp_client_shutdown(uv_shutdown_t* req, int status)
{
	context_arg* carg = req->data;
	if (ac->log_level > 1)
		printf("%"u64": tcp client shutdowned %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	(carg->shutdown_counter)++;
	carg->shutdown_time_finish = setrtime();

	tcp_client_close((uv_handle_t *)&carg->client);
	free(req);
}

void tcp_client_readed(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)stream->data;
	//printf("tcp_client_readed: nread %lld, EOF: %d, UV_ECONNRESET: %d, UV_ECONNABORTED: %d, UV_ENOBUFS: %d\n", nread, UV_EOF, UV_ECONNRESET, UV_ECONNABORTED, UV_ENOBUFS);

	if (ac->log_level > 1)
		printf("%"u64": tcp client readed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread);
	
	(carg->read_counter)++;
	carg->read_time_finish = setrtime();

	if (nread > 0)
	{
		if (buf && buf->base)
			buf->base[nread] = 0;
		if (carg && carg->log_level > 99)
		{
			puts("");
			printf("==================BASE===================\n'%s'\n======\n", buf->base);
		}
		carg->read_bytes_counter += nread;
		if (buf && buf->base)
		{
			if (!carg->full_body->l)
			{
				http_reply_data* hr_data = http_reply_parser(buf->base, nread);
				if (hr_data)
				{
					http_hrdata_metrics(carg, hr_data);
					http_follow_redirect(carg, hr_data);
					//printf("INIT nread %zd, clear http size %"d64", full body size %"d64"\n", nread, hr_data->clear_http->l, carg->full_body->l);
					string_string_cat(carg->full_body, hr_data->clear_http);

					carg->is_http_query = 1;
					carg->chunked_size = hr_data->chunked_size;
					carg->chunked_expect = hr_data->chunked_expect;
					carg->headers_size = hr_data->headers_size;
					//printf("chunked_size %u, expect %d\n", carg->chunked_size, carg->chunked_expect);
					carg->expect_body_length = hr_data->content_length;

					if (carg->chunked_expect)
						chunk_calc(carg, hr_data->body, hr_data->body_size);

					http_reply_free(hr_data);
				}
				else
				{
					//printf("string_cat 3: %zd\n", nread);
					string_cat(carg->full_body, buf->base, nread);
				}
			}
			else if (carg->chunked_expect) // maybe chunked_expect?
			{
				chunk_calc(carg, buf->base, nread);
			}
			else
			{
				string_cat(carg->full_body, buf->base, nread);
			}

			int8_t rc = tcp_check_full(carg, carg->full_body->s, carg->full_body->l);
			//printf("tcp_check_full: %d: %p\n", rc, carg->full_body->s);
			if (rc)
			{
				alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
			}
		}
	}

	else if (nread == 0)
		return;

	else if(nread == UV_EOF)
	{
		if (carg->full_body->l && !carg->parsed)
			alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

		uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
		req->data = carg;
		carg->shutdown_time = setrtime();
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
	if (ac->log_level > 1)
		printf("%"u64": tls client readed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zu\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread);
	(carg->tls_read_counter)++;
	carg->tls_read_time_finish = setrtime();

	if (nread <= 0)
	{
		tcp_client_readed(stream, nread, 0);
		return;
	}

	carg->tls_read_bytes_counter += nread;

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
		{
			tcp_connected(&carg->connect,  0);
		}
		carg->ssl_read_buffer_len = 0;
		carg->ssl_read_buffer_offset = 0;
		if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
			tcp_connected(&carg->connect,  0);
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
	(carg->tls_write_counter)++;
	carg->tls_write_time_finish = setrtime();
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
			tcp_connected(&carg->connect,  0);
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
		carg->tls_write_bytes_counter += len;
		carg->tls_write_time = setrtime();
		ret = uv_write(write_req, (uv_stream_t*)&carg->client, &carg->write_buffer, 1, tls_client_writed);
		if (ret != 0)
		{
			len = -1;
			carg->is_write_error = 1;
			if (carg->tls_ctx.state != MBEDTLS_SSL_HANDSHAKE_OVER)
			{
				tcp_connected(&carg->connect,  -1);
			}
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
	(carg->tls_init_counter)++;
	int ret = 0;

	mbedtls_ssl_init(&carg->tls_ctx);
	mbedtls_ssl_config_init(&carg->tls_conf);
	mbedtls_x509_crt_init(&carg->tls_cacert);
	mbedtls_x509_crt_init(&carg->tls_cert);
	mbedtls_ctr_drbg_init(&carg->tls_ctr_drbg);
	mbedtls_entropy_init(&carg->tls_entropy);
	mbedtls_pk_init(&carg->tls_key);
 
	if((ret = mbedtls_ctr_drbg_seed(&carg->tls_ctr_drbg, mbedtls_entropy_func, &carg->tls_entropy, (const unsigned char *) "Alligator", sizeof("Alligator") -1)) != 0)
		return ret;
 
	if (!carg->tls_ca_file)
	{
		if(mbedtls_x509_crt_parse(&carg->tls_cacert, (const unsigned char *) mbedtls_test_cas_pem, mbedtls_test_cas_pem_len))
			return ret;
	}
	else
	{
		printf("get certs %s:%s:%s\n", carg->tls_ca_file, carg->tls_cert_file, carg->tls_key_file);
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
	int ret = 0;
	context_arg* carg = (context_arg*)req->data;
	if (ac->log_level > 1)
		printf("%"u64": tls client connected %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, status: %d\n", carg->count, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	carg->connect_time_finish = setrtime();

	if (status < 0)
	{
		tcp_connected(&carg->connect,  -1);
		return;
	}
	carg->tls_read_time = setrtime();
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
	context_arg* carg = (context_arg*)req->data;
	if (ac->log_level > 1)
		printf("%"u64": tcp client connected %p(%p:%p) with key %s, parser name %s, hostname %s, port: %s tls: %d, status: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->parser_name, carg->host, carg->port, carg->tls, status);
	(carg->conn_counter)++;

	uint64_t ok = 1;
	if (status < 0)
	{
		ok = 0;
		metric_add_labels5("alligator_connect_ok", &ok, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);
		return;
	}

	metric_add_labels5("alligator_connect_ok", &ok, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);

	carg->read_time = setrtime();
	if (!carg->tls)
	{
		carg->connect_time_finish = setrtime();
		uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, tcp_client_readed);
	}

	memset(&carg->write_req, 0, sizeof(carg->write_req));

	carg->write_req.data = carg;

	if(carg->tls)
	{
		carg->tls_connect_time_finish = setrtime();
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
	{
		uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->request_buffer, 1, NULL);
		carg->write_bytes_counter += carg->request_buffer.len;
		(carg->write_counter)++;
	}
}

void tcp_timeout_timer(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (ac->log_level > 1)
		printf("%"u64": timeout tcp client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);
	(carg->timeout_counter)++;

	if (!carg->parsed && carg->full_body->l)
		alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
	tcp_client_close((uv_handle_t *)&carg->client);
}

void tcp_client_connect(void *arg)
{
	context_arg *carg = arg;
	carg->count = 0;
	if (ac->log_level > 1)
		printf("%"u64": tcp client connect %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->port, carg->tls, carg->lock, carg->timeout);

	if (carg->lock)
		return;

	carg->lock = 1;
	carg->parsed = 0;
	carg->is_closing = 0;
	carg->curr_ttl = carg->ttl;

	carg->tt_timer.data = carg;
	uv_timer_init(carg->loop, &carg->tt_timer);
	uv_timer_start(&carg->tt_timer, tcp_timeout_timer, carg->timeout, 0);
	memset(&carg->connect, 0, sizeof(carg->connect));
	carg->connect.data = carg;

	memset(&carg->client, 0, sizeof(carg->client));
	carg->client.data = carg;
	uv_tcp_init(carg->loop, &carg->client);
	if (carg->tls)
		tls_client_init(carg->loop, carg);

	carg->connect_time = setrtime();
	if (carg->tls)
	{
		carg->tls_connect_time = setrtime();
		uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)carg->dest, tls_connected);
	}
	else
		uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)carg->dest, tcp_connected);
}

void unix_client_connect(void *arg)
{
	context_arg *carg = arg;
	carg->count = 0;
	if (ac->log_level > 1)
		printf("%"u64": unix client connect %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);

	carg->lock = 1;
	carg->parsed = 0;
	carg->is_closing = 0;
	carg->curr_ttl = carg->ttl;

	carg->tt_timer.data = carg;
	uv_timer_init(carg->loop, &carg->tt_timer);
	uv_timer_start(&carg->tt_timer, tcp_timeout_timer, carg->timeout, 0);
	memset(&carg->connect, 0, sizeof(carg->connect));
	carg->connect.data = carg;

	memset(&carg->client, 0, sizeof(carg->client));
	carg->client.data = carg;

	uv_pipe_init(carg->loop, (uv_pipe_t*)&carg->client, 0); 
	carg->connect.data = carg;
	carg->client.data = carg;
	if (carg->tls)
		tls_client_init(carg->loop, carg);

	carg->connect_time = setrtime();
	if (carg->tls)
	{
		carg->tls_connect_time = setrtime();
		uv_pipe_connect(&carg->connect, (uv_pipe_t *)&carg->client, carg->host, tls_connected);
	}
	else
		uv_pipe_connect(&carg->connect, (uv_pipe_t *)&carg->client, carg->host, tcp_connected);
}

static void tcp_client_crawl(uv_timer_t* handle) {
	(void)handle;
	tommy_hashdyn_foreach(ac->aggregator, tcp_client_connect);
	tommy_hashdyn_foreach(ac->uggregator, unix_client_connect);
}

void aggregator_getaddrinfo(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
	context_arg* carg = (context_arg*)req->data;
	if (ac->log_level > 1)
		printf("getaddrinfo tcp client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);

	char addr[17] = {'\0'};
	if (status < 0)
	{
		uv_freeaddrinfo(res);
		free(req);
		return;
	}

	uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);

	carg->dest = (struct sockaddr_in*)res->ai_addr;
	//carg->key = malloc(64);
	//snprintf(carg->key, 64, "tcp://%s:%u", addr, htons(carg->dest->sin_port));

	tommy_hashdyn_insert(ac->aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));

	//uv_freeaddrinfo(res);
	free(req);
}

void aggregator_resolve_host(context_arg* carg)
{
	if (ac->log_level > 1)
		printf("resolve host call tcp client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);
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

char* tcp_client(void *arg)
{
	if (!arg)
		return NULL;

	context_arg *carg = arg;

	aggregator_resolve_host(carg);
	return "tcp";
}

char* unix_tcp_client(context_arg* carg)
{
	if (!carg)
		return NULL;

	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);

	tommy_hashdyn_insert(ac->uggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "unix";
}

void tcp_client_del(context_arg *carg)
{
	if (!carg)
		return;

	//if (!key)
	//	return;

	//context_arg *carg = tommy_hashdyn_search(ac->aggregators, aggregator_compare, key, tommy_strhash_u32(0, key));
	if (carg)
	{
		tommy_hashdyn_remove_existing(ac->aggregators, &(carg->context_node));
		
		if (carg->lock)
		{
			r_time time = setrtime();
			carg->context_ttl = time.sec;
			tcp_client_close((uv_handle_t *)&carg->client);
		}
		else
		{
			carg->lock = 1;
			tommy_hashdyn_remove_existing(ac->aggregator, &(carg->node));
			carg_free(carg);
		}
	}
}

void unix_tcp_client_del(context_arg *carg)
{
	if (!carg)
		return;

	//if (!key)
	//	return;

	//context_arg *carg = tommy_hashdyn_search(ac->aggregators, aggregator_compare, key, tommy_strhash_u32(0, key));
	if (carg)
	{
		tommy_hashdyn_remove_existing(ac->aggregators, &(carg->context_node));
		
		if (carg->lock)
		{
			r_time time = setrtime();
			carg->context_ttl = time.sec;
			tcp_client_close((uv_handle_t *)&carg->client);
		}
		else
		{
			carg->lock = 1;
			tommy_hashdyn_remove_existing(ac->aggregator, &(carg->node));
			carg_free(carg);
		}
	}
}

//void fill_unixunbound(context_arg *carg)
//{
//	carg->tls = 1;
//	carg->request_buffer = uv_buf_init(MESG2, sizeof(MESG2));
//	carg->tls_ca_file = strdup("/etc/unbound/unbound_server.pem");
//	carg->tls_cert_file = strdup("/etc/unbound/unbound_control.pem");
//	carg->tls_key_file = strdup("/etc/unbound/unbound_control.key");
//	memcpy(carg->host, "/var/run/unbound.sock",  strlen("/var/run/unbound.sock"));
//
//	unix_tcp_client(carg);
//}
//
//void fill_tcpunbound(context_arg *carg)
//{
//	carg->tls = 1;
//	carg->request_buffer = uv_buf_init(MESG2, sizeof(MESG2));
//	carg->tls_ca_file = strdup("/etc/unbound/unbound_server.pem");
//	carg->tls_cert_file = strdup("/etc/unbound/unbound_control.pem");
//	carg->tls_key_file = strdup("/etc/unbound/unbound_control.key");
//	memcpy(carg->host, "127.0.0.1",  strlen("127.0.0.1"));
//	memcpy(carg->port, "8953", strlen("8953"));
//
//	tcp_client(carg);
//}
//
//void fill_tcpmemcached(context_arg *carg)
//{
//	carg->tls = 1;
//	carg->request_buffer = uv_buf_init(MESG3, sizeof(MESG3));
//	carg->tls_ca_file = strdup("/app/certs/ca.key");
//	carg->tls_cert_file = strdup("/app/certs/client2.crt");
//	carg->tls_key_file = strdup("/app/certs/client2.key");
//	memcpy(carg->host, "127.0.0.1",  strlen("127.0.0.1"));
//	memcpy(carg->port, "11211", strlen("11211"));
//	tcp_client(carg);
//}
//
//void fill_news(context_arg *carg)
//{
//	carg->tls = 1;
//	carg->request_buffer = uv_buf_init(MESG, sizeof(MESG));
//	memcpy(carg->host, URL,  strlen(URL));
//	memcpy(carg->port, PORT, strlen(PORT));
//	tcp_client(carg);
//}
//
//void fill_nginx(context_arg *carg)
//{
//	carg->tls = 1;
//	carg->request_buffer = uv_buf_init(MESG4, sizeof(MESG4)-1);
//	carg->tls_ca_file = strdup("/app/certs/ca.key");
//	carg->tls_cert_file = strdup("/app/certs/client2.crt");
//	carg->tls_key_file = strdup("/app/certs/client2.key");
//	memcpy(carg->host, "127.0.0.1",  strlen("127.0.0.1"));
//	memcpy(carg->port, "443", strlen("443"));
//	tcp_client(carg);
//}

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
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(loop, timer1);
	uv_timer_start(timer1, tcp_client_crawl, ac->aggregator_startup, ac->aggregator_repeat);
}
