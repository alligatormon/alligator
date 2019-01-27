#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <fcntl.h>
#include "rtime.h"
#include "dstructures/metric.h"
#include "dstructures/tommy.h"
#include "https_tls_check.h"
#include "main.h"

#include <sys/types.h>
//#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX_LENGTH 100

#define FAIL	-1


#ifndef _WIN32
#include <sys/socket.h>
int connect_w_to(const char* hostname, int port) { 
	int res, valopt; 
	struct sockaddr_in addr; 
	long arg; 
	fd_set myset; 
	struct timeval tv; 
	socklen_t lon; 
	struct hostent *host;

	int soc = socket(AF_INET, SOCK_STREAM, 0); 

	arg = fcntl(soc, F_GETFL, NULL); 
	arg |= O_NONBLOCK; 
	fcntl(soc, F_SETFL, arg); 
	//addr.sin_family = AF_INET; 
	//addr.sin_port = htons(2000); 
	//addr.sin_addr.s_addr = inet_addr("192.168.0.1"); 

	if ( (host = gethostbyname(hostname)) == NULL )
	{
		return 0;
	}
	//soc = socket(PF_INET, SOCK_STREAM, 0);
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = *(long*)(host->h_addr);


	res = connect(soc, (struct sockaddr *)&addr, sizeof(addr)); 

	if (res < 0) { 
		 if (errno == EINPROGRESS) { 
				tv.tv_sec = 1; 
				tv.tv_usec = 0; 
				FD_ZERO(&myset); 
				FD_SET(soc, &myset); 
				if (select(soc+1, NULL, &myset, NULL, &tv) > 0) { 
					 lon = sizeof(int); 
					 getsockopt(soc, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon); 
					 if (valopt) { 
							///fprintf(stderr, "Error in connection() %d - %s\n", valopt, strerror(valopt)); 
							return 0; 
					 } 
				} 
				else { 
					 //fprintf(stderr, "Timeout or error() %d - %s\n", valopt, strerror(valopt)); 
					 return 0; 
				} 
		 } 
		 else { 
				//fprintf(stderr, "Error connecting %d - %s\n", errno, strerror(errno)); 
				return 0; 
		 } 
	} 
	arg = fcntl(soc, F_GETFL, NULL); 
	arg &= (~O_NONBLOCK); 
	fcntl(soc, F_SETFL, arg); 
	return soc;
}

