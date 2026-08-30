#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/pkcs12.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>
#include "common/selector.h"
#include "common/lcrypto.h"
#include "dstructures/ht.h"
#include "metric/labels.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "common/logs.h"
#include "main.h"
#define SERIAL_NUM_LEN 255
//#define	 X509_get_notBefore(x) ((x)->cert_info->validity->notBefore)
//#define	 X509_get_notAfter(x) ((x)->cert_info->validity->notAfter)
extern aconf *ac;

static inline void x509_labels_add_endpoint(context_arg *carg, alligator_ht *lbl)
{
	char endpoint[HOSTHEADER_SIZE + PORT_SIZE];

	if (!carg || !lbl || !carg->host[0])
		return;

	if (carg->port[0])
		snprintf(endpoint, sizeof(endpoint), "%s:%s", carg->host, carg->port);
	else
		strlcpy(endpoint, carg->host, sizeof(endpoint));

	labels_hash_insert_nocache(lbl, "endpoint", endpoint);
}

static inline void x509_metric_families_set(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "x509_cert_not_before", METRIC_TYPE_GAUGE, "X.509 certificate notBefore timestamp (Unix seconds).");
	namespace_metric_family_set(NULL, carg, "x509_cert_not_after", METRIC_TYPE_GAUGE, "X.509 certificate notAfter timestamp (Unix seconds).");
	namespace_metric_family_set(NULL, carg, "x509_cert_expire_days", METRIC_TYPE_GAUGE, "Whole days until X.509 certificate expiration.");
	namespace_metric_family_set(NULL, carg, "x509_cert_valid", METRIC_TYPE_GAUGE, "1 if certificate passes time/chain(/hostname for network) checks, 0 otherwise. Label reason explains invalidity.");
	namespace_metric_family_set(NULL, carg, "x509_cert_revocation_status", METRIC_TYPE_GAUGE, "1 if revocation check is not revoked. Labels: source=crl|ocsp|stapled, status=ok|revoked|unknown|error|pending.");
	namespace_metric_family_set(NULL, carg, "x509_cert_ocsp_next_update", METRIC_TYPE_GAUGE, "OCSP nextUpdate timestamp (Unix seconds).");
	namespace_metric_family_set(NULL, carg, "ocsp_requests_total", METRIC_TYPE_COUNTER, "OCSP HTTP requests.");
	namespace_metric_family_set(NULL, carg, "ocsp_request_duration_seconds_sum", METRIC_TYPE_COUNTER, "Sum of OCSP request durations in seconds.");
}

static const char *x509_verify_err_to_reason(int err)
{
	switch (err) {
		case X509_V_OK:
			return "ok";
		case X509_V_ERR_CERT_NOT_YET_VALID:
			return "not_yet_valid";
		case X509_V_ERR_CERT_HAS_EXPIRED:
		case X509_V_ERR_CRL_HAS_EXPIRED:
			return "expired";
		case X509_V_ERR_CERT_REVOKED:
			return "revoked";
		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
		case X509_V_ERR_UNABLE_TO_GET_CRL_ISSUER:
		case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
		case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
		case X509_V_ERR_CERT_UNTRUSTED:
			return "unknown_ca";
		case X509_V_ERR_HOSTNAME_MISMATCH:
		case X509_V_ERR_IP_ADDRESS_MISMATCH:
			return "hostname_mismatch";
		default:
			return "invalid_chain";
	}
}

