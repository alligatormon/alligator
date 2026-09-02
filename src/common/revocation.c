#include "common/revocation.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "common/units.h"
#include "common/http.h"
#include "common/url.h"
#include "common/aggregator.h"
#include "common/selector.h"
#include "dstructures/ht.h"
#include "metric/namespace.h"
#include "main.h"

#include <uv.h>
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

extern aconf *ac;
char *mask_password(const char *url);

#ifndef REV_OCSP_CACHE_MAX
#define REV_OCSP_CACHE_MAX 4096
#endif

#ifndef REV_DEFAULT_TIMEOUT_MS
#define REV_DEFAULT_TIMEOUT_MS 3000
#endif

#ifndef REV_DEFAULT_TTL_MIN_MS
#define REV_DEFAULT_TTL_MIN_MS 60000
#endif

#ifndef REV_DEFAULT_TTL_MAX_MS
#define REV_DEFAULT_TTL_MAX_MS 3600000
#endif

typedef struct crl_cache_entry {
	alligator_ht_node node;
	char *path;
	time_t mtime;
	STACK_OF(X509_CRL) *crls;
	int refs;
	int in_cache;
} crl_cache_entry;

typedef struct ocsp_cache_entry {
	alligator_ht_node node;
	char *key;
	rev_status status;
	int64_t next_update;
	uint64_t expire_ms;
	int ready;
	int inflight;
} ocsp_cache_entry;

typedef struct ocsp_bg_job {
	uv_timer_t timer;
	unsigned char *cert_der;
	int cert_len;
	unsigned char *issuer_der;
	int issuer_len;
	char *ca_file;
	char *cache_key;
	revocation_policy pol;
} ocsp_bg_job;

static alligator_ht *crl_cache;
static alligator_ht *ocsp_cache;
static uv_mutex_t rev_lock;
static int rev_ready;

int config_json_is_on(json_t *j)
{
	const char *s;

	if (!j)
		return 0;
	if (json_is_true(j))
		return 1;
	if (json_is_false(j))
		return 0;
	if (json_is_integer(j))
		return json_integer_value(j) != 0;
	s = json_string_value(j);
	if (!s)
		return 0;
	return !strcmp(s, "on") || !strcmp(s, "true") || !strcmp(s, "1") || !strcmp(s, "yes");
}

int config_json_verify_client(json_t *j)
{
	const char *s;

	if (!j)
		return REV_VERIFY_CLIENT_OFF;
	if (json_is_integer(j)) {
		int v = (int)json_integer_value(j);
		if (v >= REV_VERIFY_CLIENT_REQUIRE)
			return REV_VERIFY_CLIENT_REQUIRE;
		if (v > 0)
			return REV_VERIFY_CLIENT_OPTIONAL;
		return REV_VERIFY_CLIENT_OFF;
	}
	s = json_string_value(j);
	if (!s)
		return REV_VERIFY_CLIENT_OFF;
	if (!strcmp(s, "require") || !strcmp(s, "on") || !strcmp(s, "mandatory"))
		return REV_VERIFY_CLIENT_REQUIRE;
	if (!strcmp(s, "optional") || !strcmp(s, "request"))
		return REV_VERIFY_CLIENT_OPTIONAL;
	return REV_VERIFY_CLIENT_OFF;
}

const char *rev_status_str(rev_status st)
{
	switch (st) {
		case REV_OK: return "ok";
		case REV_REVOKED: return "revoked";
		case REV_UNKNOWN: return "unknown";
		case REV_NO_RESPONDER: return "no_responder";
		case REV_FETCH_ERROR: return "error";
		case REV_PENDING: return "pending";
		default: return "error";
	}
}

const char *rev_status_reason(rev_status st)
{
	if (st == REV_REVOKED)
		return "revoked";
	if (st == REV_OK || st == REV_PENDING)
		return NULL;
	return "invalid_chain";
}

int revocation_should_fail(const revocation_policy *pol, rev_status st)
{
	if (st == REV_REVOKED)
		return 1;
	if (!pol || pol->mode != REV_MODE_HARD)
		return 0;
	return st == REV_FETCH_ERROR || st == REV_UNKNOWN || st == REV_NO_RESPONDER;
}

void revocation_policy_init(revocation_policy *p)
{
	if (!p)
		return;
	memset(p, 0, sizeof(*p));
	p->timeout_ms = REV_DEFAULT_TIMEOUT_MS;
	p->cache_ttl_min_ms = REV_DEFAULT_TTL_MIN_MS;
	p->cache_ttl_max_ms = REV_DEFAULT_TTL_MAX_MS;
	p->mode = REV_MODE_SOFT;
	p->fetch = REV_FETCH_BACKGROUND;
	p->crl_scope = REV_CRL_SCOPE_LEAF;
}

