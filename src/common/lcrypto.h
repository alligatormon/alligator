#pragma once
#include "events/context_arg.h"
void libcrypto_p12_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename);
int libcrypto_pem_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename);
void x509_parse_cert(context_arg *carg, X509 *cert, char *cert_name, char *host);
