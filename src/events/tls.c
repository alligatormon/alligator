#include "events/context_arg.h"
#include "common/logs.h"
#include "common/lcrypto.h"
#include "common/revocation.h"
#include "common/rtime.h"
#include "main.h"

#define carglog_elapsed_ms(carg, when) getrtime_elapsed_ms((carg)->connect_time, (when))
#define carglog_elapsed_sec(carg, when) getrtime_sec_float((when), (carg)->connect_time)

void tls_init()
{
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();
	ERR_load_crypto_strings();

}

int verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
	X509 *cert = X509_STORE_CTX_get_current_cert(ctx);
	int depth = X509_STORE_CTX_get_error_depth(ctx);
	int err = X509_STORE_CTX_get_error(ctx);
	char *subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);

	glog(L_DEBUG, "cert verify depth %d subject: %s\n", depth, subject ? subject : "(null)");
	if (!preverify_ok) {
		glog(L_ERROR, "cert verify failed depth %d subject %s: %s\n",
			depth, subject ? subject : "(null)", X509_verify_cert_error_string(err));
		free(subject);
		return 0;
	}
	free(subject);
	return 1;
}

char *openssl_get_error_string() {
	BIO *bio = BIO_new(BIO_s_mem());
	ERR_print_errors(bio);

	char *buf;
	long len = BIO_get_mem_data(bio, &buf);

	char *result = malloc(len + 1);
	memcpy(result, buf, len);
	result[len] = '\0';

	BIO_free(bio);
	return result;
}

void ssl_info_callback(const SSL *ssl, int where, int ret) {
    const char *str;
    int w = where & ~SSL_ST_MASK;

    if (w & SSL_ST_CONNECT) str = "SSL_connect";
    else if (w & SSL_ST_ACCEPT) str = "SSL_accept";
    else str = "undefined";

    if (where & SSL_CB_LOOP)
        glog(L_TRACE, "%s:%s\n", str, SSL_state_string_long(ssl));
    else if (where & SSL_CB_ALERT)
        glog(L_TRACE, "SSL alert %s: %s\n",
               (where & SSL_CB_READ) ? "read" : "write",
               SSL_alert_desc_string_long(ret));
    else if (where & SSL_CB_EXIT && ret <= 0)
        glog(L_TRACE, "SSL error in %s: %d\n", SSL_state_string_long(ssl), ret);
}