void revocation_policy_free(revocation_policy *p)
{
	if (!p)
		return;
	free(p->crl_file);
	free(p->ocsp_responder);
	free(p->ocsp_proxy);
	p->crl_file = NULL;
	p->ocsp_responder = NULL;
	p->ocsp_proxy = NULL;
}

void revocation_policy_copy(revocation_policy *dst, const revocation_policy *src)
{
	if (!dst || !src)
		return;
	revocation_policy_free(dst);
	*dst = *src;
	dst->crl_file = src->crl_file ? strdup(src->crl_file) : NULL;
	dst->ocsp_responder = src->ocsp_responder ? strdup(src->ocsp_responder) : NULL;
	dst->ocsp_proxy = src->ocsp_proxy ? strdup(src->ocsp_proxy) : NULL;
}

static json_t *pol_get(json_t *root, int tls_prefix, const char *bare, const char *alt)
{
	json_t *j;
	char key[64];

	if (!root)
		return NULL;
	if (tls_prefix) {
		snprintf(key, sizeof(key), "tls_%s", bare);
		j = json_object_get(root, key);
		if (j)
			return j;
	} else {
		j = json_object_get(root, bare);
		if (j)
			return j;
		if (alt) {
			j = json_object_get(root, alt);
			if (j)
				return j;
		}
	}
	return json_object_get(root, bare);
}

static uint64_t json_duration_ms(json_t *j, uint64_t fallback)
{
	if (!j)
		return fallback;
	if (json_is_string(j))
		return (uint64_t)get_ms_from_human_range(json_string_value(j), json_string_length(j));
	if (json_is_integer(j))
		return (uint64_t)json_integer_value(j);
	if (json_is_real(j))
		return (uint64_t)json_real_value(j);
	return fallback;
}

void revocation_policy_parse_json(revocation_policy *p, json_t *root, int tls_prefix)
{
	json_t *j;
	const char *s;

	if (!p || !root)
		return;

	j = tls_prefix ? json_object_get(root, "tls_crl") : json_object_get(root, "crl_file");
	if (!j && !tls_prefix)
		j = json_object_get(root, "crl");
	s = j ? json_string_value(j) : NULL;
	if (s && *s) {
		free(p->crl_file);
		p->crl_file = strdup(s);
		p->crl_enabled = 1;
	}

	j = pol_get(root, tls_prefix, "crl_scope", NULL);
	s = j ? json_string_value(j) : NULL;
	if (s && !strcmp(s, "chain"))
		p->crl_scope = REV_CRL_SCOPE_CHAIN;
	else if (s && !strcmp(s, "leaf"))
		p->crl_scope = REV_CRL_SCOPE_LEAF;

	j = pol_get(root, tls_prefix, "ocsp", NULL);
	if (j)
		p->ocsp_enabled = config_json_is_on(j) ? 1 : 0;

	j = pol_get(root, tls_prefix, "ocsp_responder", NULL);
	s = j ? json_string_value(j) : NULL;
	if (s && *s) {
		free(p->ocsp_responder);
		p->ocsp_responder = strdup(s);
		if (!j || p->ocsp_enabled || !tls_prefix)
			p->ocsp_enabled = 1;
	}

	j = pol_get(root, tls_prefix, "ocsp_proxy", NULL);
	s = j ? json_string_value(j) : NULL;
	if (s && *s) {
		free(p->ocsp_proxy);
		p->ocsp_proxy = strdup(s);
	}

	j = pol_get(root, tls_prefix, "ocsp_stapling", NULL);
	if (j)
		p->ocsp_stapling = config_json_is_on(j) ? 1 : 0;

	j = pol_get(root, tls_prefix, "ocsp_timeout", NULL);
	p->timeout_ms = json_duration_ms(j, p->timeout_ms ? p->timeout_ms : REV_DEFAULT_TIMEOUT_MS);

	j = pol_get(root, tls_prefix, "ocsp_cache_ttl", NULL);
	if (j) {
		uint64_t ttl = json_duration_ms(j, p->cache_ttl_max_ms);
		p->cache_ttl_max_ms = ttl;
		if (p->cache_ttl_min_ms > ttl)
			p->cache_ttl_min_ms = ttl;
	}

	j = pol_get(root, tls_prefix, "revocation_mode", NULL);
	s = j ? json_string_value(j) : NULL;
	if (s && !strcmp(s, "hard"))
		p->mode = REV_MODE_HARD;
	else if (s && !strcmp(s, "soft"))
		p->mode = REV_MODE_SOFT;

	j = pol_get(root, tls_prefix, "ocsp_fetch", NULL);
	s = j ? json_string_value(j) : NULL;
	if (s && !strcmp(s, "inline"))
		p->fetch = REV_FETCH_INLINE;
	else if (s && !strcmp(s, "background"))
		p->fetch = REV_FETCH_BACKGROUND;
}

