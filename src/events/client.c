#include "context_arg.h"
#include "mbedtls/certs.h"
#include "mbedtls/debug.h"
#include "events/uv_alloc.h"
#include "events/debug.h"
#include "events/fragmentation.h"
#include "main.h"
#include "parsers/http_proto.h"
#include "dstructures/uv_cache.h"
#include "events/metrics.h"
#include "cluster/later.h"
#include "resolver/resolver.h"
#include "x509/type.h"
#include "common/selector.h"
#include "parsers/multiparser.h"
#include "common/units.h"
#include "common/logs.h"
extern aconf* ac;

void tcp_connected(uv_connect_t* req, int status);

void tcp_client_closed(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	carg->close_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tcp client closed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, TTL: %"d64"\n", carg->count++, getrtime_now_ms(carg->close_time_finish), getrtime_sec_float(carg->close_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->context_ttl);
	(carg->close_counter)++;


	aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);

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

	if (carg->write_buffer.base)
	{
		free(carg->write_buffer.base);
		carg->write_buffer.base = 0;
	}

	if (carg->context_ttl)
	{
		r_time time = setrtime();
		if (time.sec >= carg->context_ttl)
		{
			carg->remove_from_hash = 1;
			smart_aggregator_del(carg);
		}
	}

	if (carg->period)
		uv_timer_set_repeat(carg->period_timer, carg->period);
}

