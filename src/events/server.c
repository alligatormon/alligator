#include <stdlib.h>
#include <uv.h>
#include "context_arg.h"
#include "events/debug.h"
#include "events/uv_alloc.h"
#include "common/entrypoint.h"
#include "parsers/http_proto.h"
#include "parsers/multiparser.h"
#include "events/metrics.h"
#include "events/access.h"
#include "common/logs.h"
#include "events/tls.h"
#include "main.h"
extern aconf *ac;

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

	if (!carg->body_read && carg->full_body)
		alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

	if (carg->tls)
		tls_client_cleanup(carg, 0);

	if (carg->full_body)
	{
		carg->buffer_request_size = carg->full_body->m;
		string_free(carg->full_body);
	}
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
			if (carg->tls_handshake_done) {
				int ret = SSL_get_shutdown(carg->ssl);
				if (!ret)
				{
					do_tls_shutdown(carg->ssl);
				}
			}
			uv_close((uv_handle_t*)&carg->client, tcp_server_closed_client);
		}
		else
		{
			uv_close((uv_handle_t*)&carg->client, tcp_server_closed_client);
		}
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



void tcp_server_written(uv_write_t* req, int status)
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


void tls_server_written(uv_write_t* req, int status) {
	context_arg *carg = req->data;
	if (status)
		carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] server data written %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d, write error: %s\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status, uv_strerror(status));
	else
		carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] server data written %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	free(carg->write_buffer.base);
	carg->write_buffer.len = 0;
	carg->write_buffer.base = 0;
}

void tls_server_written_nofree(uv_write_t* req, int status) {
	context_arg *carg = req->data;
	if (status)
		carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] server data written %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d, write error: %s\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status, uv_strerror(status));
	else
		carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] server data written %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	carg->write_buffer.len = 0;
	carg->write_buffer.base = 0;
}

void server_tcp_create_tls_client(context_arg *carg) {
	carg->ssl = SSL_new(carg->ssl_ctx);
	SSL_set_accept_state(carg->ssl);
	carg->rbio = BIO_new(BIO_s_mem());
	carg->wbio = BIO_new(BIO_s_mem());
	SSL_set_bio(carg->ssl, carg->rbio, carg->wbio);
}

void flush_tls_write(context_arg *carg, void *cb) {
	char buffer[EVENT_BUFFER];
	int bytes;
	while ((bytes = BIO_read(carg->wbio, buffer, sizeof(buffer))) > 0) {
		char *out = malloc(bytes);
		memcpy(out, buffer, bytes);
		carg->write_req.data = carg;
		carg->write_buffer = uv_buf_init(out, bytes);
		uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->write_buffer, 1, cb);
		free(out);
	}
}

int do_tls_handshake_server(context_arg *carg) {
	if (!carg->tls_handshake_done) {
		int r = SSL_accept(carg->ssl);

		if (r == 1) {
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
				flush_tls_write(carg, tls_server_written);
			} else if (err == SSL_ERROR_WANT_WRITE) {
				flush_tls_write(carg, tls_server_written);
			} else {
				char *err = openssl_get_error_string();
				carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] handshake receive failed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, error: %s\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, err);
				free(err);

				return 0;
			}
		}

		size_t bio_read = 0;
		carg->write_buffer = uv_buf_init(calloc(1, EVENT_BUFFER), EVENT_BUFFER);
		carg->write_req.data = carg;
		BIO_read_ex(carg->wbio, carg->write_buffer.base, EVENT_BUFFER, &bio_read);
		carg->write_buffer.len = bio_read;

		if (bio_read > 0) {
			uv_write(&carg->write_req, (uv_stream_t *)&carg->client, &carg->write_buffer, 1, tls_server_written);
		}

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

