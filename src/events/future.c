#include "events/future.h"

#include <stdlib.h>
#include <string.h>

#ifndef UVA_DEFAULT_MAX_DEPTH
#define UVA_DEFAULT_MAX_DEPTH 8
#endif

static __thread int uva_depth;
static int uva_max_depth = UVA_DEFAULT_MAX_DEPTH;

int uva_loop_depth(uv_loop_t *loop)
{
	(void)loop;
	return uva_depth;
}

void uva_set_max_await_depth(int depth)
{
	uva_max_depth = depth > 0 ? depth : UVA_DEFAULT_MAX_DEPTH;
}

int uva_max_await_depth(void)
{
	return uva_max_depth;
}

void uva_future_init(uva_future_t *f, uv_loop_t *loop)
{
	memset(f, 0, sizeof(*f));
	f->loop = loop;
	f->gen = 1;
}

void uva_future_reset(uva_future_t *f)
{
	uv_loop_t *loop = f->loop;
	uint64_t gen = f->gen + 1;
	memset(f, 0, sizeof(*f));
	f->loop = loop;
	f->gen = gen ? gen : 1;
}

void uva_future_complete(uva_future_t *f, int status)
{
	if (!f || f->done || f->cancelled)
		return;
	f->status = status;
	f->done = 1;
}

void uva_future_complete_gen(uva_future_t *f, uint64_t gen, int status)
{
	if (!f || f->gen != gen)
		return;
	uva_future_complete(f, status);
}

void uva_future_cancel(uva_future_t *f, int status)
{
	if (!f)
		return;
	f->cancelled = 1;
	if (!f->done) {
		f->status = status;
		f->done = 1;
	}
}

int uva_await(uva_future_t *f)
{
	int status;

	if (!f || !f->loop)
		return UV_EINVAL;
	if (uva_depth >= uva_max_depth)
		return UV_ELOOP;

	uva_depth++;
	while (!f->done) {
		/*
		 * UV_RUN_ONCE blocks until at least one event, then returns.
		 * That is the C equivalent of "await": this call does not
		 * busy-spin, and other handles on the same loop run too.
		 */
		int r = uv_run(f->loop, UV_RUN_ONCE);
		if (r == 0 && !f->done) {
			uva_depth--;
			return UV_EOF; /* idle loop, future can never complete */
		}
	}
	status = f->status;
	uva_depth--;
	return status;
}

typedef struct {
	uva_future_t *f;
	uv_timer_t timer;
	int closed;
} uva_timeout_wait_t;

static void on_await_timeout(uv_timer_t *t)
{
	uva_timeout_wait_t *w = t->data;
	if (!w->f->done)
		uva_future_cancel(w->f, UV_ETIMEDOUT);
}

static void on_timer_closed(uv_handle_t *h)
{
	uva_timeout_wait_t *w = h->data;
	w->closed = 1;
}

int uva_await_timeout(uva_future_t *f, uint64_t timeout_ms)
{
	uva_timeout_wait_t w;
	int status;

	if (!f || !f->loop)
		return UV_EINVAL;
	if (timeout_ms == 0)
		return uva_await(f);

	memset(&w, 0, sizeof(w));
	w.f = f;

	uv_timer_init(f->loop, &w.timer);
	w.timer.data = &w;
	uv_timer_start(&w.timer, on_await_timeout, timeout_ms, 0);

	status = uva_await(f);

	uv_timer_stop(&w.timer);
	if (!uv_is_closing((uv_handle_t *)&w.timer)) {
		uv_close((uv_handle_t *)&w.timer, on_timer_closed);
		while (!w.closed)
			uv_run(f->loop, UV_RUN_ONCE);
	}

	return status;
}