int OpenConnection(const char *hostname, int port)
{
	int sd;
	struct hostent *host;
	struct sockaddr_in addr;

	if ( (host = gethostbyname(hostname)) == NULL )
	{
		return 0;
	}
	sd = socket(PF_INET, SOCK_STREAM, 0);
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = *(long*)(host->h_addr);
	if ( connect(sd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
	{
		close(sd);
		//perror(hostname);
		return 0;
	}
	return sd;
}

SSL_CTX* InitCTX(void)
{
	const SSL_METHOD *method;
	SSL_CTX *ctx;

	OpenSSL_add_all_algorithms();	/* Load cryptos, et.al. */
	SSL_load_error_strings();	 /* Bring in and register error messages */
	method = TLSv1_2_client_method();	/* Create new client-method instance */
	ctx = SSL_CTX_new(method);	 /* Create new context */
	if ( ctx == NULL )
	{
		ERR_print_errors_fp(stderr);
		return NULL;
	}
	return ctx;
}

void print_certificate(X509* cert, const char *hostname) {
	//char subj[MAX_LENGTH+1];
	//char issuer[MAX_LENGTH+1];
	//X509_NAME_oneline(X509_get_subject_name(cert), subj, MAX_LENGTH);
	//X509_NAME_oneline(X509_get_issuer_name(cert), issuer, MAX_LENGTH);
	ASN1_TIME *notAfter = X509_get_notAfter(cert);

	struct tm tm_time;
	int len = 32;
	char buf2[len];
	if( notAfter && notAfter->data && notAfter->type == V_ASN1_UTCTIME)
	{
		strptime((const char*)notAfter->data, "%y%m%d%H%M%SZ" , &tm_time);
		strftime(buf2, sizeof(char) * len, "%s", &tm_time);
	}

	//printf("certificate: %s\n", subj);
	//printf("\tissuer: %s\n", issuer);
	r_time now = setrtime();

	int64_t buftime = atoll(buf2);
	//printf("complete for: %u.\n",now.sec);
	int64_t expdays = (buftime-now.sec)/86400;
	metric_labels_add_lbl("https_tls_expiration_days", &expdays, ALLIGATOR_DATATYPE_INT, 0, "hostname", hostname);
}

void ShowCerts(SSL* ssl, const char *hostname)
{
	X509 *cert;

	cert = SSL_get_peer_certificate(ssl);
	if ( cert != NULL )
	{
		print_certificate(cert, hostname);

		//printf("Server certificates:\n");
		//line = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
		//printf("Subject: %s\n", line);
		//free(line);		 /* free the malloc'ed string */
		//line = X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0);
		//printf("Issuer: %s\n", line);
		//free(line);		 /* free the malloc'ed string */
		X509_free(cert);	 /* free the malloc'ed certificate copy */
	}
	//else
		//printf("Info: No client certificates configured.\n");
}

void check_https_cert(const char* hostname)
{
	SSL_CTX *ctx;
	int server;
	SSL *ssl;

	SSL_library_init();

	ctx = InitCTX();
	SSL_CTX_load_verify_locations(ctx, "/etc/pki/tls/cert.pem", NULL);
	//server = OpenConnection(hostname, 443);
	server = connect_w_to(hostname, 443);
	if (!server)
	{
		SSL_CTX_free(ctx);
		return;
	}
	ssl = SSL_new(ctx);
	SSL_set_tlsext_host_name(ssl, hostname);
	SSL_set_fd(ssl, server);
	//printf("hostname is '%s'(%zu)\n", hostname, strlen(hostname));
	int rc = SSL_connect(ssl);
	if ( rc == FAIL ) {}
		//ERR_print_errors_fp(stderr);
	else
	{
		ShowCerts(ssl, hostname);
		//printf("Connected with %s encryption\n", SSL_get_cipher(ssl));
		//SSL_write(ssl, msg, strlen(msg));	 /* encrypt & send message */
		//bytes = SSL_read(ssl, buf, sizeof(buf)); /* get reply & decrypt */
		//buf[bytes] = 0;
		//printf("Received: \"%s\"\n", buf);
	}
	SSL_free(ssl);
	close(server);
	SSL_CTX_free(ctx);
}
#endif

int domainname_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((https_ssl_check_node*)obj)->domainname;
	return strcmp(s1, s2);
}

void https_ssl_check_push(char *hostname)
{
	size_t len = strlen(hostname);
	if ( len < 5 )
		return;
	if ( !validate_domainname(hostname, len) )
		return;
	//printf("add to ssl check %s\n", hostname);
	extern aconf *ac;
	https_ssl_check_node *node = malloc(sizeof(*node));
	node->domainname = strdup(hostname);
	node->len = len;

	https_ssl_check_node *node_search = tommy_hashdyn_search(ac->https_ssl_domains, domainname_compare, node->domainname, tommy_strhash_u32(0, node->domainname));
	if (!node_search)
	{
		//printf("adding '%s'\n", node->domainname);
		tommy_hashdyn_insert(ac->https_ssl_domains, &(node->node), node, tommy_strhash_u32(0, node->domainname));
	}
	else
	{
		free(node->domainname);
		free(node);
	}
}

void https_domain_load(char *file)
{
	FILE *fd = fopen (file, "r");
	if (!fd)
		return;

	char domain[255];
	while (fgets(domain, 255, fd))
	{
		domain[strlen(domain)-1] = '\0';
		https_ssl_check_push(domain);
	}

	fclose(fd);
}

void save_domains_forearch(void *obj1, void* arg)
{
	https_ssl_check_node *obj = arg;
	FILE *fd = (FILE*)obj1;
	if ( obj && obj->domainname )
	{
		fprintf(fd, "%s\n", obj->domainname);
	}
}

void https_domain_save(char *file)
{
	FILE *fd = fopen (file, "w");
	if (!fd)
		return;

	extern aconf *ac;
	tommy_hashdyn_foreach_arg(ac->https_ssl_domains, save_domains_forearch, fd);

	fclose(fd);
}
