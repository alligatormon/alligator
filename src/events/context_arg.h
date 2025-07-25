#pragma once
#define PORT_SIZE 6
#define HOSTHEADER_SIZE 232
#define AUTH_SIZE 1024
#define EVENT_BUFFER 65536
#include <uv.h>
#include <jansson.h>
#include "common/rtime.h"
#include "common/pcre_parser.h"
#include "metric/metrictree.h"
#include "dstructures/tommy.h"
#include "common/netlib.h"
#include "cluster/type.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include "common/selector.h"
#include "common/url.h"
#include "common/patricia.h"
#include "picohttpparser.h"
#define ENTRYPOINT_RETURN_EMPTY 1
#define ENTRYPOINT_AGGREGATION_COUNT 1
enum ssl_mode { SSLMODE_SERVER, SSLMODE_CLIENT };

typedef struct env_struct {
	char *k;
	char *v;

	tommy_node node;
} env_struct;

typedef struct threaded_loops {
	uv_loop_t **loop;
	uint64_t cur;
	uint64_t max;

	uv_thread_t thread;
	char *key;
	tommy_node node;
} threaded_loop;

typedef struct context_arg
{
	char *name;
	struct sockaddr_in dest;
	struct sockaddr_in *recv;
	//uv_connect_t *connect;
	uv_tcp_t *socket;
	uv_timer_t *tt_timer;
	//uv_handle_t * server;
	uv_fs_event_t fs_handle;
	char *key;
	char *parser_name;
	void *parser_handler;
	char *mesg;
	size_t mesg_len;
	char *stdin_s;
	size_t stdin_l;
	char *auth_header;
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
	regex_match **rematch;
	mapping_metric *mm;
	struct phr_chunked_decoder chunked_dec;

	// chunk body read
	string *full_body;
	//int8_t expect_json;
	uint64_t expect_body_length;
	uint8_t no_exit_status;
	int64_t chunked_expect;
	char *chunked_ptr;
	int8_t (*expect_function)(struct context_arg*, char *, size_t);
	uint8_t expect_count;
	uint8_t read_count;
	uint64_t pingloop;
	double pingpercent_success;

	uint64_t buffer_request_size;
	uint64_t buffer_response_size;

	char *namespace;
	uint8_t namespace_allocated;
	alligator_ht *auth_basic;
	alligator_ht *auth_bearer;
	alligator_ht *auth_other;
	char *body;
	uint8_t body_read;
	patricia_t *net_tree_acl;
	patricia_t *net6_tree_acl;

	// for process spawn
	uv_process_options_t options;
	uv_pipe_t channel;
	uv_stdio_container_t child_stdio[3];
	uv_process_t child_req;
	char** args;
	string *work_dir;

	char *lang;

	char *uvbuf;

	alligator_ht *labels;
	alligator_ht *env;

	// counters
	uint64_t open_counter;
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

	uint8_t no_metric;
	uint8_t no_collect;
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

	percentile_buffer* q_request_time;
	percentile_buffer* q_read_time;
	percentile_buffer* q_connect_time;

	uint64_t push_parsing_time;
	uint64_t push_metric_time;
	uint64_t push_split_data;

	void *data; // for parser-data
	char *ns; // for parsers ns
	uint8_t parser_status;
	int8_t running;

	uv_tcp_t server;
	uv_tcp_t client; // move only with pclient
	uv_pipe_t pclient; // move only with client!
	uv_loop_t* loop;
	int loop_allocated;
	uv_connect_t connect;
	uv_write_t write_req;
	uv_write_t write_tls;
	uv_shutdown_t shutdown_req;
	uv_udp_t udp_server;
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

	uint8_t is_http_query;
	uint8_t follow_redirects;
	uint8_t tls;
	SSL_CTX *ssl_ctx;
	SSL *ssl;
	BIO *rbio;
	BIO *wbio;
	uint8_t tls_handshake_done;
	char *tls_ca_file;
	char *tls_cert_file;
	char *tls_key_file;
	char *tls_server_name;
	uint8_t tls_verify;

	uv_buf_t write_buffer;
	uv_buf_t user_read_buf;
	char ssl_read_buffer[EVENT_BUFFER];
	char net_buffer_in[EVENT_BUFFER]; // preallocated buffer for uv alloc
	char* ssl_write_buffer;
	uv_buf_t request_buffer;
	uv_buf_t response_buffer;
	string *response; // temporary buffer for pass to parsers

	char host[HOSTHEADER_SIZE];
	char port[PORT_SIZE];
	uint16_t numport;
	char user[AUTH_SIZE];
	char password[AUTH_SIZE];
	uint64_t timeout;
	uint64_t count;
	uv_thread_t thread;
	uint64_t threads; //listener
	char* threaded_loop_name; //aggregator

	uint64_t parsed;

	int64_t ttl; // TTL for this context metrics
	int64_t curr_ttl;
	uint8_t headers_pass;
	uint64_t headers_size;
	uint8_t api_enable;

	uint8_t remove_from_hash; // enable if deleting 1 object

	char *url;

	alligator_ht* reject;
	void *srv_carg;

	// unixgram info
	struct sockaddr_un *local;
	socklen_t local_len;
	struct sockaddr_un *remote;
	socklen_t remote_len;
	int fd;

	int log_level;
	int64_t context_ttl;

	//uint64_t sequence_size;
	uint64_t sequence_done;
	uint64_t sequence_success;
	uint64_t sequence_error;
	uint64_t packets_send_period;
	uint64_t timeout_time;
	uint64_t sequence_time;
	uint16_t sequence_id;
	uint16_t packets_id;
	uint8_t check_receive;

	uv_poll_t poll_socket;
	uv_timer_t t_timeout;
	uv_timer_t t_towrite;
	uv_timer_t t_seq_timer;
	uv_timer_t resolver_timer;

	char *rrtype;
	uint32_t rrclass;
	uint8_t resolver;
	void *rd; //resolver_data
	r_time resolve_time;
	r_time resolve_time_finish;
	r_time total_time;
	r_time total_time_finish;

	uint32_t ping_key;

	char *cluster;
	uint8_t cluster_size;
	cluster_node *cluster_node;
	char *instance; // only for entrypoint, not aggregate!
	uint8_t rreturn; // only for entrypoint, not aggregate!
	uint8_t metric_aggregation; // only for entrypoint, not aggregate!
	char **pquery; // parser query
	uint8_t pquery_size;

	// overwrited period
	uint64_t period;
	uv_timer_t *period_timer;

	tommy_node node;
	tommy_node context_node;
	tommy_node ping_node;
} context_arg;

