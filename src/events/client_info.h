#pragma once
#include <uv.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "common/rtime.h"
#include "common/pcre_parser.h"
#include "config/context.h"
#include "dstructures/tommyds/tommyds/tommy.h"
#include "evt_tls.h"

typedef struct client_info
{
	struct sockaddr_in *dest;
	uv_connect_t *connect;
	uv_tcp_t *socket;
	uv_timer_t *tt_timer;
	char *key;
	void *parser_handler;
	char *mesg;
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
	size_t expect_http_length;
	uv_buf_t *buffer;
	size_t buflen;
	char *http_body;
	regex_match *rematch;
	mapping_metric *mm;

	char *tls_certificate;
	char *tls_key;
	evt_ctx_t *ctx;

	uint64_t conn_counter;
	uint64_t read_counter;

	void *data; // for parser-data

	tommy_node node;
} client_info;
