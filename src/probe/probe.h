#pragma once
#include "dstructures/tommy.h"
#include "events/context_arg.h"
#include <jansson.h>
#include <pcre.h>

#define PROBE_HIST_BUCKETS 18

typedef struct probe_qr_step
{
	char *expect;
	pcre *expect_re;
	char *send;
	char *expect_bytes;
	size_t expect_bytes_len;
	uint8_t starttls;
} probe_qr_step;

typedef struct probe_header_match
{
	char *header;
	char *regexp;
	pcre *re;
	uint8_t allow_missing;
} probe_header_match;

typedef struct probe_json_check
{
	char *path;
	char *regexp;
	pcre *re;
	uint8_t must_exist;
} probe_json_check;

typedef struct probe_node
{
	char *name;

	alligator_ht *labels;
	json_t *metricstransform;
	char *metric_name_transform_pattern;
	char *metric_name_transform_replacement;
	pcre *metric_name_transform_compiled;
	alligator_ht *env;
	uint64_t follow_redirects;
	uint8_t method;
	char *source_ip_address;
	char *body;
	char *body_file;
	char *scheme;
	char *url;
	char *query_type;
	char *query_name;
	uint8_t prober;
	char *prober_str;
	uint64_t loop;
	double percent_success;
	uint8_t tls;
	uint8_t probe_ip_version[2];
	uint64_t timeout; // milliseconds
	char *http_proxy_url;
	uint8_t unixgram;

	probe_qr_step *query_response;
	uint64_t query_response_size;

	char **valid_status_codes;
	uint64_t valid_status_codes_size;

	char **fail_if_body_matches_regexp;
	pcre **fail_if_body_matches_re;
	uint64_t fail_if_body_matches_regexp_size;

	char **fail_if_body_not_matches_regexp;
	pcre **fail_if_body_not_matches_re;
	uint64_t fail_if_body_not_matches_regexp_size;

	probe_header_match *fail_if_header_matches;
	uint64_t fail_if_header_matches_size;
	probe_header_match *fail_if_header_not_matches;
	uint64_t fail_if_header_not_matches_size;

	char **valid_rcodes;
	uint64_t valid_rcodes_size;

	char **fail_if_answer_matches_regexp;
	pcre **fail_if_answer_matches_re;
	uint64_t fail_if_answer_matches_regexp_size;

	char **fail_if_answer_not_matches_regexp;
	pcre **fail_if_answer_not_matches_re;
	uint64_t fail_if_answer_not_matches_regexp_size;

	char *payload;

	uint8_t tls_verify;
	char *ca_file;
	char *cert_file;
	char *key_file;
	char *server_name;

	uint8_t fail_if_ssl;
	uint8_t fail_if_not_ssl;
	uint8_t negative_test;

	uint32_t icmp_payload_size;
	int icmp_ttl;
	int icmp_tos;
	uint64_t icmp_interval; /* milliseconds; 0 = burst (loop) */

	probe_json_check *json_checks;
	uint64_t json_checks_size;

	tommy_node node;
} probe_node;

typedef void (*probe_qr_write_cb)(context_arg *carg, const char *data, size_t len);
typedef void (*probe_qr_starttls_cb)(context_arg *carg);

probe_node* probe_get(char *name);
void probe_del_json(json_t *probe);
void probe_push_json(json_t *probe);
probe_node* probe_from_carg(context_arg *carg);
void probe_metric_success(context_arg *carg, uint64_t val);
int probe_ip_family(const char *host, uint8_t ip_version);
void probe_copy_opts_to_carg(context_arg *carg, probe_node *pn);
int probe_qr_active(context_arg *carg);
int probe_qr_on_connect(context_arg *carg, probe_qr_write_cb wr, probe_qr_starttls_cb stls);
int probe_qr_on_read(context_arg *carg, const char *data, size_t n, probe_qr_write_cb wr, probe_qr_starttls_cb stls);
int probe_qr_on_tls_ready(context_arg *carg, probe_qr_write_cb wr, probe_qr_starttls_cb stls);
string *probe_http_request_body(probe_node *pn);
int icmp_cmsg_hop_limit(struct msghdr *msg);
int probe_http_eval_body(probe_node *pn, const char *body, size_t body_size, uint64_t *val);
int probe_dns_eval_answers(probe_node *pn, const char *answers, size_t answers_size, uint64_t *val);
int probe_udp_payload_ok(const char *reply, size_t reply_len, const char *payload, size_t payload_len);
char *probe_subst_target(const char *tmpl, const char *target);
void probe_labels_subst_target(alligator_ht *labels, const char *target);
int probe_http_eval_headers(probe_node *pn, const char *headers, uint64_t *val);
int probe_http_eval_ssl(probe_node *pn, int used_tls, uint64_t *val);
int probe_rcode_is_valid(probe_node *pn, unsigned rcode);
void probe_histogram_observe(context_arg *carg, const char *native_name, const char *alias_name, double seconds);
void probe_apply_negative(context_arg *carg, uint64_t *val);
int probe_json_validate(probe_node *pn, const char *body, size_t body_size, uint64_t *val);
const double *probe_histogram_bounds(uint8_t *n);
