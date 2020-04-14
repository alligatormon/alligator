#pragma once
#define PORT_SIZE 6
#define URL_SIZE 1024
#define EVENT_BUFFER 65536
#include <uv.h>
#include "common/rtime.h"
#include "common/pcre_parser.h"
#include "metric/metrictree.h"
#include "dstructures/tommyds/tommyds/tommy.h"
#include "common/netlib.h"

#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "common/selector.h"

typedef struct context_arg
{
	struct sockaddr_in *dest;
	//uv_connect_t *connect;
	uv_tcp_t *socket;
	uv_timer_t *tt_timer;
	//uv_handle_t * server;
	char *key;
	void *parser_handler;
	char *mesg;
	size_t mesg_len;
	char *hostname;
	//char *port;
	r_time connect_time;
	r_time connect_time_finish;
	r_time write_time;
	r_time write_time_finish;
	r_time read_time;
	r_time read_time_finish;
	uint8_t lock;
	uint8_t proto;
	uint8_t transport;
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
	int64_t chunked_expect;
	int8_t (*expect_function)(char *, size_t);
	uint8_t expect_count;
	uint8_t read_count;

	char *namespace;
	char *auth_basic;
	char *auth_bearer;
	network_range *net_acl;

	// for process spawn
	uv_process_options_t *options;
	uv_pipe_t *channel;
	uv_stdio_container_t *child_stdio;
	char** args;

	char *uvbuf;

	uint64_t conn_counter;
	uint64_t read_counter;

	tommy_hashdyn *labels;

	void *data; // for parser-data

	uv_tcp_t server;
	uv_tcp_t client;
	uv_loop_t* loop;
	uv_connect_t connect;
	uv_write_t write_req;
	uv_shutdown_t shutdown_req;

	char is_async_writing;
	char is_writing;
	char is_write_error;
	char is_closing;
	uint8_t is_http_query;
	uint8_t tls;
	mbedtls_ssl_context tls_ctx;
	mbedtls_pk_context tls_key;
	mbedtls_x509_crt tls_cert;
	mbedtls_x509_crt tls_cacert;
	mbedtls_entropy_context tls_entropy;
	mbedtls_ctr_drbg_context tls_ctr_drbg;
	mbedtls_ssl_config tls_conf;
	char *tls_ca_file;
	char *tls_cert_file;
	char *tls_key_file;

	uv_buf_t write_buffer;
	uv_buf_t user_read_buf;
	char ssl_read_buffer[EVENT_BUFFER];
	char net_buffer_in[EVENT_BUFFER]; // preallocated buffer for uv alloc
	unsigned int ssl_read_buffer_len;
	unsigned int ssl_read_buffer_offset;
	unsigned int ssl_write_offset;
	unsigned int ssl_write_buffer_len;
	char* ssl_write_buffer;
	uv_buf_t request_buffer;
	uv_buf_t response_buffer;

	char host[URL_SIZE];
	char port[PORT_SIZE];
	uint64_t timeout;
	uint64_t count;

	uint8_t parsed;

	tommy_node node;
} context_arg;
