#pragma once
#define X509_TYPE_PEM 1
#define X509_TYPE_PFX 1

typedef struct x509_fs_t {
	char *name;
	char *path;
	char *match;
	char *password;
	uint8_t type;

	tommy_node node;
} x509_fs_t;

void pem_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename);
