#include <stdlib.h>
#include <string.h>
#include "client3.h"
#include "sclient3.h"
#include "events/context_arg.h"
#include "events/uv_alloc.h"
#include "mbedtls/certs.h"
#include "mbedtls/debug.h"

//static void uvhttp_ssl_client_close_cb(
//	uv_handle_t* handle
//	);
static void uvhttp_ssl_client_close_cb(uv_handle_t* handle)
{
	puts("uvhttp_ssl_client_close_cb:1");

	//struct uvhttp_ssl_client* ssl = (struct uvhttp_ssl_client*)handle;
	context_arg *carg = handle->data;
	puts("uvhttp_ssl_client_close_cb:2");
	struct uvhttp_ssl_client* ssl = carg->tls_ctx; //(struct uvhttp_ssl_client*)handle;
	printf("carg %p, ssl %p, user_close_cb %p\n", carg, carg->tls_ctx, ssl->user_close_cb);
	puts("uvhttp_ssl_client_close_cb:3");

	mbedtls_x509_crt_free( &ssl->cacert );
	puts("uvhttp_ssl_client_close_cb:4");
	mbedtls_ssl_free( &ssl->ssl );
	puts("uvhttp_ssl_client_close_cb:5");
	mbedtls_ssl_config_free( &ssl->conf );
	puts("uvhttp_ssl_client_close_cb:6");
	mbedtls_ctr_drbg_free( &ssl->ctr_drbg );
	puts("uvhttp_ssl_client_close_cb:7");
	mbedtls_entropy_free( &ssl->entropy );
	puts("uvhttp_ssl_client_close_cb:8");
	mbedtls_ssl_free( &ssl->ssl );
	puts("uvhttp_ssl_client_close_cb:9");
	printf("client_close: %p\n", ssl->user_close_cb);
	ssl->user_close_cb(handle);
}


static void ssl_connect_user_call( context_arg *carg, int status)
{
	printf("ssl_connect_user_call, status: %d, carg: %p, carg->socket: %p\n", status, carg, carg->socket);
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;
	if ( ssl->user_connnect_cb) {
		uv_read_stop( (uv_stream_t*)carg->socket);
		printf("1req %p\n", ssl->user_connnect_req);
		ssl->user_connnect_cb( ssl->user_connnect_req,  status);
	}
}

static void uvhttp_ssl_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	printf("uvhttp_ssl_read_cb: %lld\n'%s'", nread, buf->base);
	context_arg *carg = stream->data;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;
	if ( nread <= 0) {
		goto cleanup;
	}

	printf("DEBUG: ssl->is_closing %d, ssl->ssl.state %d!=%d\n", ssl->is_closing, ssl->ssl.state,MBEDTLS_SSL_HANDSHAKE_OVER);
	if ( ssl->is_closing) {
		puts("closing");
		if ( uv_is_closing( (uv_handle_t*)ssl) == 0) {
			uv_close( (uv_handle_t*)stream, uvhttp_ssl_client_close_cb);
		}
	}
	else if ( ssl->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER) {
		puts("handshake");
		int ret = 0;
		ssl->ssl_read_buffer_len = nread;
		ssl->ssl_read_buffer_offset = 0;
		while ( (ret = mbedtls_ssl_handshake_step( &ssl->ssl )) == 0) {
			printf("2. mbedtls_ssl_handshake_step: %d\n", ret);
			if ( ssl->ssl.state == MBEDTLS_SSL_HANDSHAKE_OVER){
				puts("ssl connect user call 0");
				ssl_connect_user_call(carg, 0);
				break;
			}
		}
		if ( ssl->ssl_read_buffer_offset != nread) {
			nread = -1;
			goto cleanup;
		}
		ssl->ssl_read_buffer_len = 0;
		ssl->ssl_read_buffer_offset = 0;
		if ( ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			nread = -1;
			goto cleanup;
		}
	}
	else {
		puts("else");
		int read_len = 0;
		ssl->ssl_read_buffer_len = nread;
		ssl->ssl_read_buffer_offset = 0;
 
		while((read_len = mbedtls_ssl_read( &ssl->ssl, 
			(unsigned char *)ssl->user_read_buf.base,  ssl->user_read_buf.len)) > 0) {
			ssl->user_read_cb( stream, read_len, &ssl->user_read_buf);
		}
		if ( read_len !=0 && read_len != MBEDTLS_ERR_SSL_WANT_READ) {
			if ( MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY == read_len) {
				nread = UV_ECONNABORTED;
				goto cleanup;
			}
			else {
				nread = -1;
				goto cleanup;
			}
		}
	}