void tcp_server_read_data(uv_stream_t* stream, ssize_t nread, char *base)
{
	context_arg* carg = (context_arg*)stream->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tcp server read %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, nread: %zd, EOF: %d, ECONNRESET: %d, ECONNABORTED: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread, (nread == UV_EOF), (nread == UV_ECONNRESET), (nread == UV_ECONNABORTED));
	(srv_carg->read_counter)++;
	carg->read_time_finish = setrtime();

	if (nread == 0)
		return;
	else if (nread == UV_EOF)
	{
		carg->shutdown_req.data = carg;
		carg->shutdown_time = setrtime();
		uv_shutdown(&carg->shutdown_req, (uv_stream_t*)&carg->client, tcp_server_shutdown_client);
		return;
	}
	else if (nread == UV_ECONNRESET || nread == UV_ECONNABORTED)
	{
		tcp_server_close_client(carg);
		return;
	}

	http_reply_data* hrdata;
	if (!carg->full_body || !carg->full_body->l)
	{
		if (nread > 0) {
			hrdata = http_proto_get_request_data(base, nread, carg->auth_header);
			if (hrdata)
			{
				carg->expect_body_length = hrdata->content_length + hrdata->headers_size;
				carg->body = hrdata->body;
				if (carg->body)
					carg->body_read = 1;
				else
					carg->body_read = 0;

				// initial big size of answer only for GET method
				if (!carg->full_body && hrdata->method == HTTP_METHOD_GET)
					carg->full_body = string_init(carg->buffer_request_size);

				http_reply_data_free(hrdata);
			}
		}
	}

	if (!carg->body_read)
	{
		char *body = strstr(base, "\r\n\r\n");
		if (!body)
			body = strstr(base, "\n\n");

		if (body)
			carg->body_read = 1;
	}

	if (!carg->full_body)
		carg->full_body = string_init(1024);

	if ((nread) > 0 && (nread < 65536) && carg->expect_body_length <= (nread + carg->full_body->l) && carg->body_read)
	{
		srv_carg->read_bytes_counter += nread;
		string *str = string_init(carg->buffer_response_size);

		if (base)
			string_cat(carg->full_body, base, nread);

		if (carg->body)
		{
			carg->body = strstr(carg->full_body->s, "\r\n\r\n");
			if (!carg->body)
				carg->body = strstr(carg->full_body->s, "\n\n");
		}

		char *pastr = carg->full_body->l ? carg->full_body->s : base;
		uint64_t paslen = carg->full_body->l ? carg->full_body->l : nread;

		alligator_multiparser(pastr, paslen, carg->parser_handler, str, carg);

		carg->write_req.data = carg;
		if (uv_is_writable((uv_stream_t*)&carg->client))
		{
			srv_carg->write_bytes_counter += str->l;
			carg->write_time = setrtime();
			if(!carg->tls)
			{
				carg->response_buffer = uv_buf_init(str->s, str->l);
				uv_write(&carg->write_req, (uv_stream_t*)&carg->client, (const struct uv_buf_t *)&carg->response_buffer, 1, tcp_server_written);
			}
			else {
				tls_write(carg, (uv_stream_t*)&carg->client, str->s, str->l, tls_server_written_nofree);
				free(str->s);
			}
		}
		string_null(carg->full_body);
		carg->buffer_response_size = str->m;
		free(str);
	}
	else if (nread >= 65536)
	{
		srv_carg->read_bytes_counter += nread;
		string_cat(carg->full_body, base, nread);
	}
	else if (carg->expect_body_length > (nread + carg->full_body->l))
	{
		srv_carg->read_bytes_counter += nread;
		string_cat(carg->full_body, base, nread);
	}
	else
		string_cat(carg->full_body, base, nread);
}

void tcp_server_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
	tcp_server_read_data(stream, nread, buf ? buf->base : NULL);
}

void tls_server_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = stream->data;
	context_arg *srv_carg = carg->srv_carg;
	carglog(carg, L_INFO, "%"u64": tls server read %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, nread: %zd, is_closing: %d, handshake over: %d\n", carg->count++, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread, uv_is_closing((uv_handle_t*)&carg->client), carg->tls_handshake_done);
	(srv_carg->tls_read_counter)++;
	carg->tls_read_time_finish = setrtime();

	if (nread <= 0)
	{
		tcp_server_read(stream, nread, 0);
		return;
	}

	carg->tls_read_bytes_counter += nread;

	BIO_write(carg->rbio, buf->base, nread);
	int handshaked = do_tls_handshake_server(carg);
	if (handshaked >= 0) {
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
			tcp_server_read_data(stream, buffer->l, buffer->s);
		}
		else {
			int err = SSL_get_error(carg->ssl, read_size);
			int need_shutdown = tls_io_check_shutdown_need(carg, err, read_size);
			if (need_shutdown == 1) {
				do_tls_shutdown(carg->ssl);
				uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
				req->data = carg;
				uv_shutdown(req, (uv_stream_t*)&carg->client, tcp_server_shutdown_client);
				tcp_server_close_client(carg);
			} else if (need_shutdown == -1) {
				uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
				req->data = carg;
				uv_shutdown(req, (uv_stream_t*)&carg->client, tcp_server_shutdown_client);
				tcp_server_close_client(carg);
			}
		}
		string_free(buffer);
	}
	else {
		uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
		req->data = carg;
		uv_shutdown(req, (uv_stream_t*)&carg->client, tcp_server_shutdown_client);
		tcp_server_close_client(carg);
	}
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