void revocation_policy_export_json(json_t *ctx, const revocation_policy *p, int tls_prefix)
{
	if (!ctx || !p)
		return;
	if (p->crl_file)
		json_object_set_new(ctx, tls_prefix ? "tls_crl" : "crl_file", json_string(p->crl_file));
	if (p->crl_enabled)
		json_object_set_new(ctx, tls_prefix ? "tls_crl_scope" : "crl_scope",
			json_string(p->crl_scope == REV_CRL_SCOPE_CHAIN ? "chain" : "leaf"));
	if (p->ocsp_enabled)
		json_object_set_new(ctx, tls_prefix ? "tls_ocsp" : "ocsp", json_string("on"));
	if (p->ocsp_responder)
		json_object_set_new(ctx, tls_prefix ? "tls_ocsp_responder" : "ocsp_responder",
			json_string(p->ocsp_responder));
	if (p->ocsp_proxy) {
		char *masked = mask_password(p->ocsp_proxy);
		json_object_set_new(ctx, tls_prefix ? "tls_ocsp_proxy" : "ocsp_proxy",
			json_string(masked ? masked : p->ocsp_proxy));
		free(masked);
	}
	if (p->ocsp_stapling)
		json_object_set_new(ctx, "tls_ocsp_stapling", json_string("on"));
	if (p->mode == REV_MODE_HARD)
		json_object_set_new(ctx, tls_prefix ? "tls_revocation_mode" : "revocation_mode", json_string("hard"));
	if (p->fetch == REV_FETCH_INLINE)
		json_object_set_new(ctx, tls_prefix ? "tls_ocsp_fetch" : "ocsp_fetch", json_string("inline"));
}

static int path_compare(const void *arg, const void *obj)
{
	return strcmp((const char *)arg, ((const crl_cache_entry *)obj)->path);
}

static int ocsp_key_compare(const void *arg, const void *obj)
{
	return strcmp((const char *)arg, ((const ocsp_cache_entry *)obj)->key);
}

void revocation_init(void)
{
	if (rev_ready)
		return;
	crl_cache = alligator_ht_init(NULL);
	ocsp_cache = alligator_ht_init(NULL);
	uv_mutex_init(&rev_lock);
	rev_ready = 1;
}

static void crl_entry_free(void *arg)
{
	crl_cache_entry *e = arg;
	if (!e)
		return;
	free(e->path);
	if (e->crls)
		sk_X509_CRL_pop_free(e->crls, X509_CRL_free);
	free(e);
}

static void ocsp_entry_free(void *arg)
{
	ocsp_cache_entry *e = arg;
	if (!e)
		return;
	free(e->key);
	free(e);
}

void revocation_free(void)
{
	if (!rev_ready)
		return;
	alligator_ht_foreach(crl_cache, crl_entry_free);
	alligator_ht_foreach(ocsp_cache, ocsp_entry_free);
	alligator_ht_done(crl_cache);
	alligator_ht_done(ocsp_cache);
	free(crl_cache);
	free(ocsp_cache);
	crl_cache = NULL;
	ocsp_cache = NULL;
	uv_mutex_destroy(&rev_lock);
	rev_ready = 0;
}

static crl_cache_entry *crl_load_file(const char *path)
{
	struct stat st;
	FILE *fp;
	X509_CRL *crl;
	crl_cache_entry *e;
	STACK_OF(X509_CRL) *crls;

	if (!path || stat(path, &st) != 0)
		return NULL;

	fp = fopen(path, "r");
	if (!fp)
		return NULL;

	crls = sk_X509_CRL_new_null();
	if (!crls) {
		fclose(fp);
		return NULL;
	}

	while ((crl = PEM_read_X509_CRL(fp, NULL, NULL, NULL))) {
		sk_X509_CRL_push(crls, crl);
	}
	if (sk_X509_CRL_num(crls) == 0) {
		rewind(fp);
		crl = d2i_X509_CRL_fp(fp, NULL);
		if (crl)
			sk_X509_CRL_push(crls, crl);
	}
	fclose(fp);

	if (sk_X509_CRL_num(crls) == 0) {
		sk_X509_CRL_free(crls);
		return NULL;
	}

	e = calloc(1, sizeof(*e));
	if (!e) {
		sk_X509_CRL_pop_free(crls, X509_CRL_free);
		return NULL;
	}
	e->path = strdup(path);
	e->mtime = st.st_mtime;
	e->crls = crls;
	e->refs = 0;
	e->in_cache = 0;
	return e;
}

static void crl_cache_release(crl_cache_entry *e)
{
	if (!e)
		return;
	uv_mutex_lock(&rev_lock);
	e->refs--;
	if (e->refs <= 0 && !e->in_cache)
		crl_entry_free(e);
	uv_mutex_unlock(&rev_lock);
}