context_arg *carg_copy(context_arg *src);
context_arg* context_arg_json_fill(json_t *root, host_aggregator_info *hi, void *handler, char *parser_name, char *mesg, size_t mesg_len, void *data, void *expect_function, uint8_t headers_pass, uv_loop_t *loop, alligator_ht *env, uint64_t follow_redirects, char *stdin_s, size_t stdin_l);
alligator_ht *env_struct_parser(json_t *root);
alligator_ht* env_struct_duplicate(alligator_ht *src);
json_t* env_struct_dump(alligator_ht *src);
void env_struct_duplicate_foreach(void *funcarg, void* arg);
void env_struct_free(void *funcarg, void* arg);
void carg_free(context_arg *carg);
void aconf_mesg_set(context_arg *carg, char *mesg, size_t mesg_len);
void env_struct_push_alloc(alligator_ht* hash, char *k, char *v);
void env_free(alligator_ht *env);
void carglog(context_arg *carg, int priority, const char *format, ...);
void carg_or_glog(context_arg *carg, int priority, const char *format, ...);
void parse_add_label(context_arg *carg, json_t *root);
void thread_loop_set(char *key, uint64_t size);
void thread_loop_free();
uv_loop_t *get_threaded_loop_t(char *key);
uv_loop_t *get_threaded_loop_t_or_default(char *key);
uint64_t get_threads_num(json_t *value);
