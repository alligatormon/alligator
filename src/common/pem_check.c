#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include "common/rtime.h"
#include "main.h"

int64_t cert_get_expdays(X509 *cert)
{
	ASN1_TIME *notAfter = X509_get_notAfter(cert);

	struct tm tm_time = {0};
	int len = 32;
	char buf2[len];
	if( notAfter && notAfter->data && notAfter->type == V_ASN1_UTCTIME)
	{
		printf("notAfter->data %s\n", notAfter->data);
		strptime((const char*)notAfter->data, "%y%m%d%H%M%SZ" , &tm_time);
		strftime(buf2, len, "%s", &tm_time);
	}

	//printf("certificate: %s\n", subj);
	//printf("\tissuer: %s\n", issuer);
	r_time now = setrtime();
	
	int64_t buftime = atoll(buf2);
	//printf("complete for: %u.\n",now.sec);
	int64_t expdays = (buftime-now.sec)/86400; 
	printf("%"d64" exp\n", buftime);
	return expdays;
}

void cert_check_file(char *cert_filestr)
{
	BIO	*certbio = NULL;
	BIO	*outbio = NULL;
	X509	*error_cert = NULL;
	X509	*cert = NULL;
	X509_NAME	*certsubject = NULL;
	X509_STORE	*store = NULL;
	X509_STORE_CTX	*vrfy_ctx = NULL;
	int ret;

	OpenSSL_add_all_algorithms();
	ERR_load_BIO_strings();
	ERR_load_crypto_strings();

	certbio = BIO_new(BIO_s_file());
	outbio	= BIO_new_fp(stdout, BIO_NOCLOSE);

	if (!(store=X509_STORE_new()))
		 BIO_printf(outbio, "Error creating X509_STORE_CTX object\n");

	vrfy_ctx = X509_STORE_CTX_new();

	ret = BIO_read_filename(certbio, cert_filestr);
	if (! (cert = PEM_read_bio_X509(certbio, NULL, 0, NULL)))
	{
		BIO_printf(outbio, "Error loading cert into memory\n");
		return;
	}



	int64_t expdays = cert_get_expdays(cert);
	printf("ed = %"d64"\n", expdays);
	metric_add_labels("cert_expiration_days", &expdays, DATATYPE_INT, 0, "file", cert_filestr);


	X509_STORE_CTX_init(vrfy_ctx, store, cert, NULL);

	ret = X509_verify_cert(vrfy_ctx);
	BIO_printf(outbio, "Verification return code: %d\n", ret);

	if(ret == 0 || ret == 1)
		BIO_printf(outbio, "Verification result text: %s\n", X509_verify_cert_error_string(vrfy_ctx->error));

	if(ret == 0) {
		error_cert = X509_STORE_CTX_get_current_cert(vrfy_ctx);
		certsubject = X509_NAME_new();
		certsubject = X509_get_subject_name(error_cert);
		BIO_printf(outbio, "Verification failed cert:\n");
		X509_NAME_print_ex(outbio, certsubject, 0, XN_FLAG_MULTILINE);
		BIO_printf(outbio, "\n");
	}

	X509_STORE_CTX_free(vrfy_ctx);
	X509_STORE_free(store);
	X509_free(cert);
	BIO_free_all(certbio);
	BIO_free_all(outbio);
}