static crl_cache_entry *crl_cache_get(const char *path)
{
	crl_cache_entry *e;
	struct stat st;
	uint32_t hash;

	if (!path || !rev_ready)
		return NULL;
	hash = tommy_strhash_u32(0, path);
	uv_mutex_lock(&rev_lock);
	e = alligator_ht_search(crl_cache, path_compare, path, hash);
	if (e && stat(path, &st) == 0 && e->mtime == st.st_mtime) {
		e->refs++;
		uv_mutex_unlock(&rev_lock);
		return e;
	}
	if (e) {
		alligator_ht_remove_existing(crl_cache, &e->node);
		e->in_cache = 0;
		if (e->refs <= 0)
			crl_entry_free(e);
	}
	uv_mutex_unlock(&rev_lock);

	e = crl_load_file(path);
	if (!e)
		return NULL;

	uv_mutex_lock(&rev_lock);
	e->refs = 1;
	e->in_cache = 1;
	alligator_ht_insert(crl_cache, &e->node, e, hash);
	uv_mutex_unlock(&rev_lock);
	return e;
}

int revocation_store_apply_crl(X509_STORE *store, const revocation_policy *pol)
{
	crl_cache_entry *e;
	int i;
	unsigned long flags;

	if (!store || !pol || !pol->crl_enabled || !pol->crl_file)
		return 1;

	e = crl_cache_get(pol->crl_file);
	if (!e) {
		glog(L_ERROR, "revocation: failed to load CRL file '%s'\n", pol->crl_file);
		return 0;
	}

	for (i = 0; i < sk_X509_CRL_num(e->crls); i++)
		X509_STORE_add_crl(store, sk_X509_CRL_value(e->crls, i));

	flags = X509_V_FLAG_CRL_CHECK;
	if (pol->crl_scope == REV_CRL_SCOPE_CHAIN)
		flags |= X509_V_FLAG_CRL_CHECK_ALL;
	X509_STORE_set_flags(store, flags);
	crl_cache_release(e);
	return 1;
}

static X509 *find_issuer(X509 *cert, STACK_OF(X509) *chain, const char *ca_file)
{
	int i;
	X509 *issuer = NULL;
	X509_STORE *store;
	X509_STORE_CTX *ctx;

	if (!cert)
		return NULL;

	if (chain) {
		for (i = 0; i < sk_X509_num(chain); i++) {
			X509 *c = sk_X509_value(chain, i);
			if (c != cert && X509_check_issued(c, cert) == X509_V_OK) {
				X509_up_ref(c);
				return c;
			}
		}
	}

	if (!ca_file)
		return NULL;

	store = X509_STORE_new();
	ctx = X509_STORE_CTX_new();
	if (!store || !ctx) {
		X509_STORE_CTX_free(ctx);
		X509_STORE_free(store);
		return NULL;
	}
	if (X509_STORE_load_locations(store, ca_file, NULL) == 1 &&
	    X509_STORE_CTX_init(ctx, store, cert, chain) == 1) {
		if (X509_STORE_CTX_get1_issuer(&issuer, ctx, cert) <= 0)
			issuer = NULL;
	}
	X509_STORE_CTX_free(ctx);
	X509_STORE_free(store);
	return issuer;
}

static char *ocsp_cache_key(X509 *cert, X509 *issuer)
{
	OCSP_CERTID *id;
	ASN1_OCTET_STRING *iname = NULL, *ikey = NULL;
	ASN1_INTEGER *serial = NULL;
	char *hex_name = NULL, *hex_key = NULL, *hex_ser = NULL;
	char *key;
	BIGNUM *bn;

	id = OCSP_cert_to_id(EVP_sha1(), cert, issuer);
	if (!id)
		return NULL;
	if (!OCSP_id_get0_info(&iname, NULL, &ikey, &serial, id)) {
		OCSP_CERTID_free(id);
		return NULL;
	}
	hex_name = OPENSSL_buf2hexstr(ASN1_STRING_get0_data(iname), ASN1_STRING_length(iname));
	hex_key = OPENSSL_buf2hexstr(ASN1_STRING_get0_data(ikey), ASN1_STRING_length(ikey));
	bn = ASN1_INTEGER_to_BN(serial, NULL);
	hex_ser = bn ? BN_bn2hex(bn) : NULL;
	BN_free(bn);
	OCSP_CERTID_free(id);
	if (!hex_name || !hex_key || !hex_ser) {
		OPENSSL_free(hex_name);
		OPENSSL_free(hex_key);
		OPENSSL_free(hex_ser);
		return NULL;
	}
	key = malloc(strlen(hex_name) + strlen(hex_key) + strlen(hex_ser) + 4);
	if (key)
		sprintf(key, "%s|%s|%s", hex_name, hex_key, hex_ser);
	OPENSSL_free(hex_name);
	OPENSSL_free(hex_key);
	OPENSSL_free(hex_ser);
	return key;
}