/* Time, optional chain/CA, optional hostname (network only). reason always set. */
static int x509_cert_eval(X509 *cert, STACK_OF(X509) *untrusted,
	int have_times, uint64_t valid_from, uint64_t valid_to, uint64_t now_sec,
	const char *ca_file, int use_default_ca, const char *check_hostname,
	const revocation_policy *pol, const char **reason, rev_status *ocsp_st, int64_t *ocsp_next)
{
	if (!have_times) {
		*reason = "invalid_time";
		return 0;
	}
	if (now_sec < valid_from) {
		*reason = "not_yet_valid";
		return 0;
	}
	if (now_sec > valid_to) {
		*reason = "expired";
		return 0;
	}

	if (ca_file || use_default_ca || (pol && pol->crl_enabled)) {
		X509_STORE *store = X509_STORE_new();
		X509_STORE_CTX *ctx = X509_STORE_CTX_new();
		int ok = 0;
		int err = X509_V_ERR_UNSPECIFIED;

		if (!store || !ctx) {
			*reason = "invalid_chain";
			X509_STORE_CTX_free(ctx);
			X509_STORE_free(store);
			return 0;
		}

		if (ca_file) {
			if (X509_STORE_load_locations(store, ca_file, NULL) != 1) {
				*reason = "unknown_ca";
				X509_STORE_CTX_free(ctx);
				X509_STORE_free(store);
				return 0;
			}
		} else if ((use_default_ca || (pol && pol->crl_enabled)) && X509_STORE_set_default_paths(store) != 1) {
			*reason = "unknown_ca";
			X509_STORE_CTX_free(ctx);
			X509_STORE_free(store);
			return 0;
		}

		if (pol)
			revocation_store_apply_crl(store, pol);

		if (X509_STORE_CTX_init(ctx, store, cert, untrusted) == 1) {
			ok = X509_verify_cert(ctx) == 1;
			err = X509_STORE_CTX_get_error(ctx);
		}

		X509_STORE_CTX_free(ctx);
		X509_STORE_free(store);

		if (!ok) {
			*reason = x509_verify_err_to_reason(err);
			return 0;
		}
	}

	if (check_hostname && *check_hostname) {
		/* 1 = match, 0 = no match, negative = error. Try DNS then IP. */
		int host_ok = X509_check_host(cert, check_hostname, 0, 0, NULL);
		if (host_ok != 1)
			host_ok = X509_check_ip_asc(cert, check_hostname, 0);
		if (host_ok != 1) {
			*reason = "hostname_mismatch";
			return 0;
		}
	}

	if (pol && pol->ocsp_enabled) {
		int allow_fetch = pol->fetch == REV_FETCH_INLINE;
		rev_status st = revocation_ocsp_check(NULL, cert, untrusted, pol, ca_file, allow_fetch, ocsp_next);
		if (ocsp_st)
			*ocsp_st = st;
		if (st == REV_REVOKED) {
			*reason = "revoked";
			return 0;
		}
		if (revocation_should_fail(pol, st) && st != REV_PENDING) {
			*reason = "invalid_chain";
			return 0;
		}
	} else if (ocsp_st) {
		*ocsp_st = REV_OK;
	}

	*reason = "ok";
	return 1;
}

alligator_ht* pem_parse(char *cert, char *dn_subject, size_t dn_subject_size)
{
	char country_name[1000];
	char county[1000];
	char organization_name[1000];
	char organization_unit[1000];
	char common_name[1000];

	alligator_ht *lbl = alligator_ht_init(NULL);

	for(int i=0; i<dn_subject_size;)
	{
		if (!strncmp(dn_subject+i, "C=", 2))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", /");
			size_t copy_size = size < (sizeof(country_name) - 1) ? size : (sizeof(country_name) - 1);
			strlcpy(country_name, dn_subject+i, copy_size + 1);
			glog(L_DEBUG, "cert: %s, country=%s\n", cert, country_name);
			labels_hash_insert_nocache(lbl, "country", country_name);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "ST=", 3))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", /");
			size_t copy_size = size < (sizeof(county) - 1) ? size : (sizeof(county) - 1);
			strlcpy(county, dn_subject+i, copy_size + 1);
			glog(L_DEBUG, "cert: %s, state=%s\n", cert, county);
			labels_hash_insert_nocache(lbl, "county", county);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "O=", 2))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", /");
			size_t copy_size = size < (sizeof(organization_name) - 1) ? size : (sizeof(organization_name) - 1);
			strlcpy(organization_name, dn_subject+i, copy_size + 1);
			glog(L_DEBUG, "cert: %s, organization_name=%s\n", cert, organization_name);
			labels_hash_insert_nocache(lbl, "organization_name", organization_name);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "OU=", 3))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", /");
			size_t copy_size = size < (sizeof(organization_unit) - 1) ? size : (sizeof(organization_unit) - 1);
			strlcpy(organization_unit, dn_subject+i, copy_size + 1);
			glog(L_DEBUG, "cert: %s, organization_unit=%s\n", cert, organization_unit);
			labels_hash_insert_nocache(lbl, "organization_unit", organization_unit);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "CN=", 3))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", /");
			size_t copy_size = size < (sizeof(common_name) - 1) ? size : (sizeof(common_name) - 1);
			strlcpy(common_name, dn_subject+i, copy_size + 1);
			glog(L_DEBUG, "cert: %s, common_name=%s\n", cert, common_name);
			labels_hash_insert_nocache(lbl, "common_name", common_name);
			i += size;
		}
		i += strcspn(dn_subject+i, ",/");
		i += strspn(dn_subject+i, ", /\t");
	}

	return lbl;
}

