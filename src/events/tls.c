#include "events/context_arg.h"
#include "common/logs.h"
#include "main.h"

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

	printf("Depth %d: %s\n", depth, X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0));
	if (!preverify_ok) {
		printf("Cert verification error: %s\n", X509_verify_cert_error_string(err));
		return 0; // reject
	}
	return 1; // accept
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
	carg->ssl = SSL_new(carg->ssl_ctx);
	if (!carg->ssl_ctx) {
		char buf[256];
		strerror_r(errno, buf, sizeof(buf));
		carglog(carg, L_ERROR, "SSL_new failed: %s\n", buf);
		return 0;
	}

	if (certfile && keyfile) {
		if (SSL_CTX_use_certificate_file(carg->ssl_ctx, certfile,	SSL_FILETYPE_PEM) != 1) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "SSL_CTX_use_certificate_file failed: %s\n", err);
			free(err);
			return 0;
		}

		if (SSL_CTX_use_PrivateKey_file(carg->ssl_ctx, keyfile, SSL_FILETYPE_PEM) != 1) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "SSL_CTX_use_PrivateKey_file failed: %s\n", err);
			free(err);
			return 0;
		}

		if (SSL_CTX_check_private_key(carg->ssl_ctx) != 1) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "SSL_CTX_check_private_key failed: %s\n", err);
			free(err);
			return 0;
		}
		else
			carglog(carg, L_INFO,"certificate and private key loaded and verified\n");
	}

	if (ca) {
		SSL_CTX_load_verify_locations(carg->ssl_ctx, ca, NULL);
	}

	if (crl) {
		X509_STORE *store = SSL_CTX_get_cert_store(carg->ssl_ctx);

		FILE *crl_fp = fopen(crl, "r");
		if (!crl_fp) {
			char buf[256];
			strerror_r(errno, buf, sizeof(buf));
			carglog(carg, L_ERROR, "SSL_CTX_get_cert_store open() failed: %s\n", buf);
			return 0;
		}
		
		X509_CRL *crl = PEM_read_X509_CRL(crl_fp, NULL, NULL, NULL);
		fclose(crl_fp);
		
		if (!crl) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "Failed to read CRL: %s\n", err);
			free(err);

			return 0;
		}
		
		if (!X509_STORE_add_crl(store, crl)) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "Failed to add CRL to store: %s\n", err);
			free(err);

			return 0;
		}

		X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);

		X509_CRL_free(crl);
	}

	SSL_CTX_set_verify(carg->ssl_ctx, SSL_VERIFY_NONE, NULL);
	if (verify) {
		// SSL_VERIFY_FAIL_IF_NO_PEER_CERT - mTLS
		// SSL_VERIFY_NONE - disable
		// SSL_VERIFY_PEER - check peer
		SSL_CTX_set_verify(carg->ssl_ctx, verify, verify_callback);
	}

	if (servername) {
		if (!SSL_set_tlsext_host_name(carg->ssl, servername)) {
			char *err = openssl_get_error_string();
			carglog(carg, L_ERROR, "Failed to set SNI: %s\n", err);
			free(err);
		}
	}

	SSL_CTX_set_ciphersuites(carg->ssl_ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256");
	SSL_CTX_set_cipher_list(carg->ssl_ctx, "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
								 "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
								 "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256");


	SSL_CTX_set_min_proto_version(carg->ssl_ctx, TLS1_2_VERSION);
	SSL_CTX_set_min_proto_version(carg->ssl_ctx, TLS1_3_VERSION);

	if (mode == SSLMODE_SERVER)
		SSL_set_accept_state(carg->ssl);
	else if (mode == SSLMODE_CLIENT) {
		SSL_set_connect_state(carg->ssl);
	    carg->rbio = BIO_new(BIO_s_mem());
	    carg->wbio = BIO_new(BIO_s_mem());
	    SSL_set_bio(carg->ssl, carg->rbio, carg->wbio);
    }


	SSL_CTX_set_verify(carg->ssl_ctx, SSL_VERIFY_PEER, NULL);

	if (carg->log_level >= L_TRACE) {
		SSL_CTX_set_info_callback(carg->ssl_ctx, ssl_info_callback);
	}

	return 1;
}