static char *ocsp_url_from_cert(X509 *cert, const revocation_policy *pol)
{
	STACK_OF(OPENSSL_STRING) *aia;
	char *url = NULL;

	if (pol && pol->ocsp_responder && pol->ocsp_responder[0])
		return strdup(pol->ocsp_responder);

	aia = X509_get1_ocsp(cert);
	if (aia && sk_OPENSSL_STRING_num(aia) > 0) {
		const char *s = sk_OPENSSL_STRING_value(aia, 0);
		if (s)
			url = strdup(s);
	}
	if (aia)
		X509_email_free(aia);
	return url;
}

static void ocsp_metric(const char *result, uint64_t start_ms)
{
	uint64_t one = 1;
	uint64_t now;
	double dur;

	if (!ac || !ac->system_carg)
		return;
	now = ac->loop ? uv_now(ac->loop) : 0;
	dur = (now >= start_ms) ? (double)(now - start_ms) / 1000.0 : 0.0;
	metric_update_labels2("ocsp_requests_total", &one, DATATYPE_UINT,
		ac->system_carg, "result", (char *)result, "method", "POST");
	metric_update_labels2("ocsp_request_duration_seconds_sum", &dur, DATATYPE_DOUBLE,
		ac->system_carg, "result", (char *)result, "method", "POST");
}

static uint64_t clamp_ttl(const revocation_policy *pol, int64_t next_update)
{
	uint64_t ttl;
	uint64_t now_sec;
	uint64_t min_ms = pol && pol->cache_ttl_min_ms ? pol->cache_ttl_min_ms : REV_DEFAULT_TTL_MIN_MS;
	uint64_t max_ms = pol && pol->cache_ttl_max_ms ? pol->cache_ttl_max_ms : REV_DEFAULT_TTL_MAX_MS;

	now_sec = ac && ac->loop ? uv_now(ac->loop) / 1000 : (uint64_t)time(NULL);
	if (next_update > 0 && (uint64_t)next_update > now_sec)
		ttl = ((uint64_t)next_update - now_sec) * 1000;
	else
		ttl = min_ms;
	if (ttl < min_ms)
		ttl = min_ms;
	if (ttl > max_ms)
		ttl = max_ms;
	return ttl;
}

static ocsp_cache_entry *ocsp_cache_lookup(const char *key)
{
	return alligator_ht_search(ocsp_cache, ocsp_key_compare, key, tommy_strhash_u32(0, key));
}

static void ocsp_cache_store(const char *key, rev_status st, int64_t next_update, const revocation_policy *pol)
{
	ocsp_cache_entry *e;
	uint64_t now = ac && ac->loop ? uv_now(ac->loop) : 0;
	uint32_t hash = tommy_strhash_u32(0, key);

	uv_mutex_lock(&rev_lock);
	e = ocsp_cache_lookup(key);
	if (!e) {
		e = calloc(1, sizeof(*e));
		if (!e) {
			uv_mutex_unlock(&rev_lock);
			return;
		}
		e->key = strdup(key);
		alligator_ht_insert(ocsp_cache, &e->node, e, hash);
	}
	e->status = st;
	e->next_update = next_update;
	e->expire_ms = now + clamp_ttl(pol, next_update);
	e->ready = 1;
	e->inflight = 0;
	uv_mutex_unlock(&rev_lock);
}

static int ocsp_cache_begin(const char *key)
{
	ocsp_cache_entry *e;
	uint32_t hash = tommy_strhash_u32(0, key);

	uv_mutex_lock(&rev_lock);
	e = ocsp_cache_lookup(key);
	if (e && e->inflight) {
		uv_mutex_unlock(&rev_lock);
		return 0;
	}
	if (!e) {
		e = calloc(1, sizeof(*e));
		if (!e) {
			uv_mutex_unlock(&rev_lock);
			return 0;
		}
		e->key = strdup(key);
		alligator_ht_insert(ocsp_cache, &e->node, e, hash);
	}
	e->inflight = 1;
	e->ready = 0;
	uv_mutex_unlock(&rev_lock);
	return 1;
}

