#ifndef UVA_H
#define UVA_H

/*
 * uvasyncawait — sequential-looking async I/O over a shared libuv loop.
 *
 * There is no language-level async/await in C. This library emulates it:
 * each "blocking" call starts a libuv request, then pumps the loop with
 * UV_RUN_ONCE until that request completes. Other handles on the same
 * loop keep making progress (non-blocking wait for the thread).
 *
 * Safe to call from outside uv_run, or nested while the loop is already
 * running (nested uv_run). Not safe across threads: one loop → one thread.
 *
 * Do not call uva_await / uva_await_timeout from a uv_close callback:
 * the close walk is not re-entrant.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Future / await ---------------------------------------------------- */

typedef struct uva_future {
	uv_loop_t *loop;
	volatile int done;
	int status;   /* 0 or libuv UV_ error */
	int cancelled;
	uint64_t gen; /* incremented on reset; late callbacks must match */
	void *data;
	size_t n;
} uva_future_t;

void uva_future_init(uva_future_t *f, uv_loop_t *loop);
void uva_future_reset(uva_future_t *f);
void uva_future_complete(uva_future_t *f, int status);
void uva_future_complete_gen(uva_future_t *f, uint64_t gen, int status);
void uva_future_cancel(uva_future_t *f, int status);

/* Pump loop until f completes. Returns f->status. */
int uva_await(uva_future_t *f);

/* Same with wall-clock timeout (UV_ETIMEDOUT). Marks the future cancelled. */
int uva_await_timeout(uva_future_t *f, uint64_t timeout_ms);

/* Nested uv_run depth for this thread (one loop → one thread). */
int uva_loop_depth(uv_loop_t *loop);
void uva_set_max_await_depth(int depth);
int uva_max_await_depth(void);

/* ---- TCP client -------------------------------------------------------- */

typedef struct uva_tcp uva_tcp_t;

uva_tcp_t *uva_tcp_new(uv_loop_t *loop);
void uva_tcp_free(uva_tcp_t *c);

/*
 * Absolute deadline from uv_now(loop), in milliseconds. 0 disables.
 * Every subsequent connect/write/read uses the remaining budget.
 */
void uva_tcp_set_deadline(uva_tcp_t *c, uint64_t deadline_ms);

/* DNS resolve + connect. 0 or UV_ error. */
int uva_tcp_connect(uva_tcp_t *c, const char *host, int port);

/* Connect to an already-resolved address (skip getaddrinfo). */
int uva_tcp_connect_addr(uva_tcp_t *c, const struct sockaddr *addr);

/* Write all of buf. Returns len or UV_ error (<0). */
ssize_t uva_tcp_write(uva_tcp_t *c, const void *buf, size_t len);

/* Read at least 1 byte (or EOF). Returns nread, 0 on EOF, or UV_ error. */
ssize_t uva_tcp_read(uva_tcp_t *c, void *buf, size_t len);

int uva_tcp_close(uva_tcp_t *c);

/* ---- UDP client -------------------------------------------------------- */

typedef struct uva_udp uva_udp_t;

uva_udp_t *uva_udp_new(uv_loop_t *loop);
void uva_udp_free(uva_udp_t *u);

/* Bind local address; pass port 0 for ephemeral. */
int uva_udp_bind(uva_udp_t *u, const char *ip, int port);

ssize_t uva_udp_send(uva_udp_t *u, const char *host, int port,
		     const void *buf, size_t len);

/* Wait for one datagram. peer_* may be NULL. */
ssize_t uva_udp_recv(uva_udp_t *u, void *buf, size_t len,
		     char *peer_ip, size_t peer_ip_len, int *peer_port);

int uva_udp_close(uva_udp_t *u);

/* ---- Unix domain stream client ----------------------------------------- */

typedef struct uva_unix uva_unix_t;

uva_unix_t *uva_unix_new(uv_loop_t *loop);
void uva_unix_free(uva_unix_t *c);

int uva_unix_connect(uva_unix_t *c, const char *path);
ssize_t uva_unix_write(uva_unix_t *c, const void *buf, size_t len);
ssize_t uva_unix_read(uva_unix_t *c, void *buf, size_t len);
int uva_unix_close(uva_unix_t *c);

/* ---- Process ----------------------------------------------------------- */

typedef struct uva_process uva_process_t;

typedef struct uva_process_opts {
	const char *file;
	char *const *args;     /* NULL-terminated; args[0] usually = file */
	char *const *env;      /* NULL = inherit */
	const char *cwd;       /* NULL = inherit */
	int capture_stdout;
	int capture_stderr;
	int pipe_stdin;
} uva_process_opts_t;

/* Spawn; does not wait. NULL on failure (sets errno-style via last error in UV). */
uva_process_t *uva_process_spawn(uv_loop_t *loop, const uva_process_opts_t *opts);

/* Await process exit. */
int uva_process_wait(uva_process_t *p, int64_t *exit_status, int *term_signal);

ssize_t uva_process_read_stdout(uva_process_t *p, void *buf, size_t len);
ssize_t uva_process_read_stderr(uva_process_t *p, void *buf, size_t len);
ssize_t uva_process_write_stdin(uva_process_t *p, const void *buf, size_t len);
int uva_process_close_stdin(uva_process_t *p);

void uva_process_free(uva_process_t *p);

/* ---- HTTP/1.1 client (http:// only) ------------------------------------ */

typedef struct uva_http_req {
	const char *method;        /* "GET" | "POST" */
	const char *url;           /* http:// only */
	const char *content_type;
	const void *body;
	size_t body_len;
	uint64_t timeout_ms;
	size_t max_body;
	int follow_redirects;
} uva_http_req_t;

typedef struct uva_http_res {
	int status;
	char *body;
	size_t body_len;
	char *content_type;
} uva_http_res_t;

int uva_http_request(uv_loop_t *loop, const uva_http_req_t *req, uva_http_res_t *out);
void uva_http_res_free(uva_http_res_t *r);

#ifdef __cplusplus
}
#endif

#endif /* UVA_H */