string *pem_get_serial_number(X509 *cert)
{
	string *serial_number = string_init(SERIAL_NUM_LEN);
	
	ASN1_INTEGER *serial = X509_get_serialNumber(cert);
	
	BIGNUM *bn = ASN1_INTEGER_to_BN(serial, NULL);
	if (!bn) {
		glog(L_ERROR, "unable to convert ASN1INTEGER to BN\n");
		return NULL;
	}
	
	char *tmp = BN_bn2hex(bn);
	if (!tmp) {
		glog(L_ERROR, "unable to convert BN to decimal string.\n");
		BN_free(bn);
		return NULL;
	}
	
	string_cat(serial_number, tmp, strlen(tmp));
	BN_free(bn);
	OPENSSL_free(tmp);

	return serial_number;
}


static time_t ASN1_GetTimeT(ASN1_TIME* time){
	struct tm t;
	const char* str = (const char*) time->data;
	size_t i = 0;

	memset(&t, 0, sizeof(t));

	if (time->type == V_ASN1_UTCTIME) {/* two digit year */
		t.tm_year = (str[i++] - '0') * 10;
		t.tm_year += (str[i++] - '0');
		if (t.tm_year < 70)
			t.tm_year += 100;
	} else if (time->type == V_ASN1_GENERALIZEDTIME) {/* four digit year */
		t.tm_year = (str[i++] - '0') * 1000;
		t.tm_year+= (str[i++] - '0') * 100;
		t.tm_year+= (str[i++] - '0') * 10;
		t.tm_year+= (str[i++] - '0');
		t.tm_year -= 1900;
	}
	t.tm_mon  = (str[i++] - '0') * 10;
	t.tm_mon += (str[i++] - '0') - 1; // -1 since January is 0 not 1.
	t.tm_mday = (str[i++] - '0') * 10;
	t.tm_mday+= (str[i++] - '0');
	t.tm_hour = (str[i++] - '0') * 10;
	t.tm_hour+= (str[i++] - '0');
	t.tm_min  = (str[i++] - '0') * 10;
	t.tm_min += (str[i++] - '0');
	t.tm_sec  = (str[i++] - '0') * 10;
	t.tm_sec += (str[i++] - '0');

	/* Note: we did not adjust the time based on time zone information */
	return mktime(&t);
}

