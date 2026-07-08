#include "common/log_jansson.h"

#include <stdlib.h>
#include <string.h>

typedef struct log_jansson_dump_len_ctx {
	size_t len;
} log_jansson_dump_len_ctx;

typedef struct log_jansson_dump_write_ctx {
	char *buf;
	size_t off;
} log_jansson_dump_write_ctx;

static int log_jansson_dump_count_cb(const char *buffer, size_t size, void *data)
{
	log_jansson_dump_len_ctx *ctx = data;

	if (!ctx)
		return -1;

	ctx->len += size;
	return 0;
}

static int log_jansson_dump_write_cb(const char *buffer, size_t size, void *data)
{
	log_jansson_dump_write_ctx *ctx = data;

	if (!ctx || !ctx->buf)
		return -1;

	memcpy(ctx->buf + ctx->off, buffer, size);
	ctx->off += size;
	return 0;
}

static char *log_jansson_dumps_alloc(const json_t *doc, size_t extra, size_t *outlen)
{
	log_jansson_dump_len_ctx lctx = {0};
	log_jansson_dump_write_ctx wctx;
	char *ret;
	size_t total;

	if (!doc)
		return NULL;

	if (json_dump_callback(doc, log_jansson_dump_count_cb, &lctx, JSON_COMPACT) != 0)
		return NULL;

	total = lctx.len + extra;
	ret = malloc(total + 1);
	if (!ret)
		return NULL;

	wctx.buf = ret;
	wctx.off = 0;
	if (json_dump_callback(doc, log_jansson_dump_write_cb, &wctx, JSON_COMPACT) != 0)
	{
		free(ret);
		return NULL;
	}

	ret[lctx.len] = '\0';
	if (outlen)
		*outlen = total;
	return ret;
}

char *log_jansson_dumps_compact(const json_t *doc, size_t *outlen)
{
	return log_jansson_dumps_alloc(doc, 0, outlen);
}

char *log_jansson_dumps_line(const json_t *doc, size_t *outlen)
{
	char *ret;
	size_t jsonlen;

	ret = log_jansson_dumps_alloc(doc, 1, &jsonlen);
	if (!ret)
		return NULL;

	ret[jsonlen - 1] = '\n';
	ret[jsonlen] = '\0';
	if (outlen)
		*outlen = jsonlen;
	return ret;
}