static rev_status ocsp_validate_response(const unsigned char *der, size_t der_len,
	X509 *cert, X509 *issuer, const char *ca_file, STACK_OF(X509) *chain,
	int64_t *next_update)
{
	const unsigned char *p = der;
	OCSP_RESPONSE *resp;
	OCSP_BASICRESP *basic = NULL;
	OCSP_CERTID *id = NULL;
	X509_STORE *store = NULL;
	int status = V_OCSP_CERTSTATUS_UNKNOWN;
	int reason = 0;
	ASN1_GENERALIZEDTIME *thisupd = NULL, *nextupd = NULL;
	rev_status out = REV_FETCH_ERROR;

	if (next_update)
		*next_update = 0;

	resp = d2i_OCSP_RESPONSE(NULL, &p, (long)der_len);
	if (!resp)
		return REV_FETCH_ERROR;
	if (OCSP_response_status(resp) != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
		OCSP_RESPONSE_free(resp);
		return REV_FETCH_ERROR;
	}
	basic = OCSP_response_get1_basic(resp);
	if (!basic) {
		OCSP_RESPONSE_free(resp);
		return REV_FETCH_ERROR;
	}

	store = X509_STORE_new();
	if (store) {
		if (ca_file)
			X509_STORE_load_locations(store, ca_file, NULL);
		else
			X509_STORE_set_default_paths(store);
		if (OCSP_basic_verify(basic, chain, store, 0) <= 0) {
			/* still accept if we can read a status; nginx-like soft path */
			glog(L_DEBUG, "revocation: OCSP_basic_verify failed\n");
		}
	}

	id = OCSP_cert_to_id(EVP_sha1(), cert, issuer);
	if (id && OCSP_resp_find_status(basic, id, &status, &reason, NULL, &thisupd, &nextupd) == 1) {
		if (thisupd && OCSP_check_validity(thisupd, nextupd, 300, -1) != 1)
			out = REV_FETCH_ERROR;
		else if (status == V_OCSP_CERTSTATUS_GOOD)
			out = REV_OK;
		else if (status == V_OCSP_CERTSTATUS_REVOKED)
			out = REV_REVOKED;
		else
			out = REV_UNKNOWN;
		if (nextupd && next_update) {
			struct tm tm;
			memset(&tm, 0, sizeof(tm));
			if (ASN1_TIME_to_tm(nextupd, &tm))
				*next_update = (int64_t)timegm(&tm);
		}
	}

	OCSP_CERTID_free(id);
	X509_STORE_free(store);
	OCSP_BASICRESP_free(basic);
	OCSP_RESPONSE_free(resp);
	return out;
}

static rev_status ocsp_fetch_once(X509 *cert, X509 *issuer, const char *url,
	const revocation_policy *pol, const char *ca_file, STACK_OF(X509) *chain,
	int64_t *next_update)
{
	OCSP_REQUEST *req;
	OCSP_CERTID *id;
	unsigned char *der = NULL;
	int derlen;
	host_aggregator_info *hi;
	alligator_ht *env;
	string body;
	char clen[32];
	char *query;
	char *sep;
	size_t qlen;
	context_arg hint;
	aggregator_await_res_t res;
	uv_loop_t *loop;
	uint64_t start;
	rev_status st;
	int r;

	if (!url || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0))
		return REV_NO_RESPONDER;

	req = OCSP_REQUEST_new();
	id = OCSP_cert_to_id(EVP_sha1(), cert, issuer);
	if (!req || !id) {
		OCSP_REQUEST_free(req);
		OCSP_CERTID_free(id);
		return REV_FETCH_ERROR;
	}
	OCSP_request_add0_id(req, id);
	derlen = i2d_OCSP_REQUEST(req, &der);
	OCSP_REQUEST_free(req);
	if (derlen <= 0 || !der)
		return REV_FETCH_ERROR;

	hi = parse_url((char *)url, strlen(url));
	if (!hi) {
		OPENSSL_free(der);
		return REV_FETCH_ERROR;
	}

	env = alligator_ht_init(NULL);
	env_struct_push_alloc(env, "Content-Type", "application/ocsp-request");
	snprintf(clen, sizeof(clen), "%d", derlen);
	env_struct_push_alloc(env, "Content-Length", clen);
	env_struct_push_alloc(env, "Connection", "close");

	memset(&body, 0, sizeof(body));
	body.s = (char *)der;
	body.l = (size_t)derlen;
	body.m = (size_t)derlen;
	query = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator",
		hi->auth, NULL, env, NULL, &body);
	url_free(hi);
	if (!query) {
		OPENSSL_free(der);
		env_free(env);
		return REV_FETCH_ERROR;
	}
	sep = strstr(query, "\r\n\r\n");
	qlen = sep ? (size_t)(sep - query) + 4 + (size_t)derlen : strlen(query);

	loop = (ac && ac->loop) ? ac->loop : uv_default_loop();
	start = uv_now(loop);
	memset(&hint, 0, sizeof(hint));
	hint.timeout = pol && pol->timeout_ms ? pol->timeout_ms : REV_DEFAULT_TIMEOUT_MS;
	hint.log_level = ac ? ac->log_level : 0;
	if (pol && pol->ocsp_proxy && pol->ocsp_proxy[0]) {
		hint.proxy = proxy_parse_url(pol->ocsp_proxy);
		if (!hint.proxy) {
			glog(L_ERROR, "revocation: cannot parse ocsp proxy '%s'\n", pol->ocsp_proxy);
			free(query);
			OPENSSL_free(der);
			env_free(env);
			return REV_FETCH_ERROR;
		}
		glog(L_DEBUG, "revocation: OCSP fetch via proxy %s:%s\n",
			hint.proxy->host, hint.proxy->port);
	}

	memset(&res, 0, sizeof(res));
	r = aggregator_oneshot_await(&hint, (char *)url, strlen(url), query, qlen,
		NULL, "ocsp", NULL, NULL, 1, NULL, NULL, 0, NULL, NULL, &res);
	proxy_settings_free(hint.proxy);
	hint.proxy = NULL;
	OPENSSL_free(der);
	env_free(env);
	if (r < 0 || res.http_code < 200 || res.http_code >= 300 || !res.body) {
		ocsp_metric("failure", start);
		aggregator_await_res_free(&res);
		return REV_FETCH_ERROR;
	}

	st = ocsp_validate_response((unsigned char *)res.body, res.body_len,
		cert, issuer, ca_file, chain, next_update);
	ocsp_metric(st == REV_OK ? "success" : (st == REV_REVOKED ? "revoked" : "failure"), start);
	aggregator_await_res_free(&res);
	return st;
}

