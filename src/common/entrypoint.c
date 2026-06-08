#include "common/entrypoint.h"
#include "main.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

int entrypoint_compare(const void *arg, const void *obj)
{
	char *s1 = (char *)arg;
	char *s2 = ((context_arg *)obj)->key;
	return strcmp(s1, s2);
}

int entrypoint_key_match_transport(const char *key, const char *prefix, const char *host, uint16_t port)
{
	if (!key || !prefix || !host)
		return 0;

	char kp[16];
	uint64_t idx;
	char kh[HOSTHEADER_SIZE];
	unsigned p;

	if (sscanf(key, "%15[^:]:%" SCNu64 ":%231[^:]:%u", kp, &idx, kh, &p) != 4)
		return 0;

	return !strcmp(kp, prefix) && !strcmp(kh, host) && p == (unsigned)port;
}

typedef struct entrypoint_collect_ctx {
	const char *prefix;
	const char *host;
	uint16_t port;
	context_arg **list;
	size_t n;
	size_t cap;
} entrypoint_collect_ctx;

static void entrypoint_collect_cb(void *funcarg, void *arg)
{
	entrypoint_collect_ctx *ctx = funcarg;
	context_arg *carg = arg;

	if (!carg || !carg->key)
		return;
	if (!entrypoint_key_match_transport(carg->key, ctx->prefix, ctx->host, ctx->port))
		return;

	if (ctx->n >= ctx->cap) {
		size_t ncap = ctx->cap ? ctx->cap * 2 : 4;
		context_arg **nb = realloc(ctx->list, ncap * sizeof(*nb));
		if (!nb)
			return;
		ctx->list = nb;
		ctx->cap = ncap;
	}
	ctx->list[ctx->n++] = carg;
}

size_t entrypoint_collect_transport(const char *prefix, const char *host, uint16_t port, context_arg ***out)
{
	entrypoint_collect_ctx ctx = { prefix, host, port, NULL, 0, 0 };

	if (!out || !ac || !ac->entrypoints)
		return 0;

	alligator_ht_foreach_arg(ac->entrypoints, entrypoint_collect_cb, &ctx);
	*out = ctx.list;
	return ctx.n;
}

void entrypoint_carg_replace_key(context_arg *carg, const char *fmt, ...)
{
	char buf[255];
	va_list ap;

	if (!carg)
		return;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	if (carg->key)
		free(carg->key);
	carg->key = strdup(buf);
}

void entrypoint_shutdown_carg(context_arg *carg)
{
	if (!carg)
		return;

	carg_uv_detach_timers(carg);

	if (carg->threads && carg->entrypoint_stop_async_ready) {
		uv_async_send(&carg->entrypoint_stop_async);
		return;
	}

	if (carg->pipe && !uv_is_closing((uv_handle_t *)carg->pipe))
		uv_close((uv_handle_t *)carg->pipe, NULL);
	else if (!uv_is_closing((uv_handle_t *)&carg->server))
		uv_close((uv_handle_t *)&carg->server, NULL);
	else if (!uv_is_closing((uv_handle_t *)&carg->udp_server)) {
		uv_udp_recv_stop(&carg->udp_server);
		uv_close((uv_handle_t *)&carg->udp_server, NULL);
	} else if (!uv_is_closing((uv_handle_t *)&carg->poll_socket)) {
		uv_poll_stop(&carg->poll_socket);
		uv_close((uv_handle_t *)&carg->poll_socket, NULL);
	}

	carg_free(carg);
}

typedef struct entrypoint_collect_all_ctx {
	context_arg **list;
	size_t n;
	size_t cap;
} entrypoint_collect_all_ctx;

static void entrypoint_collect_all_cb(void *funcarg, void *arg)
{
	entrypoint_collect_all_ctx *ctx = funcarg;
	context_arg *carg = arg;

	if (!carg)
		return;

	if (ctx->n >= ctx->cap) {
		size_t ncap = ctx->cap ? ctx->cap * 2 : 8;
		context_arg **nb = realloc(ctx->list, ncap * sizeof(*nb));
		if (!nb)
			return;
		ctx->list = nb;
		ctx->cap = ncap;
	}
	ctx->list[ctx->n++] = carg;
}

void entrypoint_shutdown_all(void)
{
	entrypoint_collect_all_ctx ctx = { NULL, 0, 0 };
	size_t i;

	if (!ac || !ac->entrypoints)
		return;

	alligator_ht_foreach_arg(ac->entrypoints, entrypoint_collect_all_cb, &ctx);

	for (i = 0; i < ctx.n; ++i) {
		context_arg *carg = ctx.list[i];
		if (!carg)
			continue;
		alligator_ht_remove_existing(ac->entrypoints, &(carg->context_node));
		entrypoint_shutdown_carg(carg);
	}

	free(ctx.list);
}