int asn1_time_to_uint64(const ASN1_TIME *time, uint64_t *out) {
    struct tm tm;
    if (!ASN1_TIME_to_tm(time, &tm)) {
        return 0;
    }

    time_t t = timegm(&tm);  // UTC
    if (t == (time_t)-1) {
        return 0;
    }

    *out = (uint64_t)t;
    return 1;
}
void x509_parse_cert(context_arg *carg, X509 *cert, char *cert_name, char *host) {
	if (!cert)
		return;
	X509_NAME *subject = X509_get_subject_name(cert);
	char common_name[256] = { 0 };
	X509_NAME_get_text_by_NID(subject, NID_commonName, common_name, sizeof(common_name));

	alligator_ht *lbl = calloc(1, sizeof(*lbl));
	alligator_ht_init(lbl);
	labels_hash_insert_nocache(lbl, "cert", cert_name);
	if (host)
		labels_hash_insert_nocache(lbl, "host", host);

	labels_hash_insert_nocache(lbl, "common_name", common_name);
	carg_or_glog(carg, L_INFO, "cert: %s, common_name=%s\n", cert_name, common_name);

	X509_NAME *issuer = X509_get_issuer_name(cert);
	char issuer_str[256];
	X509_NAME_oneline(issuer, issuer_str, sizeof(issuer_str));
	labels_hash_insert_nocache(lbl, "issuer", issuer_str);
	carg_or_glog(carg, L_INFO, "cert: %s, issuer: %s\n", cert_name, issuer_str);

	STACK_OF(GENERAL_NAME) *san_names = NULL;
	san_names = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
	if (san_names) {
		char san_index[20];
		int count = sk_GENERAL_NAME_num(san_names);
		for (int i = 0; i < count; i++) {
			const GENERAL_NAME *name = sk_GENERAL_NAME_value(san_names, i);
			if (name->type == GEN_DNS) {
				snprintf(san_index, 19, "san%d", i);
				char *dns = (char *)ASN1_STRING_get0_data(name->d.dNSName);
				labels_hash_insert_nocache(lbl, san_index, dns);
				carg_or_glog(carg, L_INFO, "cert: %s, san: %s\n", cert_name, dns);
			}
		}
		sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
	}

	ASN1_INTEGER *serial = X509_get_serialNumber(cert);
	BIGNUM *bn = ASN1_INTEGER_to_BN(serial, NULL);
	char *serial_str = BN_bn2hex(bn);
	labels_hash_insert_nocache(lbl, "serial", serial_str);
	carg_or_glog(carg, L_INFO, "cert: %s, serial: %s\n", cert_name, serial_str);

	char buffer[256];
    *buffer = 0;
	if (X509_NAME_get_text_by_NID(subject, NID_countryName, buffer, sizeof(buffer)) > 0) {
		labels_hash_insert_nocache(lbl, "country", buffer);
		carg_or_glog(carg, L_INFO, "cert: %s, countr: %s\n", cert_name, buffer);
	}
    *buffer = 0;
	if (X509_NAME_get_text_by_NID(subject, NID_stateOrProvinceName, buffer, sizeof(buffer)) > 0) {
		labels_hash_insert_nocache(lbl, "county", buffer);
		carg_or_glog(carg, L_INFO, "cert: %s, countr: %s\n", cert_name, buffer);
	}
    *buffer = 0;
	if (X509_NAME_get_text_by_NID(subject, NID_organizationName, buffer, sizeof(buffer)) > 0) {
		labels_hash_insert_nocache(lbl, "organization_unit", buffer);
		carg_or_glog(carg, L_INFO, "cert: %s, countr: %s\n", cert_name, buffer);
	}
    *buffer = 0;
	if (X509_NAME_get_text_by_NID(subject, NID_organizationalUnitName, buffer, sizeof(buffer)) > 0) {
		labels_hash_insert_nocache(lbl, "organization_name", buffer);
		carg_or_glog(carg, L_INFO, "cert: %s, countr: %s\n", cert_name, buffer);
	}

	const ASN1_TIME *notBefore = X509_get0_notBefore(cert);
	const ASN1_TIME *notAfter  = X509_get0_notAfter(cert);
	uint64_t valid_from, valid_to;
	if (asn1_time_to_uint64(notBefore, &valid_from) && asn1_time_to_uint64(notAfter, &valid_to)) {
		r_time now = setrtime();
		int64_t expdays =  (valid_to-now.sec)/86400; 
		carg_or_glog(carg, L_INFO, "cert: %s, complete for: %u.\n", cert_name, now.sec);
		carg_or_glog(carg, L_INFO, "cert: %s, valid from: %"d64".\n", cert_name, valid_from);
		carg_or_glog(carg, L_INFO, "cert: %s, %"d64" exp\n", cert_name, expdays);
		carg_or_glog(carg, L_INFO, "cert: %s, version: %d\n", cert_name, X509_get_version(cert) + 1);
		alligator_ht *notafter_lbl = labels_dup(lbl);
		alligator_ht *expiredays_lbl = labels_dup(lbl);
		metric_add("x509_cert_not_before", lbl, &valid_from, DATATYPE_INT, NULL);
		metric_add("x509_cert_not_after", notafter_lbl, &valid_to, DATATYPE_INT, NULL);
		metric_add("x509_cert_expire_days", expiredays_lbl, &expdays, DATATYPE_INT, NULL);
	}
	else {
		carg_or_glog(carg, L_ERROR, "Failed to parse ASN1_TIME in cert: %s\n", cert_name);
	}

	BN_free(bn);
	OPENSSL_free(serial_str);
}

