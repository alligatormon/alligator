#include "context_arg.h"
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
#include "events/tls.h"
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
		tls_client_cleanup(carg, 1);
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
	else {

		if (carg->period)
			uv_timer_set_repeat(carg->period_timer, carg->period);
	}
}


void tcp_client_close(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	carg->close_time = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client call close %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, getrtime_now_ms(carg->close_time), getrtime_sec_float(carg->close_time, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	carg->tt_timer->data = NULL;

	if (!uv_is_closing((uv_handle_t*)&carg->client))
	{
		carg->close_time = setrtime();
		if (carg->tls)
		{
			int ret = SSL_get_shutdown(carg->ssl);
			if (!ret)
			{
			    do_tls_shutdown(carg->ssl);
			}
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
	context_arg* carg = req->data;
	carg->shutdown_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tcp client shutdowned %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d\n", carg->count++, getrtime_now_ms(carg->shutdown_time_finish), getrtime_sec_float(carg->shutdown_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	(carg->shutdown_counter)++;

	tcp_client_close((uv_handle_t *)&carg->client);
	free(req);
}

void tcp_client_read_data(uv_stream_t* stream, ssize_t nread, char *base)
{
	context_arg* carg = (context_arg*)stream->data;

	carg->read_time_finish = setrtime();
	carglog(carg, L_DEBUG, "%"u64": [%"PRIu64"/%lf] tcp client read %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, getrtime_now_ms(carg->read_time_finish), getrtime_sec_float(carg->read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread);
	
	(carg->read_counter)++;

	if (nread > 0)
	{
		uint64_t chunksize = 0;
		if (base)
			base[nread] = 0;
		carglog(carg, L_TRACE, "\n==================BASE===================\n'%s'\n======\n", base? base : "");
		carg->read_bytes_counter += nread;
		if (base)
		{
			if (!carg->full_body->l)
			{
				http_reply_data* hr_data = http_reply_parser(base, nread);
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
					string_cat(carg->full_body, base, nread);
				}
			}
			else if (carg->chunked_expect) // maybe chunked_expect?
			{
				chunksize = chunk_calc(carg, base, nread, 1);
			}
			else
			{
				string_cat(carg->full_body, base, nread);
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

void tcp_client_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
	tcp_client_read_data(stream, nread, buf ? buf->base : NULL);
}

void client_tcp_write_cb(uv_write_t* req, int status) {
	context_arg *carg = req->data;
	if (status) {
		carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] client data written %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d, write error: %s\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status, uv_strerror(status));
	}
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] client data written %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	free(carg->write_buffer.base);
	carg->write_buffer.len = 0;
	carg->write_buffer.base = 0;
}

int do_client_tls_handshake(context_arg *carg) {
	if (!carg->tls_handshake_done) {
		int r = SSL_do_handshake(carg->ssl);
		if (r == 1) {
			carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] handshake complete %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls);
			carg->tls_handshake_done = 1;
			X509 *cert = SSL_get_peer_certificate(carg->ssl);
			if (cert) {
				x509_parse_cert(carg, cert, carg->host, carg->host);
				X509_free(cert);
			}

			return 1;
		}
		else if (r < 1) {
			int err = SSL_get_error(carg->ssl, r);
			if (err == SSL_ERROR_WANT_READ) {
			} else if (err == SSL_ERROR_WANT_WRITE) {
			} else {
				char *err = openssl_get_error_string();
				carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] handshake receive failed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, error: %s\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, err);
				free(err);

				return -1;
			}
		}

		size_t bio_read;
		carg->write_buffer = uv_buf_init(calloc(1, EVENT_BUFFER), EVENT_BUFFER);
		r = BIO_read_ex(carg->wbio, carg->write_buffer.base, EVENT_BUFFER, &bio_read);
		carg->write_buffer.len = bio_read;
		uv_write(&carg->write_req, carg->connect.handle, &carg->write_buffer, 1, client_tcp_write_cb);

		int err = SSL_get_error(carg->ssl, r);
		if (err == SSL_ERROR_WANT_READ) {
		} else if (err == SSL_ERROR_WANT_WRITE) {
		} else {
			char *err = openssl_get_error_string();
			carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] handshake send failed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, error: %s\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, err);
			free(err);

			return -1;
		}

	}
	return 0;
}


