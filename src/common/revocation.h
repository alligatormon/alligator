#pragma once

#include <stdint.h>
#include <jansson.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

struct context_arg;

#define REV_CRL_SCOPE_LEAF  0
#define REV_CRL_SCOPE_CHAIN 1

#define REV_MODE_SOFT 0
#define REV_MODE_HARD 1

#define REV_FETCH_BACKGROUND 0
#define REV_FETCH_INLINE     1

#define REV_VERIFY_CLIENT_OFF      0
#define REV_VERIFY_CLIENT_OPTIONAL 1
#define REV_VERIFY_CLIENT_REQUIRE  2

typedef enum {
	REV_OK = 0,
	REV_REVOKED,
	REV_UNKNOWN,
	REV_NO_RESPONDER,
	REV_FETCH_ERROR,
	REV_PENDING
} rev_status;

typedef struct revocation_policy {
	uint8_t crl_enabled;
	char *crl_file;
	uint8_t crl_scope;
	uint8_t ocsp_enabled;
	char *ocsp_responder;
	uint8_t ocsp_stapling;
	uint8_t mode;
	uint8_t fetch;
	uint64_t timeout_ms;
	uint64_t cache_ttl_min_ms;
	uint64_t cache_ttl_max_ms;
} revocation_policy;

void revocation_init(void);
void revocation_free(void);

void revocation_policy_init(revocation_policy *p);
void revocation_policy_copy(revocation_policy *dst, const revocation_policy *src);
void revocation_policy_free(revocation_policy *p);
void revocation_policy_parse_json(revocation_policy *p, json_t *root, int tls_prefix);
void revocation_policy_export_json(json_t *ctx, const revocation_policy *p, int tls_prefix);

int config_json_is_on(json_t *j);
int config_json_verify_client(json_t *j);

int revocation_store_apply_crl(X509_STORE *store, const revocation_policy *pol);

rev_status revocation_ocsp_check(struct context_arg *carg, X509 *cert, STACK_OF(X509) *chain,
	const revocation_policy *pol, const char *ca_file, int allow_fetch, int64_t *next_update);

rev_status revocation_ocsp_stapled(SSL *ssl, X509 *cert, STACK_OF(X509) *chain,
	const revocation_policy *pol, const char *ca_file, int64_t *next_update);

int revocation_should_fail(const revocation_policy *pol, rev_status st);
const char *rev_status_str(rev_status st);
const char *rev_status_reason(rev_status st);

/* Stapling then OCSP. Returns 0 if the handshake should continue. */
int revocation_peer_check(struct context_arg *carg, X509 *cert, int allow_fetch);
