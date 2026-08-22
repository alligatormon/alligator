#pragma once
#include "dstructures/tommy.h"

typedef struct aggregate_handler {
	void (*name)(char*, size_t, context_arg*);
	int8_t (*validator)(context_arg*, char*, size_t);
	string* (*mesg_func)(host_aggregator_info*, void *arg, void *env, void *proxy_settings);
	int (*smart_aggregator_replace)(context_arg*);
	uint8_t headers_pass;
	char key[255];
	int no_exit_status;
	int no_metric;
} aggregate_handler;

typedef struct aggregate_context
{
	uint64_t handlers;
	aggregate_handler *handler;
	void *data;
	void* (*data_func)(host_aggregator_info*, void *arg, void *data);

	tommy_node node;
	char *key;
} aggregate_context;

int actx_compare(const void* arg, const void* obj);
int aggregator_compare(const void* arg, const void* obj);
void try_again(context_arg *carg, char *mesg, size_t mesg_len, void *handler, char *parser_name, void *validator, char *override_key, void *data);
context_arg *aggregator_oneshot(context_arg *carg, char *url, size_t url_len, char *mesg, size_t mesg_len, void *handler, char *parser_name, void *validator, char *override_key, uint64_t follow_redirects, void *data, char *s_stdin, size_t l_stdin, string* work_dir, alligator_ht *env);

/* Sequential oneshot: same transport as aggregator_oneshot(), then pump the
 * shared libuv loop (uva_await in events/future.c) until the parser handler
 * or empty-failure path runs. Loop thread only. Do not call from a uv_close
 * callback, or from an in-flight parent connection's connect/read/close
 * callbacks.
 *
 * handler may be NULL; if set it runs on the oneshot carg before the wait
 * completes (same signature as aggregator_oneshot). override_key ownership
 * is transferred, same as aggregator_oneshot; NULL generates a unique key.
 *
 * Returns 0 or a UV_ error. out is filled on success and on transport
 * failure (http_code 0, empty body). Caller must aggregator_await_res_free(). */
typedef struct aggregator_await_res {
	int status;
	int http_code;
	char *body;
	size_t body_len;
} aggregator_await_res_t;

void aggregator_await_res_free(aggregator_await_res_t *r);
int aggregator_oneshot_await(context_arg *carg, char *url, size_t url_len, char *mesg, size_t mesg_len, void *handler, char *parser_name, void *validator, char *override_key, uint64_t follow_redirects, void *data, char *s_stdin, size_t l_stdin, string *work_dir, alligator_ht *env, aggregator_await_res_t *out);

/* Kick the transport immediately (TCP/TLS/unix/udp/exec/file/pg/mysql/cassandra).
 * Safe to call more than once: connect functions no-op while lock is set.
 * aggregator_oneshot() calls this after insert; DNS completion calls
 * aggregator_oneshot_retry_host() so hostname oneshots do not wait for crawl. */
void aggregator_oneshot_start(context_arg *carg);
void aggregator_oneshot_retry_host(const char *host);
int smart_aggregator_default_key(char *key, const char *transport_string, const char *parser_name, const char *host, const char *port, const char *query);
void smart_aggregator_key_normalize(char *key);
void smart_aggregator_del(context_arg *carg);
int smart_aggregator(context_arg *carg);
void smart_aggregator_del_key(char *key);
void smart_aggregator_del_key_gen(char *transport_string, char *parser_name, char *host, char *port, char *query);
void aggregators_free();
void aggregate_ctx_free();
void entrypoints_free();
void aggregate_ctx_init();
