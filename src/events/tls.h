#pragma once
#include "events/context_arg.h"
int tls_context_init(context_arg *carg, enum ssl_mode mode, int verify, const char *ca, const char * certfile, const char* keyfile, const char *servername, const char *crl);
void tls_init();
void tls_client_cleanup(context_arg *carg, uint8_t clean_context);
char *openssl_get_error_string();
void x509_parse_cert(context_arg *carg, X509 *cert_ctx, char *cert_name, char *host);
void tls_write(context_arg *carg, uv_stream_t *stream, char *message, uint64_t len, void *callback);
int do_tls_shutdown(SSL *ssl);
int tls_io_check_shutdown_need(context_arg *carg, int err, int read_size);