void pem_create_metric(alligator_ht *lbl, char *cert, char *dn_subject, char *dn_issuer, char *serial, int64_t valid_from, int64_t valid_to, X509 *x509, STACK_OF(X509) *untrusted, const char *ca_file, const revocation_policy *pol)
{
	x509_metric_families_set(NULL);

	labels_hash_insert_nocache(lbl, "issuer", dn_issuer);
	glog(L_DEBUG, "cert: %s, issuer: %s\n", cert, dn_issuer);

	labels_hash_insert_nocache(lbl, "serial", serial);
	glog(L_DEBUG, "cert: %s, serial: %s\n", cert, serial);

	r_time now = setrtime();

	int64_t expdays =  ((int64_t)valid_to-(int64_t)now.sec)/86400;
	const char *reason = "ok";
	rev_status ocsp_st = REV_OK;
	int64_t ocsp_next = 0;
	int64_t is_valid = x509_cert_eval(x509, untrusted, 1, (uint64_t)valid_from, (uint64_t)valid_to, now.sec,
		ca_file, 0, NULL, pol, &reason, &ocsp_st, &ocsp_next);
	glog(L_DEBUG, "cert: %s, certsubject: %s\n", cert, dn_subject);
	glog(L_DEBUG, "cert: %s, complete for: %u.\n", cert, now.sec);
	glog(L_DEBUG, "cert: %s, valid from: %"d64".\n", cert, valid_from);
	glog(L_DEBUG, "cert: %s, %"d64" exp\n", cert, expdays);
	glog(L_DEBUG, "cert: %s, valid: %"d64" reason=%s\n", cert, is_valid, reason);
	alligator_ht *notafter_lbl = labels_dup(lbl);
	alligator_ht *expiredays_lbl = labels_dup(lbl);
	alligator_ht *valid_lbl = labels_dup(lbl);
	labels_hash_insert_nocache(valid_lbl, "reason", (char *)reason);
	metric_add("x509_cert_not_before", lbl, &valid_from, DATATYPE_INT, NULL);
	metric_add("x509_cert_not_after", notafter_lbl, &valid_to, DATATYPE_INT, NULL);
	metric_add("x509_cert_expire_days", expiredays_lbl, &expdays, DATATYPE_INT, NULL);
	if (pol && (pol->crl_enabled || pol->ocsp_enabled)) {
		alligator_ht *rev_lbl = labels_dup(valid_lbl);
		int64_t rev_val = (ocsp_st == REV_REVOKED) ? 0 : 1;
		labels_hash_insert_nocache(rev_lbl, "source", pol->ocsp_enabled ? "ocsp" : "crl");
		labels_hash_insert_nocache(rev_lbl, "status", (char *)rev_status_str(ocsp_st));
		metric_add("x509_cert_revocation_status", rev_lbl, &rev_val, DATATYPE_INT, NULL);
		if (ocsp_next) {
			alligator_ht *next_lbl = labels_dup(valid_lbl);
			metric_add("x509_cert_ocsp_next_update", next_lbl, &ocsp_next, DATATYPE_INT, NULL);
		}
	}
	metric_add("x509_cert_valid", valid_lbl, &is_valid, DATATYPE_INT, NULL);
}

void libcrypto_p12_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename)
{
	//printf("pem check '%s'\n", pem_cert);
	++cert_size;
	x509_parse_fctx *fctx = data;
	char *password = fctx ? fctx->password : NULL;
	const char *ca_file = fctx ? fctx->ca_file : NULL;
	const revocation_policy *pol = fctx ? fctx->pol : NULL;
	EVP_PKEY *pkey;
	X509 *cert;
	STACK_OF(X509) *ca = NULL;
	PKCS12 *p12;
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();
	BIO *fd_bio = BIO_new_mem_buf((void*)pem_cert, cert_size);
	p12 = d2i_PKCS12_bio(fd_bio, NULL);
	if (!p12) {
		char errbuf[256];
		ERR_error_string_n(ERR_peek_last_error(), errbuf, sizeof(errbuf));
		glog(L_ERROR, "Error reading PKCS#12 file %s: %s\n", filename ? filename : "(null)", errbuf);
		BIO_free(fd_bio);
		return;
	}
	if (!PKCS12_parse(p12, password, &pkey, &cert, &ca)) {
		char errbuf[256];
		ERR_error_string_n(ERR_peek_last_error(), errbuf, sizeof(errbuf));
		glog(L_ERROR, "Error parsing PKCS#12 file %s: %s\n", filename ? filename : "(null)", errbuf);
		PKCS12_free(p12);
		BIO_free(fd_bio);
		return;
	}

	char *subj = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
	char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
	alligator_ht *lbl = pem_parse(filename, subj + 1, strlen(subj) - 1);

	string *serial = pem_get_serial_number(cert);

	time_t not_after = ASN1_GetTimeT(X509_get_notAfter(cert));
	time_t not_before = ASN1_GetTimeT(X509_get_notBefore(cert));

	pem_create_metric(lbl, filename, subj, issuer, serial->s, not_before, not_after, cert, ca, ca_file, pol);

	string_free(serial);
	free(subj);
	free(issuer);

	PKCS12_free(p12);

	sk_X509_pop_free(ca, X509_free);
	X509_free(cert);
	EVP_PKEY_free(pkey);
}