cleanup:
	if ( nread <= 0) {
		if ( ssl->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER) {
			puts("DEBUG: -1");
			ssl_connect_user_call(carg, -1);
		}
		else {
			puts("DEBUG: 0");
			ssl->user_read_cb( stream, nread, 0);
		}
	}
}

static void uvhttp_ssl_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	puts("uvhttp_ssl_alloc_cb");
	context_arg *carg = handle->data;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;

	ssl->user_read_buf.base = 0;
	ssl->user_read_buf.len = 0;
	if ( ssl->user_alloc_cb)
	{
		ssl->user_alloc_cb( handle, suggested_size, &ssl->user_read_buf);
	}
	buf->base = ssl->ssl_read_buffer;
	buf->len = sizeof( ssl->ssl_read_buffer);
}

static int ssl_recv(void *ctx, unsigned char *buf, size_t len)
{
	puts("ssl_recv");

	context_arg *carg = ctx;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;

	int need_copy = ((ssl->ssl_read_buffer_len - ssl->ssl_read_buffer_offset) < len ? (ssl->ssl_read_buffer_len - ssl->ssl_read_buffer_offset) : len);
	if ( need_copy == 0) {
		need_copy = MBEDTLS_ERR_SSL_WANT_READ;
		goto cleanup;
	}
	memcpy( buf, ssl->ssl_read_buffer + ssl->ssl_read_buffer_offset, need_copy);
	ssl->ssl_read_buffer_offset += need_copy;
cleanup:
	return need_copy ;
}

static void ssl_write_user_call(struct uvhttp_ssl_client* ssl, int status)
{
	puts("ssl_write_user_call");
	ssl->is_writing = 0;
	if ( ssl->user_write_cb) {
		ssl->user_write_cb( ssl->user_write_req,  status);
	}
}

static void ssl_write_cb(uv_write_t* req, int status)
{
	puts("ssl_write_cb");
	int ret = 0;
	context_arg *carg = req->data;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;

	if( status != 0) {
		ret = UVHTTP_ERROR_FAILED;
		goto cleanup;
	}
	if ( ssl->is_write_error ) {
		ret = UVHTTP_ERROR_FAILED;
		goto cleanup;
	}
	if ( ssl->write_buffer.base) {
		free( ssl->write_buffer.base);
		ssl->write_buffer.base = 0;
	}
	if ( ssl->is_closing) {
		if ( uv_is_closing((uv_handle_t *)carg->socket) == 0) {
			uv_close((uv_handle_t *)carg->socket, uvhttp_ssl_client_close_cb);
		}
	}
	else if ( ssl->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER ) {
		while ( (ret = mbedtls_ssl_handshake_step( &ssl->ssl )) == 0) {
			printf("3. mbedtls_ssl_handshake_step: %d\n", ret);
			if ( ssl->ssl.state == MBEDTLS_SSL_HANDSHAKE_OVER){
				break;
			}
		}
		if ( ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
			ret = UVHTTP_ERROR_FAILED;
			goto cleanup;
		}
		ret = 0;
	}
	else {
		ret = mbedtls_ssl_write( &ssl->ssl,
			(const unsigned char *)ssl->ssl_write_buffer + ssl->ssl_write_offset, 
			ssl->ssl_write_buffer_len - ssl->ssl_write_offset
			);
		if ( ret == MBEDTLS_ERR_SSL_WANT_WRITE ) {
			ret = UVHTTP_ERROR_FAILED;
			goto cleanup;
		}
		else if ( ret > 0 ) {
			ssl->ssl_write_offset += ret;
			if ( ssl->ssl_write_offset == ssl->ssl_write_buffer_len) {
				ssl_write_user_call( ssl,  0);
			}
			ret = 0;
		}
		else {
			ret = UVHTTP_ERROR_FAILED;
			goto cleanup;
		}
	}
cleanup:
	if( ret != 0) {
		if ( ssl->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER) {
			ssl_connect_user_call(carg, -1);
		}
		else {
			if ( !ssl->is_write_error ) {
				ssl_write_user_call( ssl, UVHTTP_ERROR_FAILED);
				ssl->is_write_error = 1;
			}
		}
	}
	if ( ssl->write_buffer.base) {
		free( ssl->write_buffer.base);
		ssl->write_buffer.base = 0;
	}
	if ( req) {
		free( req);
	}
}

