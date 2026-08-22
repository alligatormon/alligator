#ifndef ALLIGATOR_EVENTS_FUTURE_H
#define ALLIGATOR_EVENTS_FUTURE_H

/*
 * Sequential-looking wait over a shared libuv loop (nested uv_run).
 *
 * There is no language-level async/await in C. This emulates it: a call
 * starts a libuv request, then pumps the loop with UV_RUN_ONCE until that
 * request completes. Other handles on the same loop keep making progress.
 *
 * Safe to call from outside uv_run, or nested while the loop is already
 * running. Not safe across threads: one loop → one thread.
 *
 * Production HTTP/TCP/TLS/exec uses aggregator_oneshot() /
 * aggregator_oneshot_await() on events/client.c. This file is only the
 * wait primitive used by aggregator_oneshot_await().
 *
 * Do not call uva_await / uva_await_timeout from a uv_close callback:
 * the close walk is not re-entrant.
 */

#include <stddef.h>
#include <stdint.h>
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* ALLIGATOR_EVENTS_FUTURE_H */
