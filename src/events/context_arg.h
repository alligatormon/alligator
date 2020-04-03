#pragma once
#include <uv.h>
#include "common/rtime.h"
#include "common/pcre_parser.h"
#include "metric/metrictree.h"
#include "dstructures/tommyds/tommyds/tommy.h"
#include "common/netlib.h"
#include "events/sclient3.h"

#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "common/selector.h"

typedef struct context_arg
{
	struct sockaddr_in *dest;
	uv_connect_t *connect;
	uv_tcp_t *socket;
	uv_timer_t *tt_timer;
	uv_handle_t * server;
	char *key;
	void *parser_handler;
	char *mesg;
	size_t mesg_len;
	char *hostname;
	char *port;
	r_time connect_time;
	r_time connect_time_finish;
	r_time write_time;
	r_time write_time_finish;
	r_time read_time;
	r_time read_time_finish;
	int lock;
	int proto;
	int write;
	size_t http_body_size;
	//size_t expect_http_length;
	uv_buf_t *buffer;
	size_t buflen;
	char *http_body;
	regex_match *rematch;
	mapping_metric *mm;

	// chunk body read
	string *full_body;
	//int8_t expect_json;
	uint64_t expect_body_length;
	int64_t chunked_size;
	int8_t chunked_expect;
	int8_t (*expect_function)(char *);
	uint8_t expect_count;
	uint8_t read_count;

	char *tls_certificate;
	char *tls_key;
	//evt_ctx_t *ctx;
	char *namespace;
	char *auth_basic;
	char *auth_bearer;
	network_range *net_acl;

	// for process spawn
	uv_process_options_t *options;
	uv_pipe_t *channel;
	uv_stdio_container_t *child_stdio;
	char** args;

	int8_t status; // mbedtls
	int8_t deleted; // mbedtls
	int8_t tls; //mbedtls
	uvhttp_ssl_client* tls_ctx;

	char *uvbuf;

	uint64_t conn_counter;
	uint64_t read_counter;

	tommy_hashdyn *labels;

	void *data; // for parser-data

	tommy_node node;
} context_arg;