void tcp_client_close(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	carg->close_time = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client call close %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, getrtime_now_ms(carg->close_time), getrtime_sec_float(carg->close_time, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	carg->tt_timer->data = NULL;

	const mbedtls_x509_crt* peercert = mbedtls_ssl_get_peer_cert(&carg->tls_ctx);
	parse_cert_info(peercert, carg->host, carg->host);

	if (!uv_is_closing((uv_handle_t*)&carg->client) && !carg->is_closing)
	{
		if (carg->tls)
		{
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
	carg->shutdown_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tcp client shutdowned %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d\n", carg->count++, getrtime_now_ms(carg->shutdown_time_finish), getrtime_sec_float(carg->shutdown_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	(carg->shutdown_counter)++;

	tcp_client_close((uv_handle_t *)&carg->client);
	free(req);
}

void tcp_client_readed(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)stream->data;

	carg->read_time_finish = setrtime();
	carglog(carg, L_DEBUG, "%"u64": [%"PRIu64"/%lf] tcp client readed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, getrtime_now_ms(carg->read_time_finish), getrtime_sec_float(carg->read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread);
	
	(carg->read_counter)++;

	if (nread > 0)
	{
		uint64_t chunksize = 0;
		if (buf && buf->base)
			buf->base[nread] = 0;
		carglog(carg, L_TRACE, "\n==================BASE===================\n'%s'\n======\n", buf? buf->base : "");
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
					string_string_cat(carg->full_body, hr_data->clear_http);

					carg->is_http_query = 1;
					carg->chunked_expect = hr_data->chunked_expect;
					carg->headers_size = hr_data->headers_size;
					carg->body = carg->chunked_ptr = carg->full_body->s + hr_data->body_offset; //
					size_t body_size = carg->full_body->l - hr_data->body_offset; //
					carg->expect_body_length = hr_data->content_length;

					if (carg->chunked_expect)
					{
						memset(&carg->chunked_dec, 0, sizeof(carg->chunked_dec));
						chunksize = chunk_calc(carg, carg->body, body_size, 0);
						carg->body[chunksize + 1] = 0;
						string_break(carg->full_body, 0, carg->full_body->l - (body_size - chunksize));
					}

					http_reply_data_free(hr_data);
				}
				else
				{
					string_cat(carg->full_body, buf->base, nread);
				}
			}
			else if (carg->chunked_expect) // maybe chunked_expect?
			{
				chunksize = chunk_calc(carg, buf->base, nread, 1);
			}
			else
			{
				string_cat(carg->full_body, buf->base, nread);
			}

			int8_t rc = tcp_check_full(carg, carg->full_body->s, carg->full_body->l, chunksize);
			if (rc)
			{
				alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
				uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
				req->data = carg;
				carg->shutdown_time = setrtime();
				uv_shutdown(req, (uv_stream_t*)&carg->client, tcp_client_shutdown);
				tcp_client_close((uv_handle_t *)&carg->client);
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
	{
		tcp_client_close((uv_handle_t *)&carg->client);
	}
	else if (nread == UV_ENOBUFS)
	{
		tcp_client_close((uv_handle_t *)&carg->client);
	}
}

void tls_client_readed(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = stream->data;
	carg->tls_read_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client readed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread);
	(carg->tls_read_counter)++;

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
		{
			tcp_client_readed(stream, read_len, &carg->user_read_buf);
		}

		if (read_len !=0 && read_len != MBEDTLS_ERR_SSL_WANT_READ)
		{
			if (MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY == read_len)
			{
				tcp_client_readed(stream, nread, 0);
			}
			else
			{
				tcp_client_readed(stream, nread, 0);
			}
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

	if (carg->write_buffer.base) {
		free(carg->write_buffer.base);
		carg->write_buffer.base = 0;
	}

	if(status != 0) {
		if (req)
			free(req);
		return;
	}
	if (carg->is_write_error) {
		if (req)
			free(req);
		return;
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
		if (carg->write_buffer.base) {
			free(carg->write_buffer.base);
			carg->write_buffer.base = 0;
		}
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
	carg->connect_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client connected %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, status: %d\n", carg->count, getrtime_now_ms(carg->connect_time_finish), getrtime_sec_float(carg->connect_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);

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
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tcp client connected %p(%p:%p) with key %s, parser name %s, hostname %s, port: %s tls: %d, status: %d\n", carg->count++, getrtime_now_ms(carg->read_time), getrtime_sec_float(carg->read_time, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->parser_name, carg->host, carg->port, carg->tls, status);
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

		if (carg->write_buffer.base)
		{
			free(carg->write_buffer.base);
			carg->write_buffer.base = 0;
		}

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
	uv_timer_stop(timer);
	alligator_cache_push(ac->uv_cache_timer, timer);

	context_arg *carg = timer->data;
	if (!carg)
	{
		return;
	}

	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] timeout tcp client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg->count++, getrtime_now_ms(setrtime()), getrtime_sec_float(setrtime(), carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);
	(carg->timeout_counter)++;

	if (!carg->parsed && carg->full_body->l)
		alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
	tcp_client_close((uv_handle_t *)&carg->client);

	if ((carg->proto == APROTO_HTTP) || (carg->proto == APROTO_HTTPS))
		http_null_metrics(carg);
}

void tcp_client_repeat_period(uv_timer_t *timer);
void tcp_client_connect(void *arg)
{
	context_arg *carg = arg;
	carg->count = 0;

	if (carg->lock)
		return;
	if (cluster_come_later(carg))
		return;

	carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);
	if (carg->period && !carg->close_counter) {
		carg->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->period_timer->data = carg;
		uv_timer_init(carg->loop, carg->period_timer);
		uv_timer_start(carg->period_timer, tcp_client_repeat_period, carg->period, 0);
	}


	carg->lock = 1;
	carg->parsed = 0;
	carg->parser_status = 0;
	carg->is_closing = 0;
	carg->curr_ttl = carg->ttl;

	carg->tt_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
	carg->tt_timer->data = carg;
	uv_timer_init(carg->loop, carg->tt_timer);
	uv_timer_start(carg->tt_timer, tcp_timeout_timer, carg->timeout, 0);

	memset(&carg->connect, 0, sizeof(carg->connect));
	carg->connect.data = carg;

	memset(&carg->client, 0, sizeof(carg->client));
	carg->client.data = carg;
	uv_tcp_init(carg->loop, &carg->client);
	if (carg->tls)
		tls_client_init(carg->loop, carg);

	string *data = aggregator_get_addr(carg, carg->host, DNS_TYPE_A, DNS_CLASS_IN);
	if (!data)
		return;

	uv_ip4_addr(data->s, carg->numport, &carg->dest);

	carg->connect_time = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/0] tcp client connect %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, getrtime_now_ms(carg->connect_time), carg, &carg->client, &carg->connect, carg->key, carg->host, carg->port, carg->tls, carg->lock, carg->timeout);
	if (carg->tls)
	{
		carg->tls_connect_time = setrtime();
		uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)&carg->dest, tls_connected);
	}
	else
		uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)&carg->dest, tcp_connected);
}

