#pragma once
#include "events/context_arg.h"
#include "common/revocation.h"
#include <openssl/x509.h>

/* Optional context for filesystem PEM/PFX callbacks (password + ca_file + revocation). */
typedef struct x509_parse_fctx {
	char *password;
	char *ca_file;
	revocation_policy *pol;
} x509_parse_fctx;

void libcrypto_p12_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename);
int libcrypto_pem_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename);

/* target: label only. check_hostname: network-only CN/SAN check (NULL to skip).
 * ca_file: trust anchors; when NULL and check_hostname!=NULL (network), system CAs are used.
 * For filesystem checks pass check_hostname=NULL; chain verify runs only if ca_file is set. */
int x509_parse_cert(context_arg *carg, X509 *cert, char *cert_name, char *target,
	const char *ca_file, const char *check_hostname, const revocation_policy *pol);
