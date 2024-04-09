#pragma once
#define X509_TYPE_PEM 1
#define X509_TYPE_PFX 1

typedef struct x509_fs_t {
	char *name;
	char *path;
	char *match;
	char *password;
	uint8_t type;

	uint64_t period;
	uv_timer_t *period_timer;

	tommy_node node;
} x509_fs_t;

void pem_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename);
void parse_cert_info(const mbedtls_x509_crt *cert_ctx, char *cert, char *host);
void jks_push(char *name, char *path, char *match, char *password, char *passtr, uint64_t period);
void jks_del(char *name);
void tls_fs_del(char *name);
void tls_fs_push(char *name, char *path, char *match, char *password, char *type, uint64_t period);
void tls_fs_free();
void tls_fs_handler();