void for_tcp_client_connect(void *arg)
{
	context_arg *carg = arg;
	if (carg->period && carg->close_counter)
		return;

	tcp_client_connect(arg);
}

void tcp_client_repeat_period(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (!carg->period)
		return;

	tcp_client_connect((void*)carg);
}

void unix_client_repeat_period(uv_timer_t *timer);
void unix_client_connect(void *arg)
{
	context_arg *carg = arg;
	carg->count = 0;

	if (carg->lock)
		return;
	if (cluster_come_later(carg))
		return;

	if (carg->period && !carg->close_counter) {
		carg->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->period_timer->data = carg;
		uv_timer_init(carg->loop, carg->period_timer);
		uv_timer_start(carg->period_timer, unix_client_repeat_period, carg->period, 0);
	}

	carg->lock = 1;
	carg->parsed = 0;
	carg->parser_status = 0;
	carg->is_closing = 0;
	carg->curr_ttl = carg->ttl;

	carg->tt_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
	carg->tt_timer->data = carg;
	uv_timer_init(carg->loop, carg->tt_timer);
	uv_timer_start(carg->tt_timer, tcp_timeout_timer, carg->timeout, 0);

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
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/0] unix client connect %p(%p:%p) with key %s, hostname %s,  tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, getrtime_now_ms(carg->connect_time), carg, &carg->client, &carg->connect, carg->key, carg->host, carg->tls, carg->lock, carg->timeout);
	if (carg->tls)
	{
		carg->tls_connect_time = setrtime();
		uv_pipe_connect(&carg->connect, (uv_pipe_t *)&carg->client, carg->host, tls_connected);
	}
	else
		uv_pipe_connect(&carg->connect, (uv_pipe_t *)&carg->client, carg->host, tcp_connected);
}

void for_unix_client_connect(void *arg)
{
	context_arg *carg = arg;
	if (carg->period && carg->close_counter)
		return;

	unix_client_connect(arg);
}

void unix_client_repeat_period(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (!carg->period)
		return;

	unix_client_connect((void*)carg);
}

static void tcp_client_crawl(uv_timer_t* handle) {
	(void)handle;
	alligator_ht_foreach(ac->aggregator, for_tcp_client_connect);
	alligator_ht_foreach(ac->uggregator, for_unix_client_connect);
}

char* tcp_client(void *arg)
{
	if (!arg)
		return NULL;

	context_arg *carg = arg;

	alligator_ht_insert(ac->aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	aggregator_get_addr(carg, carg->host, DNS_TYPE_A, DNS_CLASS_IN);
	return "tcp";
}

char* unix_tcp_client(context_arg* carg)
{
	if (!carg)
		return NULL;

	alligator_ht_insert(ac->uggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "unix";
}

void tcp_client_del(context_arg *carg)
{
	if (!carg)
		return;


	if (carg)
	{
		if (carg->remove_from_hash)
			alligator_ht_remove_existing(ac->aggregators, &(carg->context_node));
		
		if (carg->lock)
		{
			r_time time = setrtime();
			carg->context_ttl = time.sec;
			tcp_client_close((uv_handle_t *)&carg->client);
		}
		else
		{
			carg->lock = 1;

			alligator_ht_remove_existing(ac->aggregator, &(carg->node));
			carg_free(carg);
		}
	}
}

void unix_tcp_client_del(context_arg *carg)
{
	if (!carg)
		return;

	if (carg)
	{
		if (carg->remove_from_hash)
			alligator_ht_remove_existing(ac->uggregator, &(carg->context_node));
		
		if (carg->lock)
		{
			r_time time = setrtime();
			carg->context_ttl = time.sec;
			tcp_client_close((uv_handle_t *)&carg->client);
		}
		else
		{
			carg->lock = 1;
			alligator_ht_remove_existing(ac->uggregator, &(carg->node));
			carg_free(carg);
		}
	}
}

void tcp_client_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->tcp_client_timer);
	uv_timer_start(&ac->tcp_client_timer, tcp_client_crawl, ac->aggregator_startup, ac->aggregator_repeat);
}