// PEM
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
int x509_parse_cert(context_arg *carg, X509 *cert, char *cert_name, char *target,
	const char *ca_file, const char *check_hostname, const revocation_policy *pol) {
	if (!cert)
		return 0;
	X509_NAME *subject = X509_get_subject_name(cert);
	char common_name[256] = { 0 };
	X509_NAME_get_text_by_NID(subject, NID_commonName, common_name, sizeof(common_name));

	alligator_ht *lbl = calloc(1, sizeof(*lbl));
	alligator_ht_init(lbl);
	labels_hash_insert_nocache(lbl, "cert", cert_name);
	if (target)
		labels_hash_insert_nocache(lbl, "target", target);
	x509_labels_add_endpoint(carg, lbl);

	labels_hash_insert_nocache(lbl, "common_name", common_name);
	carg_or_glog(carg, L_DEBUG, "cert: %s, common_name=%s\n", cert_name, common_name);

	X509_NAME *issuer = X509_get_issuer_name(cert);
	char issuer_str[256];
	X509_NAME_oneline(issuer, issuer_str, sizeof(issuer_str));
	labels_hash_insert_nocache(lbl, "issuer", issuer_str);
	carg_or_glog(carg, L_DEBUG, "cert: %s, issuer: %s\n", cert_name, issuer_str);

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
				carg_or_glog(carg, L_DEBUG, "cert: %s, san: %s\n", cert_name, dns);
			}
		}
		sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
	}

	ASN1_INTEGER *serial = X509_get_serialNumber(cert);
	BIGNUM *bn = ASN1_INTEGER_to_BN(serial, NULL);
	char *serial_str = BN_bn2hex(bn);
	labels_hash_insert_nocache(lbl, "serial", serial_str);
	carg_or_glog(carg, L_DEBUG, "cert: %s, serial: %s\n", cert_name, serial_str);

	char buffer[256];
    *buffer = 0;
	if (X509_NAME_get_text_by_NID(subject, NID_countryName, buffer, sizeof(buffer)) > 0) {
		labels_hash_insert_nocache(lbl, "country", buffer);
		carg_or_glog(carg, L_DEBUG, "cert: %s, country: %s\n", cert_name, buffer);
	}
    *buffer = 0;
	if (X509_NAME_get_text_by_NID(subject, NID_stateOrProvinceName, buffer, sizeof(buffer)) > 0) {
		labels_hash_insert_nocache(lbl, "county", buffer);
		carg_or_glog(carg, L_DEBUG, "cert: %s, state: %s\n", cert_name, buffer);
	}
    *buffer = 0;
	if (X509_NAME_get_text_by_NID(subject, NID_organizationName, buffer, sizeof(buffer)) > 0) {
		labels_hash_insert_nocache(lbl, "organization_unit", buffer);
		carg_or_glog(carg, L_DEBUG, "cert: %s, organization: %s\n", cert_name, buffer);
	}
    *buffer = 0;
	if (X509_NAME_get_text_by_NID(subject, NID_organizationalUnitName, buffer, sizeof(buffer)) > 0) {
		labels_hash_insert_nocache(lbl, "organization_name", buffer);
		carg_or_glog(carg, L_DEBUG, "cert: %s, organization_unit: %s\n", cert_name, buffer);
	}

	STACK_OF(X509) *untrusted = NULL;
	if (carg && carg->ssl)
		untrusted = SSL_get_peer_cert_chain(carg->ssl);

	/* Network (hostname check requested): use system CA when ca_file omitted.
	 * Filesystem: chain verify only when ca_file is set. */
	int use_default_ca = (check_hostname != NULL) && (ca_file == NULL);

	const ASN1_TIME *notBefore = X509_get0_notBefore(cert);
	const ASN1_TIME *notAfter  = X509_get0_notAfter(cert);
	uint64_t valid_from = 0, valid_to = 0;
	int have_times = asn1_time_to_uint64(notBefore, &valid_from) && asn1_time_to_uint64(notAfter, &valid_to);
	x509_metric_families_set(carg);

	r_time now = setrtime();
	const char *reason = "ok";
	rev_status ocsp_st = REV_OK;
	int64_t ocsp_next = 0;
	int64_t is_valid = x509_cert_eval(cert, untrusted, have_times, valid_from, valid_to, now.sec,
		ca_file, use_default_ca, check_hostname, pol, &reason, &ocsp_st, &ocsp_next);

	if (have_times) {
		int64_t expdays =  ((int64_t)valid_to-(int64_t)now.sec)/86400;
		carg_or_glog(carg, L_DEBUG, "cert: %s, complete for: %u.\n", cert_name, now.sec);
		carg_or_glog(carg, L_DEBUG, "cert: %s, valid from: %"d64".\n", cert_name, valid_from);
		carg_or_glog(carg, L_DEBUG, "cert: %s, %"d64" exp\n", cert_name, expdays);
		carg_or_glog(carg, L_DEBUG, "cert: %s, version: %d\n", cert_name, X509_get_version(cert) + 1);
		carg_or_glog(carg, L_DEBUG, "cert: %s, valid: %"d64" reason=%s\n", cert_name, is_valid, reason);
		alligator_ht *notafter_lbl = labels_dup(lbl);
		alligator_ht *expiredays_lbl = labels_dup(lbl);
		alligator_ht *valid_lbl = labels_dup(lbl);
		labels_hash_insert_nocache(valid_lbl, "reason", (char *)reason);
		if (pol && (pol->crl_enabled || pol->ocsp_enabled)) {
			alligator_ht *rev_lbl = labels_dup(valid_lbl);
			int64_t rev_val = (ocsp_st == REV_REVOKED) ? 0 : 1;
			labels_hash_insert_nocache(rev_lbl, "source", pol->ocsp_enabled ? "ocsp" : "crl");
			labels_hash_insert_nocache(rev_lbl, "status", (char *)rev_status_str(ocsp_st));
			metric_add("x509_cert_revocation_status", rev_lbl, &rev_val, DATATYPE_INT, carg);
			if (ocsp_next) {
				alligator_ht *next_lbl = labels_dup(lbl);
				metric_add("x509_cert_ocsp_next_update", next_lbl, &ocsp_next, DATATYPE_INT, carg);
			}
		}
		metric_add("x509_cert_not_before", lbl, &valid_from, DATATYPE_INT, carg);
		metric_add("x509_cert_not_after", notafter_lbl, &valid_to, DATATYPE_INT, carg);
		metric_add("x509_cert_expire_days", expiredays_lbl, &expdays, DATATYPE_INT, carg);
		metric_add("x509_cert_valid", valid_lbl, &is_valid, DATATYPE_INT, carg);
	}
	else {
		carg_or_glog(carg, L_ERROR, "Failed to parse ASN1_TIME in cert: %s\n", cert_name);
		labels_hash_insert_nocache(lbl, "reason", (char *)reason);
		metric_add("x509_cert_valid", lbl, &is_valid, DATATYPE_INT, carg);
	}

	BN_free(bn);
	OPENSSL_free(serial_str);
	return (int)is_valid;
}

int libcrypto_pem_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename) {
	x509_parse_fctx *fctx = data;
	const char *ca_file = fctx ? fctx->ca_file : NULL;
	const revocation_policy *pol = fctx ? fctx->pol : NULL;
	if (!pem_cert || !cert_size)
		return 0;

	BIO *bio = BIO_new_mem_buf(pem_cert, (int)cert_size);
	if (!bio) {
		glog(L_ERROR, "filename: %s, BIO_new_mem_buf failed\n", filename ? filename : "(null)");
		return 0;
	}

	X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	BIO_free(bio);

	if (!cert) {
		glog(L_ERROR, "filename: %s, PEM_read_bio_X509 failed\n", filename ? filename : "(null)");
        return 0;
	}

	x509_parse_cert(NULL, cert, NULL, filename, ca_file, NULL, pol);
    X509_free(cert);
    return 1;
}
