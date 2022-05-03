#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/pkcs12.h>
#include "common/selector.h"
#include "dstructures/ht.h"
#include "metric/labels.h"
#include "main.h"
#define SERIAL_NUM_LEN 255
//#define	 X509_get_notBefore(x) ((x)->cert_info->validity->notBefore)
//#define	 X509_get_notAfter(x) ((x)->cert_info->validity->notAfter)
extern aconf *ac;

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
			strlcpy(country_name, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, country=%s\n", cert, country_name);
			labels_hash_insert_nocache(lbl, "country", country_name);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "ST=", 3))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", /");
			strlcpy(county, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, county=%s\n", cert, county);
			labels_hash_insert_nocache(lbl, "county", county);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "O=", 2))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", /");
			strlcpy(organization_name, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, organization_name=%s\n", cert, organization_name);
			labels_hash_insert_nocache(lbl, "organization_name", organization_name);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "OU=", 3))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", /");
			strlcpy(organization_unit, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, organization_unit=%s\n", cert, organization_unit);
			labels_hash_insert_nocache(lbl, "organization_unit", organization_unit);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "CN=", 3))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", /");
			strlcpy(common_name, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, common_name=%s\n", cert, common_name);
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
		fprintf(stderr, "unable to convert ASN1INTEGER to BN\n");
		return NULL;
	}
	
	char *tmp = BN_bn2hex(bn);
	if (!tmp) {
		fprintf(stderr, "unable to convert BN to decimal string.\n");
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

void pem_create_metric(alligator_ht *lbl, char *cert, char *dn_subject, char *dn_issuer, char *serial, int64_t valid_from, int64_t valid_to)
{
	labels_hash_insert_nocache(lbl, "issuer", dn_issuer);
	if (ac->log_level > 2)
		printf("cert: %s, issuer: %s\n", cert, dn_issuer);

	labels_hash_insert_nocache(lbl, "serial", serial);
	if (ac->log_level > 2)
		printf("cert: %s, serial: %s\n", cert, serial);

	r_time now = setrtime();

	int64_t expdays =  (valid_to-now.sec)/86400; 
	if (ac->log_level > 2)
	{
		printf("cert: %s, certsubject: %s\n", cert, dn_subject);
		printf("cert: %s, complete for: %u.\n", cert, now.sec);
		printf("cert: %s, valid from: %"d64".\n", cert, valid_from);
		printf("cert: %s, %"d64" exp\n", cert, expdays);
	}
	alligator_ht *notafter_lbl = labels_dup(lbl);
	alligator_ht *expiredays_lbl = labels_dup(lbl);
	metric_add("x509_cert_not_before", lbl, &valid_from, DATATYPE_INT, NULL);
	metric_add("x509_cert_not_after", notafter_lbl, &valid_to, DATATYPE_INT, NULL);
	metric_add("x509_cert_expire_days", expiredays_lbl, &expdays, DATATYPE_INT, NULL);
}

void libcrypto_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename)
{
	//printf("pem check '%s'\n", pem_cert);
	++cert_size;
	char *password = data;
	EVP_PKEY *pkey;
	X509 *cert;
	STACK_OF(X509) *ca = NULL;
	PKCS12 *p12;
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();
	BIO *fd_bio = BIO_new_mem_buf((void*)pem_cert, cert_size);
	p12 = d2i_PKCS12_bio(fd_bio, NULL);
	if (!p12) {
		if (ac->log_level > 0)
		{
			fprintf(stderr, "Error reading PKCS#12 file\n");
			ERR_print_errors_fp(stderr);
		}
		return;
	}
	if (!PKCS12_parse(p12, password, &pkey, &cert, &ca)) {
		if (ac->log_level > 0)
		{
			fprintf(stderr, "Error parsing PKCS#12 file\n");
			ERR_print_errors_fp(stderr);
		}
		return;
	}

	char *subj = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
	char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
	alligator_ht *lbl = pem_parse(filename, subj + 1, strlen(subj) - 1);

	string *serial = pem_get_serial_number(cert);

	time_t not_after = ASN1_GetTimeT(X509_get_notAfter(cert));
	time_t not_before = ASN1_GetTimeT(X509_get_notBefore(cert));

	pem_create_metric(lbl, filename, subj, issuer, serial->s, not_before, not_after);

	string_free(serial);
	free(subj);
	free(issuer);

	PKCS12_free(p12);

	sk_X509_pop_free(ca, X509_free);
	X509_free(cert);
	EVP_PKEY_free(pkey);
}

