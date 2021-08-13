#pragma once

typedef struct x509_fs_t {
	char *name;
	char *path;
	char *match;

	tommy_node node;
} x509_fs_t;

void pem_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename);