void tcp_server_connected(uv_stream_t* stream, int status) 
{
	context_arg* srv_carg = stream->data;

	if (status != 0)
		return;

	context_arg *carg = malloc(sizeof(*carg));
	memcpy(carg, srv_carg, sizeof(context_arg));
	carglog(carg, L_INFO, "%"u64": tcp server accepted on server %p, context %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, srv_carg, carg, &carg->server, &carg->client, carg->key, carg->host, carg->port, carg->tls);
	(srv_carg->conn_counter)++;

	uv_tcp_init(carg->loop, &carg->client);

	carg->srv_carg = srv_carg;
	carg->client.data = carg;
	carg->curr_ttl = carg->ttl;

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
		server_tcp_create_tls_client(carg);
		//do_tls_handshake_server(carg);

		if (uv_read_start((uv_stream_t*)&carg->client, tls_server_alloc, tls_server_read))
			tcp_server_close_client(carg);
	}
	else
	{
		if (uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, tcp_server_read))
			tcp_server_close_client(carg);
	}
}

void tcp_server_run(void *passarg) {
	context_arg *srv_carg = passarg;
	struct sockaddr_in addr;

	if (srv_carg->threads) {
		srv_carg->loop = malloc(sizeof *srv_carg->loop);
		srv_carg->loop_allocated = 1;
		uv_loop_init(srv_carg->loop); // TODO: need to be freed in carg_free
	} else {
		srv_carg->loop = uv_default_loop();
	}

	if (srv_carg->tls) {
		int rc = tls_context_init(srv_carg, SSLMODE_SERVER, srv_carg->tls_verify, srv_carg->tls_ca_file, srv_carg->tls_cert_file, srv_carg->tls_key_file, NULL, NULL);
		if (!rc) {
			srv_carg->running = -1;
		}
	}

	if(uv_ip4_addr(srv_carg->host, srv_carg->numport, &addr))
	{
		srv_carg->running = -1;
	}


	// after update libuv to versions 1.49.2+ remove this block to the UV_TCP_REUSEPORT flag in uv_tcp_bind
	uv_tcp_init_ex(srv_carg->loop, &srv_carg->server, AF_INET);
	uv_os_fd_t fd;
	int on = 1;
	uv_fileno((const uv_handle_t *)&srv_carg->server, &fd);
	setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
	//

	if(uv_tcp_bind(&srv_carg->server, (const struct sockaddr*)&addr, 0))
	{
		srv_carg->running = -1;
	}

	if (uv_tcp_nodelay(&srv_carg->server, 1))
	{
		srv_carg->running = -1;
	}

	int ret = uv_listen((uv_stream_t*)&srv_carg->server, 1024, tcp_server_connected);
	if (ret)
	{
		carglog(srv_carg, L_FATAL, "Listen '%s:%d' error %s\n", srv_carg->host, srv_carg->numport, uv_strerror(ret));
		srv_carg->running = -1;
	}

	srv_carg->running = 1;
	if (srv_carg->threads)
		uv_run(srv_carg->loop, UV_RUN_DEFAULT);
}

int tcp_server_init(uv_loop_t *loop, const char* ip, int port, uint8_t tls, context_arg *import_carg)
{
	for (uint64_t i = 0; i < import_carg->threads; ++i) {
		context_arg* srv_carg = NULL;
		if (import_carg) {
			srv_carg = carg_copy(import_carg);
		} else {
			srv_carg = calloc(1, sizeof(context_arg));
		}

		srv_carg->key = malloc(255);
		srv_carg->numport = port;
		strlcpy(srv_carg->host, ip, HOSTHEADER_SIZE);

		srv_carg->tls = tls;
		srv_carg->server.data = srv_carg;

		snprintf(srv_carg->key, 255, "tcp:%"PRIu64":%s:%u", i, srv_carg->host, port);
		carglog(srv_carg, L_INFO, "init server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", loop, tls, srv_carg, srv_carg->host, port);

		uv_thread_create(&srv_carg->thread, tcp_server_run, srv_carg);
		alligator_ht_insert(ac->entrypoints, &(srv_carg->context_node), srv_carg, tommy_strhash_u32(0, srv_carg->key));
	}
	if (!import_carg->threads) {
		context_arg* srv_carg = NULL;
		if (import_carg) {
			srv_carg = carg_copy(import_carg);
		} else {
			srv_carg = calloc(1, sizeof(context_arg));
		}

		srv_carg->key = malloc(255);
		srv_carg->numport = port;
		strlcpy(srv_carg->host, ip, HOSTHEADER_SIZE);

		srv_carg->tls = tls;
		srv_carg->server.data = srv_carg;

		snprintf(srv_carg->key, 255, "tcp:0:%s:%u", srv_carg->host, port);
		carglog(srv_carg, L_INFO, "init server with loop %p and ssl:%d and carg server: %p and ip:%s and port %d\n", loop, tls, srv_carg, srv_carg->host, port);

		uv_thread_create(&srv_carg->thread, tcp_server_run, srv_carg);
		alligator_ht_insert(ac->entrypoints, &(srv_carg->context_node), srv_carg, tommy_strhash_u32(0, srv_carg->key));
	}
	return 1;
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