static int ssl_send(void *ctx, const unsigned char *buf, size_t len)
{
	puts("====ssl_send");
	context_arg *carg = ctx;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;

	uv_write_t* write_req = 0;
	int ret = 0;
	ssl->write_buffer.base = 0;
	ssl->write_buffer.len = 0;
	if ( ssl->is_write_error ) {
		puts("write error");
		return UVHTTP_ERROR_FAILED;
	}
	if ( ssl->is_async_writing == 0 ) {
		puts("async writing");
		ssl->write_buffer.base = (char*)malloc( len );
		memcpy( ssl->write_buffer.base, buf, len);
		ssl->write_buffer.len = len;
		write_req = (uv_write_t*)malloc(sizeof(uv_write_t));
		write_req->data = carg;
		printf("trying to write %p:'%s':(%zu):1:%p\n", write_req, ssl->write_buffer.base, ssl->write_buffer.len, ssl_write_cb);
		ret = uv_write( write_req, (uv_stream_t*)carg->socket, &ssl->write_buffer, 1, ssl_write_cb);
		if ( ret != 0) {
			goto cleanup;
		}
		len = MBEDTLS_ERR_SSL_WANT_WRITE;
		ssl->is_async_writing = 1;
	}
	else {
		puts("disable async wirting");
		ssl->is_async_writing = 0;
	}
cleanup:
	if ( ret != 0) {
		len = UVHTTP_ERROR_FAILED;
		ssl->is_write_error = 1;
		if ( ssl->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER) {
			puts("CLEANUP");
			ssl_connect_user_call( carg, -1);
		}
		else {
			ssl_write_user_call( ssl, UVHTTP_ERROR_FAILED);
		}
		if ( write_req) {
			free( write_req);
		}
		if ( ssl->write_buffer.base) {
			free( ssl->write_buffer.base);
			ssl->write_buffer.base = 0;
		}
	}
	return len;
}

static void my_debug(void *ctx, int level, const char *file, int line, const char *str)
{
	puts("my_debug");
	const char *p, *basename;

	for( p = basename = file; *p != '\0'; p++ )
		if( *p == '/' || *p == '\\' )
			basename = p + 1;

	mbedtls_fprintf( (FILE *) ctx, "%s:%04d: |%d| %s", basename, line, level, str );
	fflush(  (FILE *) ctx  );
}

