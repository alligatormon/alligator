#pragma once
#include "dstructures/tommy.h"
#include <jansson.h>

typedef struct probe_node
{
	char *name;

	alligator_ht *labels;
	alligator_ht *env;
	uint64_t follow_redirects;
	//char *compression;
	uint8_t method;
	//uint64_t *valid_http_versions;
	char *source_ip_address;
	char *body;
	char *scheme;
	uint8_t prober;
	char *prober_str;
	uint64_t loop;
	double percent_success;
	uint8_t tls;
	uint8_t probe_ip_version[2];
	uint64_t timeout; // milliseconds
	char *http_proxy_url;

	//char **send_expect;
	//uint64_t send_expect_size;

	char **valid_status_codes;
	uint64_t valid_status_codes_size;

	char **fail_if_body_matches_regexp;
	uint64_t fail_if_body_matches_regexp_size;

	char **fail_if_body_not_matches_regexp;
	uint64_t fail_if_body_not_matches_regexp_size;

	uint8_t tls_verify;
	char *ca_file;
	char *cert_file;
	char *key_file;
	char *server_name;

	//uint8_t fail_if_ssl;
	//uint8_t fail_if_not_ssl;

	tommy_node node;
} probe_node;

probe_node* probe_get(char *name);
void probe_del_json(json_t *probe);
void probe_push_json(json_t *probe);