int tls_context_init(context_arg *carg, enum ssl_mode mode, int verify, const char *ca, const char * certfile, const char* keyfile, const char *servername, const char *crl)
{
	carg->ssl_ctx = SSL_CTX_new(TLS_method());
	if (!carg->ssl_ctx) {
		char buf[256];
		strerror_r(errno, buf, sizeof(buf));
		carglog(carg, L_ERROR, "context %p  SSL_new failed: %s\n", carg, buf);
		return 0;
	}

	if (certfile && keyfile) {
		if (SSL_CTX_use_certificate_file(carg->ssl_ctx, certfile, SSL_FILETYPE_PEM) != 1) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "context %p SSL_CTX_use_certificate_file '%s' failed: %s\n", carg, certfile, err);
			free(err);
			return 0;
		}

		if (SSL_CTX_use_PrivateKey_file(carg->ssl_ctx, keyfile, SSL_FILETYPE_PEM) != 1) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "context %p SSL_CTX_use_PrivateKey_file '%s' failed: %s\n", carg, keyfile, err);
			free(err);
			return 0;
		}

		if (SSL_CTX_check_private_key(carg->ssl_ctx) != 1) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "context %p SSL_CTX_check_private_key '%s' failed: %s\n", carg, keyfile, err);
			free(err);
			return 0;
		}
		else
			carglog(carg, L_DEBUG, "TLS certificate and private key loaded and verified: cert:%s, key:%s\n", certfile, keyfile);
	}

	if (ca) {
		if (SSL_CTX_load_verify_locations(carg->ssl_ctx, ca, NULL)) {
			carglog(carg, L_DEBUG, "TLS loaded CA file: %s\n", ca);
		}
		else {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "context %p Failed loading CA file: %s: %s\n", carg, ca, err);
			free(err);
		}
	} else if (verify) {
		if (SSL_CTX_set_default_verify_paths(carg->ssl_ctx)) {
			carglog(carg, L_DEBUG, "TLS loaded default CA verify paths\n");
		} else {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "context %p Failed loading default CA verify paths: %s\n", carg, err);
			free(err);
		}
	}

	if (!crl && carg->rev.crl_file)
		crl = carg->rev.crl_file;
	if (crl && !carg->rev.crl_file) {
		carg->rev.crl_file = strdup(crl);
		carg->rev.crl_enabled = 1;
	}
	if (carg->rev.crl_enabled && carg->rev.crl_file) {
		X509_STORE *store = SSL_CTX_get_cert_store(carg->ssl_ctx);
		if (!revocation_store_apply_crl(store, &carg->rev)) {
			carglog(carg, L_ERROR, "context %p Failed to load CRL file: %s\n", carg, carg->rev.crl_file);
			return 0;
		}
		carglog(carg, L_DEBUG, "TLS loaded CRL file: %s\n", carg->rev.crl_file);
	}

	if (mode == SSLMODE_SERVER && carg->tls_verify_client)
		verify = 1;

	SSL_CTX_set_verify(carg->ssl_ctx, SSL_VERIFY_NONE, NULL);
	if (verify) {
		int vmode = SSL_VERIFY_PEER;
		if (mode == SSLMODE_SERVER && carg->tls_verify_client == REV_VERIFY_CLIENT_REQUIRE)
			vmode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
		SSL_CTX_set_verify(carg->ssl_ctx, vmode, verify_callback);
	}

	if (mode == SSLMODE_CLIENT) {
		carg->ssl = SSL_new(carg->ssl_ctx);
		if (!carg->ssl) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "context %p SSL_new (client) failed: %s\n", carg, err);
			free(err);
			return 0;
		}
	}

	if (servername && mode == SSLMODE_CLIENT) {
		if (!SSL_set_tlsext_host_name(carg->ssl, servername)) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "context %p Failed to set SNI '%s': %s\n", carg, servername, err);
			free(err);
		}
		/* Enforce CN/SAN match during handshake when peer verify is enabled. */
		if (verify && !SSL_set1_host(carg->ssl, servername)) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "context %p Failed to set verify hostname '%s': %s\n", carg, servername, err);
			free(err);
		}
		if (carg->rev.ocsp_stapling)
			SSL_set_tlsext_status_type(carg->ssl, TLSEXT_STATUSTYPE_ocsp);
	}

	SSL_CTX_set_ciphersuites(carg->ssl_ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256");
	SSL_CTX_set_cipher_list(carg->ssl_ctx, "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
								 "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
								 "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256");


	SSL_CTX_set_min_proto_version(carg->ssl_ctx, TLS1_2_VERSION);
	SSL_CTX_set_max_proto_version(carg->ssl_ctx, TLS1_3_VERSION);

	if (mode == SSLMODE_CLIENT) {
	   carg->rbio = BIO_new(BIO_s_mem());
	   carg->wbio = BIO_new(BIO_s_mem());
	   SSL_set_bio(carg->ssl, carg->rbio, carg->wbio);
	   SSL_set_connect_state(carg->ssl);
    }


	if (carg->log_level >= L_TRACE) {
		SSL_CTX_set_info_callback(carg->ssl_ctx, ssl_info_callback);
	}

	return 1;
}

void tls_client_cleanup(context_arg *carg, uint8_t clean_context)
{
	if (carg->ssl) {
		SSL_free(carg->ssl);
		carg->ssl = NULL;
	}

	if (clean_context && carg->ssl_ctx) {
		SSL_CTX_free(carg->ssl_ctx);
		carg->ssl_ctx = NULL;
	}
	carg->tls_handshake_done = 0;
}