static void ocsp_bg_job_free(uv_handle_t *h)
{
	ocsp_bg_job *job = h->data;
	if (!job)
		return;
	free(job->cert_der);
	free(job->issuer_der);
	free(job->ca_file);
	free(job->cache_key);
	revocation_policy_free(&job->pol);
	free(job);
}

static void ocsp_bg_cb(uv_timer_t *t)
{
	ocsp_bg_job *job = t->data;
	const unsigned char *cp, *ip;
	X509 *cert = NULL, *issuer = NULL;
	char *url;
	int64_t next_update = 0;
	rev_status st;

	uv_timer_stop(t);

	cp = job->cert_der;
	ip = job->issuer_der;
	cert = d2i_X509(NULL, &cp, job->cert_len);
	issuer = d2i_X509(NULL, &ip, job->issuer_len);
	if (!cert || !issuer) {
		if (job->cache_key)
			ocsp_cache_store(job->cache_key, REV_FETCH_ERROR, 0, &job->pol);
		goto done;
	}
	url = job->pol.ocsp_responder ? strdup(job->pol.ocsp_responder) : ocsp_url_from_cert(cert, &job->pol);
	if (!url) {
		ocsp_cache_store(job->cache_key, REV_NO_RESPONDER, 0, &job->pol);
		goto done;
	}
	st = ocsp_fetch_once(cert, issuer, url, &job->pol, job->ca_file, NULL, &next_update);
	ocsp_cache_store(job->cache_key, st, next_update, &job->pol);
	free(url);

done:
	X509_free(cert);
	X509_free(issuer);
	if (!uv_is_closing((uv_handle_t *)t))
		uv_close((uv_handle_t *)t, ocsp_bg_job_free);
}

static void ocsp_schedule_background(X509 *cert, X509 *issuer, const revocation_policy *pol,
	const char *ca_file, const char *cache_key)
{
	ocsp_bg_job *job;
	unsigned char *cd = NULL, *id = NULL;
	int clen, ilen;

	if (!ac || !ac->loop || !cert || !issuer || !cache_key)
		return;
	if (!ocsp_cache_begin(cache_key))
		return;

	clen = i2d_X509(cert, &cd);
	ilen = i2d_X509(issuer, &id);
	if (clen <= 0 || ilen <= 0) {
		OPENSSL_free(cd);
		OPENSSL_free(id);
		ocsp_cache_store(cache_key, REV_FETCH_ERROR, 0, pol);
		return;
	}

	job = calloc(1, sizeof(*job));
	if (!job) {
		OPENSSL_free(cd);
		OPENSSL_free(id);
		return;
	}
	job->cert_der = malloc((size_t)clen);
	job->issuer_der = malloc((size_t)ilen);
	if (!job->cert_der || !job->issuer_der) {
		OPENSSL_free(cd);
		OPENSSL_free(id);
		free(job->cert_der);
		free(job->issuer_der);
		free(job);
		return;
	}
	memcpy(job->cert_der, cd, (size_t)clen);
	memcpy(job->issuer_der, id, (size_t)ilen);
	OPENSSL_free(cd);
	OPENSSL_free(id);
	job->cert_len = clen;
	job->issuer_len = ilen;
	job->ca_file = ca_file ? strdup(ca_file) : NULL;
	job->cache_key = strdup(cache_key);
	revocation_policy_init(&job->pol);
	revocation_policy_copy(&job->pol, pol);

	uv_timer_init(ac->loop, &job->timer);
	job->timer.data = job;
	uv_timer_start(&job->timer, ocsp_bg_cb, 0, 0);
}