void tls_client_cleanup(context_arg *carg)
{
	if (carg->ssl) {
		SSL_free(carg->ssl);
		carg->ssl = NULL;
	}

	if (carg->ssl_ctx) {
		SSL_CTX_free(carg->ssl_ctx);
		carg->ssl_ctx = NULL;
	}
	carg->tls_handshake_done = 0;
}

int do_tls_shutdown(SSL *ssl) {
	int ret = SSL_shutdown(ssl);
	if (ret == 1) {
		return 1;
	} else if (ret == 0) {
		ret = SSL_shutdown(ssl);
		return (ret == 1) ? 1 : -1;
	} else {
		int err = SSL_get_error(ssl, ret);
		fprintf(stderr, "SSL_shutdown failed: %d\n", err);
		return -1;
	}
}


int tls_io_check_shutdown_need(context_arg *carg, int err, int read_size) {
	switch (err) {
		case SSL_ERROR_WANT_READ:
			carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client wait for async read %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size);
			return 0;
		case SSL_ERROR_WANT_WRITE:
			carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client wait for async write %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size);
			return 0;
		case SSL_ERROR_ZERO_RETURN:
			carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client get tls shutdown notifier by peer %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size);
			return 1;
		case SSL_ERROR_SYSCALL:
			if (read_size == 0) {
				carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client read error %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd, error: %s\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size, "SSL_read: EOF from peer");
			}
			else {
				char buf[256];
				strerror_r(errno, buf, sizeof(buf));
				carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client read error %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd, error: %s: %s\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size, "SSL_read syscall error", buf);
			}
			return -1;
		case SSL_ERROR_SSL: {
			char *msg = ERR_error_string(err, NULL);
			carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client read error %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd, error: %s: %s\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size, "SSL protocol error", msg);
			return -1;
		}
		default:
			carglog(carg, L_INFO, "%"u64": [%"PRIu64"/%lf] tls client read error %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd, error: %s: %d\n", carg->count++, getrtime_now_ms(carg->tls_read_time_finish), getrtime_sec_float(carg->tls_read_time_finish, carg->connect_time), carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, read_size, "Unknown SSL_read error", err);
			return -1;
	}
}

void tls_write(context_arg *carg, char *message, uint64_t len, void *callback) {
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

	carg->write_buffer = uv_buf_init(calloc(1, EVENT_BUFFER), EVENT_BUFFER);
	carg->write_buffer.len = BIO_read(carg->wbio, carg->write_buffer.base, EVENT_BUFFER);
	carglog(carg, L_TRACE, "\n==================WRITEBASE(plain:%"PRIu64"/tls:%d)===================\n'%s'\n======\n", len, carg->write_buffer.len, message ? message : "");
	uv_write(&carg->write_tls, carg->connect.handle, &carg->write_buffer, 1, callback);
}