int uvhttp_ssl_client_init(context_arg *carg)
{
	puts("uvhttp_ssl_client_init");
	 int ret = 0;

	if (!carg->tls_ctx)
	{
		carg->tls_ctx = malloc(sizeof(struct uvhttp_ssl_client));
	}
	// struct uvhttp_ssl_client* ssl = carg->tls_ctx = calloc(1, sizeof(*ssl));
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;
	bzero(ssl, 0);

	 mbedtls_ssl_init( &ssl->ssl );
	 mbedtls_ssl_config_init( &ssl->conf );
	 mbedtls_x509_crt_init( &ssl->cacert );
	 mbedtls_ctr_drbg_init( &ssl->ctr_drbg );
	 mbedtls_entropy_init( &ssl->entropy );
 
	 if( ( ret = mbedtls_ctr_drbg_seed( &ssl->ctr_drbg, mbedtls_entropy_func, &ssl->entropy,
		 (const unsigned char *) "UVHTTP",
		 sizeof( "UVHTTP" ) -1) ) != 0 ) {
			 goto cleanup;
	 }
 
	 ret = mbedtls_x509_crt_parse( &ssl->cacert, (const unsigned char *) mbedtls_test_cas_pem,
		 mbedtls_test_cas_pem_len );
	 if( ret != 0 ) {
		 goto cleanup;
	 }
 
	 if( ( ret = mbedtls_ssl_config_defaults( &ssl->conf,
		 MBEDTLS_SSL_IS_CLIENT,
		 MBEDTLS_SSL_TRANSPORT_STREAM,
		 MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 ) {
			 goto cleanup;
	 }
 
	 mbedtls_ssl_conf_authmode( &ssl->conf, MBEDTLS_SSL_VERIFY_OPTIONAL );
	 mbedtls_ssl_conf_ca_chain( &ssl->conf, &ssl->cacert, NULL );
	 mbedtls_ssl_conf_rng( &ssl->conf, mbedtls_ctr_drbg_random, &ssl->ctr_drbg );
 
	 mbedtls_ssl_conf_dbg( &ssl->conf, my_debug, stdout );
	 mbedtls_debug_set_threshold( 1 );
	 if( ( ret = mbedtls_ssl_setup( &ssl->ssl, &ssl->conf ) ) != 0 ) {
		 goto cleanup;
	 }
	 if( ( ret = mbedtls_ssl_set_hostname( &ssl->ssl, "UVHttp Client" ) ) != 0 ) {
		 goto cleanup;
	 }

	 mbedtls_ssl_set_bio( &ssl->ssl, carg, ssl_send, ssl_recv, NULL );

	 //ret = uv_tcp_init( loop, (uv_tcp_t*)handle->socket);
	 //if ( ret != 0) {
	 //        goto cleanup;
	 //}
 cleanup:
	 return ret;
}

int uvhttp_ssl_read_client_start(
	uv_stream_t* stream,
	uv_alloc_cb alloc_cb,
	uv_read_cb read_cb
	)
{
	puts("uvhttp_ssl_read_client_start");
	int ret = 0;

	context_arg *carg = stream->data;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;

	ssl->user_read_cb = read_cb;
	ssl->user_alloc_cb = alloc_cb;
	//ret = uv_read_start( stream, uvhttp_ssl_alloc_cb, uvhttp_ssl_read_cb);
	ret = uv_read_start(stream, uvhttp_ssl_alloc_cb, uvhttp_ssl_read_cb);
	return ret;
}

int uvhttp_ssl_client_write( uv_write_t* req, uv_stream_t* handle, char* buffer, unsigned int buffer_len, uv_write_cb cb)
{
	puts("uvhttp_ssl_client_write");
	int ret = 0;
	context_arg *carg = req->data;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;
	if ( ssl->is_writing || ssl->is_closing ) {
		ret = UVHTTP_ERROR_WRITE_WAIT;
		return ret;
	}
	ssl->is_write_error = 0;
	ssl->user_write_req = req;
	ssl->user_write_cb = cb;
	ssl->ssl_write_buffer_len = buffer_len;
	ssl->ssl_write_buffer = buffer;
	ssl->ssl_read_buffer_len = 0;
	ssl->ssl_read_buffer_offset = 0;
	ssl->ssl_write_offset = 0;
	ssl->is_async_writing = 0;
	if ( mbedtls_ssl_write( &ssl->ssl, (const unsigned char *)buffer, buffer_len ) == MBEDTLS_ERR_SSL_WANT_WRITE )
		ssl->is_writing = 1;

	return ret;
}

void uvhttp_ssl_client_close(
	uv_handle_t* handle, 
	uv_close_cb close_cb
	)
{
	printf("uvhttp_ssl_client_close: %p\n", close_cb);

	context_arg *carg = handle->data;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;

	ssl->user_close_cb = close_cb;
	int ret = 0;
	if ( ssl->is_closing) {
		return;
	}
	ssl->is_closing = 1;
	if ( !ssl->is_writing) {
		ret = mbedtls_ssl_close_notify( &ssl->ssl);
		if ( MBEDTLS_ERR_SSL_WANT_READ != ret && MBEDTLS_ERR_SSL_WANT_WRITE != ret ) {
			if (uv_is_closing((uv_handle_t*)carg->socket) == 0) {
				printf("call uv_close with carg %p, ssl %p and user_close_cb %p\n", carg, ssl, ssl->user_close_cb);
				uv_close((uv_handle_t*)carg->socket, uvhttp_ssl_client_close_cb);
			}
		}
	}
}

static void uvhttp_ssl_client_connected( uv_connect_t* req, int status)
{
	puts("uvhttp_ssl_client_connected");
	int ret = 0;
	context_arg *carg = req->data;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;

	if ( status < 0 ) {
		ret = -1;
		goto cleanup;
	}
	puts("use uv_read_start");
	carg->socket->data = carg;
	ret = uv_read_start( (uv_stream_t*)carg->socket, uvhttp_ssl_alloc_cb, uvhttp_ssl_read_cb);
	if ( ret != 0) {
		goto cleanup;
	}
	while ( (ret = mbedtls_ssl_handshake_step( &ssl->ssl )) == 0) {
		printf("4. mbedtls_ssl_handshake_step: %d\n", ret);
		if ( ssl->ssl.state == MBEDTLS_SSL_HANDSHAKE_OVER){
			break;
		}
	}
	if ( ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
		ret = UVHTTP_ERROR_FAILED;
		goto cleanup;
	}
	ret = 0;
cleanup:
	if ( ret != 0) {
		puts("use ssl_connect_user_call");
		ssl_connect_user_call(carg, -1);
	}
	if ( req )
		free( req);
}

int uvhttp_ssl_client_connect(uv_connect_t* req, uv_tcp_t* handle, const struct sockaddr* addr, uv_connect_cb cb)
{
	puts("uvhttp_ssl_client_connect");
	int ret = 0;
	context_arg *carg = req->data;
	struct uvhttp_ssl_client* ssl = carg->tls_ctx;
	uv_connect_t* connect_req = 0;
	connect_req = (uv_connect_t*)malloc(sizeof(uv_connect_t));
	connect_req->data = carg;
	ssl->user_connnect_cb = cb;
	ssl->user_connnect_req = req;
	printf("2 connect_req %p, 1req %p\n", connect_req, req);
	ret = uv_tcp_connect( connect_req, handle, addr, uvhttp_ssl_client_connected);
	return ret;
}