static rev_status ocsp_from_cache(const char *key, int64_t *next_update)
{
	ocsp_cache_entry *e;
	uint64_t now = ac && ac->loop ? uv_now(ac->loop) : 0;
	rev_status st;

	uv_mutex_lock(&rev_lock);
	e = ocsp_cache_lookup(key);
	if (!e) {
		uv_mutex_unlock(&rev_lock);
		return REV_UNKNOWN;
	}
	if (e->inflight && !e->ready) {
		uv_mutex_unlock(&rev_lock);
		return REV_PENDING;
	}
	if (!e->ready || (e->expire_ms && now >= e->expire_ms)) {
		uv_mutex_unlock(&rev_lock);
		return REV_UNKNOWN;
	}
	st = e->status;
	if (next_update)
		*next_update = e->next_update;
	uv_mutex_unlock(&rev_lock);
	return st;
}

static rev_status ocsp_run(X509 *cert, STACK_OF(X509) *chain, const revocation_policy *pol,
	const char *ca_file, int allow_fetch, int64_t *next_update)
{
	X509 *issuer;
	char *key;
	char *url;
	rev_status cached;
	rev_status st;

	if (!pol || !pol->ocsp_enabled || !cert)
		return REV_OK;

	issuer = find_issuer(cert, chain, ca_file);
	if (!issuer)
		return REV_NO_RESPONDER;

	key = ocsp_cache_key(cert, issuer);
	if (!key) {
		X509_free(issuer);
		return REV_FETCH_ERROR;
	}

	cached = ocsp_from_cache(key, next_update);
	if (cached == REV_OK || cached == REV_REVOKED || cached == REV_PENDING) {
		free(key);
		X509_free(issuer);
		return cached;
	}
	if (cached == REV_FETCH_ERROR || cached == REV_NO_RESPONDER || cached == REV_UNKNOWN) {
		/* expired/missing treated as miss except we still return pending above */
	}

	url = ocsp_url_from_cert(cert, pol);
	if (!url) {
		free(key);
		X509_free(issuer);
		return REV_NO_RESPONDER;
	}

	if (!allow_fetch) {
		ocsp_schedule_background(cert, issuer, pol, ca_file, key);
		free(url);
		free(key);
		X509_free(issuer);
		return REV_PENDING;
	}

	st = ocsp_fetch_once(cert, issuer, url, pol, ca_file, chain, next_update);
	ocsp_cache_store(key, st, next_update ? *next_update : 0, pol);
	free(url);
	free(key);
	X509_free(issuer);
	return st;
}

rev_status revocation_ocsp_check(struct context_arg *carg, X509 *cert, STACK_OF(X509) *chain,
	const revocation_policy *pol, const char *ca_file, int allow_fetch, int64_t *next_update)
{
	(void)carg;
	if (!rev_ready)
		revocation_init();
	return ocsp_run(cert, chain, pol, ca_file, allow_fetch, next_update);
}

int revocation_peer_check(struct context_arg *carg, X509 *cert, int allow_fetch)
{
	STACK_OF(X509) *chain;
	rev_status st = REV_OK;
	int64_t next = 0;

	if (!carg || !cert)
		return 0;
	if (!carg->rev.ocsp_enabled && !carg->rev.ocsp_stapling && !carg->rev.crl_enabled)
		return 0;

	chain = (carg->ssl) ? SSL_get_peer_cert_chain(carg->ssl) : NULL;
	if (carg->rev.ocsp_stapling && carg->ssl) {
		st = revocation_ocsp_stapled(carg->ssl, cert, chain, &carg->rev, carg->tls_ca_file, &next);
		if (st == REV_OK || st == REV_REVOKED)
			return revocation_should_fail(&carg->rev, st) ? -1 : 0;
	}
	if (carg->rev.ocsp_enabled) {
		st = revocation_ocsp_check(carg, cert, chain, &carg->rev, carg->tls_ca_file, allow_fetch, &next);
		if (revocation_should_fail(&carg->rev, st))
			return -1;
	}
	return 0;
}

rev_status revocation_ocsp_stapled(SSL *ssl, X509 *cert, STACK_OF(X509) *chain,
	const revocation_policy *pol, const char *ca_file, int64_t *next_update)
{
	const unsigned char *p = NULL;
	long len;
	X509 *issuer;
	rev_status st;

	if (!ssl || !cert || !pol || !pol->ocsp_stapling)
		return REV_UNKNOWN;

	len = SSL_get_tlsext_status_ocsp_resp(ssl, &p);
	if (len <= 0 || !p)
		return REV_UNKNOWN;

	issuer = find_issuer(cert, chain, ca_file);
	if (!issuer)
		return REV_NO_RESPONDER;
	st = ocsp_validate_response(p, (size_t)len, cert, issuer, ca_file, chain, next_update);
	X509_free(issuer);
	return st;
}
