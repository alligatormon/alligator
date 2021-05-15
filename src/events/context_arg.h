#pragma once
#define PORT_SIZE 6
#define URL_SIZE 65535
#define AUTH_SIZE 1024
#define EVENT_BUFFER 65536
#include <uv.h>
#include <jansson.h>
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
#include "common/url.h"

typedef struct env_struct {
	char *k;
	char *v;

	tommy_node node;
} env_struct;

typedef struct context_arg
{
	char *name;
	struct sockaddr_in *dest;
	//uv_connect_t *connect;
	uv_tcp_t *socket;
	uv_timer_t tt_timer;
	//uv_handle_t * server;
	uv_fs_event_t fs_handle;
	char *key;
	char *parser_name;
	void *parser_handler;
	char *mesg;
	size_t mesg_len;
	char *query_url;
	//char *port;
	uint8_t lock; // lock for aggregator scrape
	uint8_t data_lock; // lock for parser scrape
	uint8_t proto;
	uint8_t transport;
	char *transport_string;
	int write;
	//size_t http_body_size;
	//size_t expect_http_length;
	uv_buf_t *buffer;
	size_t buflen;
	//char *http_body;
	regex_match *rematch;
	mapping_metric *mm;

	// chunk body read
	string *full_body;
	//int8_t expect_json;
	uint64_t expect_body_length;
	int64_t chunked_size;
	int64_t chunked_expect;
	uint8_t chunked_done;
	int8_t (*expect_function)(char *, size_t);
	uint8_t expect_count;
	uint8_t read_count;

	uint64_t buffer_request_size;
	uint64_t buffer_response_size;

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

	tommy_hashdyn *labels;
	tommy_hashdyn *env;

	// counters
	uint64_t conn_counter;
	uint64_t read_counter;
	uint64_t write_counter;
	uint64_t timeout_counter;
	uint64_t tls_read_counter;
	uint64_t tls_write_counter;
	uint64_t tls_init_counter;
	uint64_t shutdown_counter;
	uint64_t close_counter;
	uint64_t write_bytes_counter;
	uint64_t tls_write_bytes_counter;
	uint64_t read_bytes_counter;
	uint64_t tls_read_bytes_counter;
	uint64_t tls_connect_time_counter;
	uint64_t tls_read_time_counter;
	uint64_t tls_write_time_counter;
	uint64_t connect_time_counter;
	uint64_t read_time_counter;
	uint64_t write_time_counter;
	uint64_t shutdown_time_counter;
	uint64_t close_time_counter;
	uint64_t exec_time_counter;

	r_time connect_time;
	r_time connect_time_finish;
	r_time write_time;
	r_time write_time_finish;
	r_time read_time;
	r_time read_time_finish;
	r_time exec_time;
	r_time exec_time_finish;
	r_time tls_connect_time;
	r_time tls_connect_time_finish;
	r_time tls_write_time;
	r_time tls_write_time_finish;
	r_time tls_read_time;
	r_time tls_read_time_finish;
	r_time shutdown_time;
	r_time shutdown_time_finish;
	r_time close_time;
	r_time close_time_finish;

	void *data; // for parser-data
	char *ns; // for parsers ns
	uint8_t parser_status;

	uv_tcp_t server;
	uv_tcp_t client; // move only with pclient
	uv_pipe_t pclient; // move only with client!
	uv_loop_t* loop;
	uv_connect_t connect;
	uv_write_t write_req;
	uv_shutdown_t shutdown_req;
	uv_udp_t *udp_server;
	uv_poll_t *poll;
	uv_poll_t poll2;
	uv_pipe_t *pipe;
	uv_udp_t udp_client;
	uv_udp_send_t udp_send;
	uv_fs_t *open;
	uv_fs_t *read;
	const char *filename;
	char *path;
	uint8_t is_dir;
	uint64_t offset;
	uint64_t files_count;

	uint64_t file_stat;
	char *checksum;
	uint8_t calc_lines;
	uint8_t notify;
	uint8_t state; // stream | begin | save

	char is_async_writing;
	char is_writing;
	char is_write_error;
	char is_closing;
	uint8_t is_http_query;
	uint8_t follow_redirects;
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
	char user[AUTH_SIZE];
	char password[AUTH_SIZE];
	uint64_t timeout;
	uint64_t count;

	uint64_t parsed;

	int64_t ttl; // TTL for this context metrics
	int64_t curr_ttl;
	uint8_t headers_pass;
	uint64_t headers_size;
	uint8_t api_enable;

	char *url;

	tommy_hashdyn* reject;
	void *srv_carg;

	// unixgram info
	struct sockaddr_un *local;
	socklen_t local_len;
	struct sockaddr_un *remote;
	socklen_t remote_len;
	int fd;

	int log_level;
	int64_t context_ttl;

	tommy_node node;
	tommy_node context_node;
} context_arg;

context_arg *carg_copy(context_arg *src);
context_arg* context_arg_json_fill(json_t *root, host_aggregator_info *hi, void *handler, char *parser_name, char *mesg, size_t mesg_len, void *data, void *expect_function, uint8_t headers_pass, uv_loop_t *loop, tommy_hashdyn *env, uint64_t follow_redirects);
tommy_hashdyn *env_struct_parser(json_t *root);
tommy_hashdyn* env_struct_duplicate(tommy_hashdyn *src);
json_t* env_struct_dump(tommy_hashdyn *src);