int do_tls_shutdown(context_arg *carg, SSL *ssl) {
	carglog(carg, L_DEBUG, "%"u64": [%"PRIu64"/%lf] tls call shutdown %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls);
	int ret = SSL_shutdown(ssl);
	if (ret == 1) {
		return 1;
	} else if (ret == 0) {
		ret = SSL_shutdown(ssl);
		return (ret == 1) ? 1 : -1;
	} else {
		int err = SSL_get_error(ssl, ret);
		carglog(carg, L_ERROR, "%"u64": tls call shutdown %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, error: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, err);
		return -1;
	}
}


typedef struct tls_flush_req {
	uv_write_t req;
	uv_buf_t buf;
} tls_flush_req;

static void tls_flush_written(uv_write_t *req, int status)
{
	(void)status;
	tls_flush_req *fr = (tls_flush_req *)req;
	if (fr->buf.base)
		free(fr->buf.base);
	free(fr);
}

void flush_tls_write(context_arg *carg, uv_stream_t *stream)
{
	char buffer[EVENT_BUFFER];
	int bytes;
	while ((bytes = BIO_read(carg->wbio, buffer, sizeof(buffer))) > 0) {
		tls_flush_req *fr = calloc(1, sizeof(*fr));
		if (!fr)
			return;
		char *out = malloc(bytes);
		if (!out) {
			free(fr);
			return;
		}
		memcpy(out, buffer, bytes);
		fr->req.data = carg;
		fr->buf = uv_buf_init(out, bytes);
		if (uv_write(&fr->req, stream, &fr->buf, 1, tls_flush_written) < 0) {
			free(out);
			free(fr);
		}
	}
}

int tls_io_check_shutdown_need(context_arg *carg, int err, int read_size) {
	switch (err) {
		case SSL_ERROR_WANT_READ:
			carglog(carg, L_DEBUG, "%"u64": [%"PRIu64"/%lf] tls client wait for async read %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size);
			return 0;
		case SSL_ERROR_WANT_WRITE:
			carglog(carg, L_DEBUG, "%"u64": [%"PRIu64"/%lf] tls client wait for async write %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size);
			return 0;
		case SSL_ERROR_ZERO_RETURN:
			carglog(carg, L_DEBUG, "%"u64": [%"PRIu64"/%lf] tls client get tls shutdown notifier by peer %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size);
			return 1;
		case SSL_ERROR_SYSCALL:
			if (read_size == 0) {
				carglog(carg, L_ERROR, "%"u64": [%"PRIu64"/%lf] tls client read error %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd, error: %s\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size, "SSL_read: EOF from peer");
			}
			else {
				char buf[256];
				strerror_r(errno, buf, sizeof(buf));
				carglog(carg, L_ERROR, "%"u64": [%"PRIu64"/%lf] tls client read error %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd, error: %s: %s\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size, "SSL_read syscall error", buf);
			}
			return -1;
		case SSL_ERROR_SSL: {
			unsigned long e = ERR_peek_error();
			if (!e) {
				carglog(carg, L_DEBUG, "%"u64": [%"PRIu64"/%lf] tls client read state %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd, error: %s\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size, "SSL_ERROR_SSL without OpenSSL queue entry, wait for more data");
				return 0;
			}

			char msg[256];
			ERR_error_string_n(e, msg, sizeof(msg));
			carglog(carg, L_ERROR, "%"u64": [%"PRIu64"/%lf] tls client read error %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd, error: %s: %s\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size, "SSL protocol error", msg);
			return -1;
		}
		default:
			carglog(carg, L_ERROR, "%"u64": [%"PRIu64"/%lf] tls client read error %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd, error: %s: %d\n", carg->count++, carglog_elapsed_ms(carg, carg->tls_read_time_finish), carglog_elapsed_sec(carg, carg->tls_read_time_finish), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size, "Unknown SSL_read error", err);
			return -1;
	}
}

void tls_write(context_arg *carg, uv_stream_t *stream, char *message, uint64_t len, void *callback) {
	if (!len)
		return;

	if (!carg->tls_write_time.sec && !carg->tls_write_time.nsec)
		carg->tls_write_time = setrtime();

	memset(&carg->write_tls, 0, sizeof(carg->write_tls));
	carg->write_tls.data = carg;

	size_t total_written = 0;
	while (total_written < len) {
		size_t written = 0;
		int r = SSL_write_ex(carg->ssl, message + total_written, len - total_written, &written);
		if (r == 1) {
			total_written += written;
		}
		else {
			int err = SSL_get_error(carg->ssl, r);
			tls_io_check_shutdown_need(carg, err, written);
			break;
		}
	}

	size_t tls_bytes = 0;
	size_t pending = BIO_ctrl_pending(carg->wbio);
	if (!pending)
		return;
	carg->write_buffer = uv_buf_init(calloc(1, pending), pending);
	BIO_read_ex(carg->wbio, carg->write_buffer.base, pending, &tls_bytes);
	carg->write_buffer.len = tls_bytes;

	carglog(carg, L_TRACE, "tls write key %s plain %"PRIu64" tls %zu preview %.*s\n", carg->key, len, carg->write_buffer.len, (int)(len > 80 ? 80 : (int)len), message ? message : "");
	int ret = uv_write(&carg->write_tls, stream, &carg->write_buffer, 1, callback);
	if (ret < 0) {
		free(carg->write_buffer.base);
		carg->write_buffer.base = 0;
		carg->write_buffer.len = 0;
	}
	r_time tls_write_now = setrtime();
	carglog(carg, L_DEBUG, "%"u64": [%"PRIu64"/%lf] client bytes written %p(%p:%p) with key %s, parser name %s, hostname %s, port: %s tls: %d, status: %d, size: %"PRIu64"\n", carg->count++, carglog_elapsed_ms(carg, tls_write_now), carglog_elapsed_sec(carg, tls_write_now), carg, &carg->connect, &carg->client, carg->key, carg->parser_name, carg->host, carg->port, carg->tls, ret > -1, carg->write_buffer.len);
}