void tls_client_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = stream->data;
	carg->tls_read_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client read %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread);
	(carg->tls_read_counter)++;

	if (nread <= 0)
	{
		tcp_client_read(stream, nread, 0);
		return;
	}

	carg->tls_read_bytes_counter += nread;

	BIO_write(carg->rbio, buf->base, nread);
	int handshaked = do_client_tls_handshake(carg);
	if (handshaked > 0) {
		//tls_write(carg, carg->mesg, carg->mesg_len, client_tcp_write_cb);
		tls_write(carg, carg->connect.handle, carg->request_buffer.base, carg->request_buffer.len, client_tcp_write_cb);
		tcp_connected(&carg->connect, 0);
	} else if (!handshaked) {
		string *buffer = string_new();
		int read_size = 0;
		while ( 1 ) {
			char read_buffer[EVENT_BUFFER] = { 0 };
			read_size = SSL_read(carg->ssl, read_buffer, EVENT_BUFFER - 1);
			if (read_size <= 0) {
				break;
			}
			string_cat(buffer, read_buffer, read_size);
		}
		if (buffer->l > 0) {
			//tcp_client_read_data(stream, read_size, read_buffer);
			tcp_client_read_data(stream, buffer->l, buffer->s);
			string_free(buffer);
		}
		else {
			int err = SSL_get_error(carg->ssl, read_size);
			int need_shutdown = tls_io_check_shutdown_need(carg, err, read_size);
			if (need_shutdown == 1) {
				do_tls_shutdown(carg->ssl);
				uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
				req->data = carg;
				uv_shutdown(req, (uv_stream_t*)&carg->client, tcp_client_shutdown);
				tcp_client_close((uv_handle_t *)&carg->client);
			} else if (need_shutdown == -1) {
				uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
				req->data = carg;
				uv_shutdown(req, (uv_stream_t*)&carg->client, tcp_client_shutdown);
				tcp_client_close((uv_handle_t *)&carg->client);
			}
		}
	}
	else {
		uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
		req->data = carg;
		uv_shutdown(req, (uv_stream_t*)&carg->client, tcp_client_shutdown);
		tcp_client_close((uv_handle_t *)&carg->client);
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

void tls_connected(uv_connect_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;
	carg->connect_time_finish = setrtime();
	carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client connected %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, status: %d\n", carg->count, getrtime_now_ms(carg->connect_time_finish), getrtime_sec_float(carg->connect_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);

	if (status < 0) {
		tcp_client_closed((uv_handle_t*)&carg->client);
		return;
	}

	carg->tls_read_time = setrtime();
	uv_read_start((uv_stream_t*)&carg->client, tls_client_alloc, tls_client_read);

	tcp_connected(&carg->connect,  1);
	do_client_tls_handshake(carg);
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
		uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, tcp_client_read);
	}

	memset(&carg->write_req, 0, sizeof(carg->write_req));
	carg->write_req.data = carg;

	if(carg->tls)
	{
		carg->tls_connect_time_finish = setrtime();
	}
	else
	{
		carglog(carg, L_TRACE, "\n==================WRITEBASE(plain:%"PRIu64")===================\n'%s'\n======\n", carg->write_buffer.len, carg->request_buffer.base ? carg->request_buffer.base : "");
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
		tls_context_init(carg, SSLMODE_CLIENT, carg->tls_verify, carg->tls_ca_file, carg->tls_cert_file, carg->tls_key_file, carg->host, NULL);

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
		tls_context_init(carg, SSLMODE_CLIENT, carg->tls_verify, carg->tls_ca_file, carg->tls_cert_file, carg->tls_key_file, carg->host, NULL);

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
